#include <SdFat.h>
#include <SPI.h>
#include <pico/mutex.h>
#include "SDmgr.h"
#include "globals.h"

bool sdInitialized = false;

const int _MISO = 4;  // CONNECT TO THE MISO ON THE SD CARD READER
const int _MOSI = 3;  // CONNECT TO THE MOSI ON THE SD CARD READER
const int _CS = 5;
const int _SCK = 2;
static uint8_t sdProbeBuffer[512];
auto_init_recursive_mutex(g_sdBusMutex);

void sdBusAcquire()
{
  recursive_mutex_enter_blocking(&g_sdBusMutex);
}

bool sdBusTryAcquire()
{
  return recursive_mutex_try_enter(&g_sdBusMutex, nullptr);
}

void sdBusRelease()
{
  recursive_mutex_exit(&g_sdBusMutex);
}

static bool sdProbeMounted()
{
  if(sdInitialized == false || SD.card() == nullptr)
  {
    return false;
  }

  if(SD.card()->readSector(0, sdProbeBuffer) == false)
  {
    return false;
  }

  FirmwareFile root = SD.open("/", FIRMWARE_FILE_READ);

  if(root == false)
  {
    return false;
  }

  root.close();
  return true;
}

static int8_t sdBeginFresh()
{
  SD.end();
  sdInitialized = false;

  SPI.setRX(_MISO);
  SPI.setTX(_MOSI);
  SPI.setSCK(_SCK);
  SPI.begin();

  // Keep SdFat's saved SPISettings and the hardware clock in agreement.
  // The old code initialized at 15 MHz and then manually forced 12.5 MHz;
  // a later beginTransaction could silently restore the former value.
  sdInitialized = SD.begin(SdSpiConfig(_CS, DEDICATED_SPI, SD_SPI_TARGET_HZ));

  if(sdInitialized == false || sdProbeMounted() == false)
  {
    SD.end();
    sdInitialized = false;
    return -1;
  }

  return 1;
}

int8_t sdEnsureReady()
{
  SdBusGuard busGuard;

  if(sdProbeMounted() == true)
  {
    return 1;
  }

  SD.end();
  sdInitialized = false;
  delay(50);

  for(uint8_t attempt = 0; attempt < 2; attempt++)
  {
    if(sdBeginFresh() == 1)
    {
      return 1;
    }

    delay(100);
  }

  return -1;
}
