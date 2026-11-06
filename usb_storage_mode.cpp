#include "usb_storage_mode.h"

#include <Arduino.h>
#include <FatFS.h>
#include <FatFSUSB.h>
#include <FatFSFlash.h>
#include "terminal.h"
#include <string.h>

#if RP2350_FATFS_FLASH_BACKEND_REVISION != 4 || RP2350_FATFSUSB_FOREGROUND_REVISION != 5
#error Build with Firmware/vendor-libraries; the stock flash/USB backends cannot be used.
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#error The firmware must be built with a board definition that declares PICO_FLASH_SIZE_BYTES.
#endif
#ifndef FS_START
#error The firmware must be built with a flash filesystem partition.
#endif
#ifndef FS_END
#error The firmware must be built with a flash filesystem partition.
#endif

static_assert(PICO_FLASH_SIZE_BYTES == 16u * 1024u * 1024u,
              "Select a 16 MB flash layout for this firmware.");
static_assert((FS_END - FS_START) == 12u * 1024u * 1024u,
              "Select the 16 MB (Sketch: 4 MB, FS: 12 MB) flash layout.");

namespace
{
volatile uint32_t storage_status = USB_STORAGE_DISABLED;
volatile uint32_t storage_owner_pid = 0u;
volatile bool flash_mounted = false;
bool fatfs_configured = false;

void set_status(UsbStorageModeStatus status)
{
  __atomic_store_n(&storage_status, (uint32_t)status, __ATOMIC_RELEASE);
}

void host_claimed(uint32_t)
{
  const uint32_t status = __atomic_load_n(&storage_status, __ATOMIC_ACQUIRE);
  if (status == USB_STORAGE_ENABLED)
    set_status(USB_STORAGE_HOST_OWNED);
}

void host_ejected(uint32_t)
{
  const uint32_t status = __atomic_load_n(&storage_status, __ATOMIC_ACQUIRE);
  if (status == USB_STORAGE_ENABLED || status == USB_STORAGE_HOST_OWNED)
    set_status(USB_STORAGE_EJECTED);
}

bool drive_ready(uint32_t)
{
  const uint32_t status = __atomic_load_n(&storage_status, __ATOMIC_ACQUIRE);
  if (status == USB_STORAGE_ENABLED)
  {
    // TEST UNIT READY is the earliest reliable indication that the host has
    // discovered the disk. From this point OFF requires a clean host eject.
    set_status(USB_STORAGE_HOST_OWNED);
    return true;
  }
  return status == USB_STORAGE_HOST_OWNED;
}

void set_volume_label_if_empty()
{
  char label[12] = {};
  fatfs::DWORD serial = 0;
  if (fatfs::f_getlabel("", label, &serial) == fatfs::FR_OK && label[0] == 0)
    fatfs::f_setlabel("RP2350FS");
}

bool configure_fatfs()
{
  if (fatfs_configured) return true;
  // Never format during boot or an ordinary firmware mount. Raw-flash mode
  // uses native 4 KiB sectors and avoids SPIFTL's large RAM map and metadata
  // writes while Windows is formatting the USB disk.
  fatfs::FatFSConfig config(false, false);
  if (!FatFS.setConfig(config)) return false;
  fatfs_configured = true;
  return true;
}
}

bool flash_storage_begin()
{
  if (usb_storage_active()) return false;
  if (__atomic_load_n(&flash_mounted, __ATOMIC_ACQUIRE)) return true;
  if (!configure_fatfs() || !FatFS.begin())
  {
    __atomic_store_n(&flash_mounted, false, __ATOMIC_RELEASE);
    return false;
  }
  set_volume_label_if_empty();
  __atomic_store_n(&flash_mounted, true, __ATOMIC_RELEASE);
  set_status(USB_STORAGE_DISABLED);
  return true;
}

bool flash_storage_ready()
{
  return __atomic_load_n(&flash_mounted, __ATOMIC_ACQUIRE) &&
         usb_storage_status() == USB_STORAGE_DISABLED;
}

int32_t usb_storage_enable(uint32_t owner_pid)
{
  const uint32_t status = usb_storage_status();
  if (status == USB_STORAGE_ENABLED || status == USB_STORAGE_HOST_OWNED ||
      status == USB_STORAGE_EJECTED)
    return USB_STORAGE_RESULT_UNCHANGED;
  if (!configure_fatfs()) return USB_STORAGE_RESULT_FILESYSTEM;

  // Failure here only means that the media has no FAT volume yet; USB mode
  // must still expose it so the host can perform the initial format.
  bool mounted_for_firmware =
      __atomic_load_n(&flash_mounted, __ATOMIC_ACQUIRE);
  if (!mounted_for_firmware)
  {
    mounted_for_firmware = FatFS.begin();
    if (mounted_for_firmware) set_volume_label_if_empty();
  }

  // The device and the host must never have the FAT volume mounted together.
  // There is nothing to unmount after a failed first mount. Avoiding end() also
  // keeps this path independent of backend synchronization behavior.
  if (mounted_for_firmware) FatFS.end();
  __atomic_store_n(&flash_mounted, false, __ATOMIC_RELEASE);
  __atomic_store_n(&storage_owner_pid, owner_pid, __ATOMIC_RELEASE);
  set_status(USB_STORAGE_ENABLED);

  FatFSUSB.onPlug(host_claimed);
  FatFSUSB.onUnplug(host_ejected);
  FatFSUSB.driveReady(drive_ready);
  if (!FatFSUSB.begin())
  {
    __atomic_store_n(&storage_owner_pid, 0u, __ATOMIC_RELEASE);
    if (FatFS.begin())
    {
      __atomic_store_n(&flash_mounted, true, __ATOMIC_RELEASE);
      set_status(USB_STORAGE_DISABLED);
    }
    else set_status(USB_STORAGE_ERROR);
    return USB_STORAGE_RESULT_USB;
  }
  // Finish the control endpoint handshake before returning to the terminal.
  // No host is also valid: ON then remains explicitly enabled and waiting.
  FatFSUSB.waitForConfiguration(3000u);
  return USB_STORAGE_RESULT_OK;
}

int32_t usb_storage_disable(uint32_t requester_pid)
{
  const uint32_t status = usb_storage_status();
  if (status == USB_STORAGE_DISABLED) return USB_STORAGE_RESULT_UNCHANGED;
  if (status == USB_STORAGE_ERROR) return USB_STORAGE_RESULT_FILESYSTEM;
  if (status == USB_STORAGE_HOST_OWNED) return USB_STORAGE_RESULT_BUSY;

  const uint32_t owner = usb_storage_owner_pid();
  if (requester_pid != 0u && owner != requester_pid)
    return USB_STORAGE_RESULT_NOT_OWNER;

  set_status(USB_STORAGE_ERROR);
  FatFSUSB.end();
  __atomic_store_n(&storage_owner_pid, 0u, __ATOMIC_RELEASE);
  if (!FatFS.begin())
  {
    __atomic_store_n(&flash_mounted, false, __ATOMIC_RELEASE);
    return USB_STORAGE_RESULT_FILESYSTEM;
  }
  __atomic_store_n(&flash_mounted, true, __ATOMIC_RELEASE);
  set_status(USB_STORAGE_DISABLED);
  return USB_STORAGE_RESULT_OK;
}

bool usb_storage_active()
{
  const uint32_t status = usb_storage_status();
  return status == USB_STORAGE_ENABLED || status == USB_STORAGE_HOST_OWNED ||
         status == USB_STORAGE_EJECTED;
}

uint32_t usb_storage_status()
{
  return __atomic_load_n(&storage_status, __ATOMIC_ACQUIRE);
}

uint32_t usb_storage_owner_pid()
{
  return __atomic_load_n(&storage_owner_pid, __ATOMIC_ACQUIRE);
}

const char *usb_storage_status_string()
{
  switch (usb_storage_status())
  {
    case USB_STORAGE_DISABLED: return "OFF";
    case USB_STORAGE_ENABLED: return "ON - WAITING FOR HOST";
    case USB_STORAGE_HOST_OWNED: return "ON - HOST OWNS DRIVE";
    case USB_STORAGE_EJECTED: return "ON - EJECTED; READY TO TURN OFF";
    default: return "ERROR";
  }
}

void usb_storage_poll()
{
  if (usb_storage_active()) FatFSUSB.poll();
}

void usb_storage_print_diagnostics()
{
  echo_message("=>USB STORAGE R5: " + String(usb_storage_status_string()));
  echo_message("=>USB CONNECTED=" + String(FatFSUSB.connected() ? 1 : 0) +
               " CONFIGURED=" + String(FatFSUSB.configured() ? 1 : 0));
  echo_message("=>USB RESETS=" + String(FatFSUSB.resetCount()) +
               " SETUP=" + String(FatFSUSB.setupCount()));
  echo_message("=>USB POLLS=" + String(FatFSUSB.pollCount()) +
               " MAX_GAP_US=" + String(FatFSUSB.maxPollGapUs()) +
               " LOCK_BUSY=" + String(FatFSUSB.mutexBusyCount()));
  echo_message("=>USB PACKETS READ=" + String(FatFSUSB.readCount()) +
               " WRITE=" + String(FatFSUSB.writeCount()) +
               " ERRORS=" + String(FatFSUSB.errorCount()));
  echo_message("=>FLASH OPS=" + String(fatfs_flash_diagnostics.operations) +
               " ERRORS=" + String(fatfs_flash_diagnostics.errors) +
               " LAST_ERROR=" + String(fatfs_flash_diagnostics.last_error));
  echo_message("=>FLASH STAGE=" + String(fatfs_flash_diagnostics.stage) +
               " OFFSET=" + String(fatfs_flash_diagnostics.last_offset) +
               " TIME_US=" + String(fatfs_flash_diagnostics.last_duration_us));
  echo_message("=>QMI FLASH BEFORE=" + String(fatfs_flash_diagnostics.qmi_flash_before, HEX) +
               " AFTER_ROM=" + String(fatfs_flash_diagnostics.qmi_flash_after_rom, HEX));
}
