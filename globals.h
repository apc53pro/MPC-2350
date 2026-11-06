#ifndef GLOBALS_H
#define GLOBALS_H

#include <SdFat.h>
#include "pico/platform.h"
#include "Arduino.h"
#include "SDmgr.h"

extern SdFat SD;

#if SDFAT_FILE_TYPE == 1
using FirmwareFile = File32;
#elif SDFAT_FILE_TYPE == 2
using FirmwareFile = ExFile;
#else
using FirmwareFile = FsFile;
#endif

static constexpr oflag_t FIRMWARE_FILE_READ = O_RDONLY;
static constexpr oflag_t FIRMWARE_FILE_WRITE = O_RDWR | O_CREAT | O_AT_END;
static constexpr oflag_t FIRMWARE_FILE_RW = O_RDWR;

static inline String firmwareFileName(FirmwareFile& file)
{
  SdBusGuard sdBusGuard;
  char name[256] = {0};
  file.getName(name, sizeof(name));
  return String(name);
}

static inline uint64_t firmwareSdCardSizeBytes()
{
  SdBusGuard sdBusGuard;
  return SD.card() ? ((uint64_t)SD.card()->sectorCount() * 512ULL) : 0ULL;
}

static inline uint64_t firmwareSdVolumeTotalBytes()
{
  SdBusGuard sdBusGuard;
  return (uint64_t)SD.clusterCount() * (uint64_t)SD.bytesPerCluster();
}

static inline uint64_t firmwareSdVolumeUsedBytes()
{
  SdBusGuard sdBusGuard;
  uint32_t cluster_count = SD.clusterCount();
  uint32_t free_cluster_count = SD.freeClusterCount();

  if(free_cluster_count > cluster_count)
  {
    return 0;
  }

  return (uint64_t)(cluster_count - free_cluster_count) * (uint64_t)SD.bytesPerCluster();
}


typedef enum{USER, SYSTEM, VACANT} partition_typ;

#define DOUBLE_SIZE 16
#define INT64_SIZE 16
#define INT32_SIZE 12
#define INT16_SIZE 10
#define INT8_SIZE 9
#define BOOL_SIZE 9
#define CHAR_SIZE 9

#define RED   0b11100000
#define GREEN 0b00011100
#define BLUE  0b00000011
#define BLACK 0b00000000
#define WHITE 0b11111111

extern const uint16_t font[128][16];

extern int leftBorder; //terminal left border
extern int rightBorder; //terminal right border

extern uint8_t PS2_CLOCK;
extern uint8_t PS2_DATA;

// This board/card path becomes unreliable above 15 MHz: 18.5 MHz can mount
// but later produces read failures, and higher clocks may not initialize.
#define SD_SPI_TARGET_HZ 15000000UL
extern bool sdInitialized;


extern volatile int readCount;
extern volatile bool extChar;
extern volatile bool release;
extern volatile bool readCountSet;
extern volatile uint8_t key_code;

// Legacy PS/2-to-character translation table. Key state and normalized events
// live in keyboard_runtime; column 1 is retained only for source compatibility.
extern uint16_t non_ext_make_LUT[256][2];

PSRAM extern char gridBuffer_char[40][60];


extern int char_grid_buffer_V_draw_offset;

typedef struct
{
  uint32_t value;

}var;

typedef struct
{
  partition_typ partition_type = VACANT;
  String partition_name; //manually make the limit 20 chars long
  uint32_t start_addr;
  uint32_t end_addr;
}partition;

#define partition_amount 256 //max amount of memory partitions
extern partition partitions[partition_amount];
#define partition_name_max 50

// Small general-purpose terminal MALLOC arena, counted in words. Font and
// display attachments live in the GPU; no graphics partitions are allocated.
#define memory_size (16u * 1024u) // 64 KiB

PSRAM extern var memory[memory_size];

struct line_pointer
{
  bool drawn = false;
  String line_pointer_str = ">:";
};

struct cursor
{
  int locationX = 2;
  int locationY = 0;

  bool showing = true;
  int blink_time_ms = 500;
  uint32_t ms = 0;
  char symbol = '_';
};

struct terminal_aspects
{
  struct cursor cur;
  struct line_pointer LP;

};

extern struct terminal_aspects terminal_aspects;

extern bool run_program;

extern String line;
extern bool terminal_on;

#endif
