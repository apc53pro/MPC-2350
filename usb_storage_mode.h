#ifndef USB_STORAGE_MODE_H
#define USB_STORAGE_MODE_H

#include <stdint.h>

// These values are part of the NARM API. Keep them in sync with native_abi.h
// and narm.tch.
enum UsbStorageModeStatus : uint32_t
{
  USB_STORAGE_DISABLED = 0,
  USB_STORAGE_ENABLED = 1,
  USB_STORAGE_HOST_OWNED = 2,
  USB_STORAGE_EJECTED = 3,
  USB_STORAGE_ERROR = 4
};

enum UsbStorageModeResult : int32_t
{
  USB_STORAGE_RESULT_UNCHANGED = 0,
  USB_STORAGE_RESULT_OK = 1,
  USB_STORAGE_RESULT_BUSY = -1,
  USB_STORAGE_RESULT_FILESYSTEM = -2,
  USB_STORAGE_RESULT_USB = -3,
  USB_STORAGE_RESULT_NOT_OWNER = -4
};

// Mount the firmware-side flash filesystem. This does not start USB MSC.
bool flash_storage_begin();
bool flash_storage_ready();

// owner_pid is zero for the terminal, otherwise it is the NARM process which
// explicitly entered storage mode.
int32_t usb_storage_enable(uint32_t owner_pid);
int32_t usb_storage_disable(uint32_t requester_pid);
bool usb_storage_active();
uint32_t usb_storage_status();
uint32_t usb_storage_owner_pid();
const char *usb_storage_status_string();
void usb_storage_poll();
void usb_storage_print_diagnostics();

#endif
