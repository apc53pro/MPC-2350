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

#define VGA_WIDTH 640
#define VGA_HEIGHT 480
#define TXCOUNT (VGA_WIDTH * VGA_HEIGHT) // Total pixels in 640x480 resolution
#define VGA_4BIT_BUFFER_SIZE (TXCOUNT / 2)

extern uint8_t vga_data_array[TXCOUNT] __attribute__((aligned(4)));
extern uint8_t * address_pointer;

PSRAM extern uint8_t vga_640_480_4_01[VGA_4BIT_BUFFER_SIZE] __attribute__((aligned(4)));
extern const uint8_t vga_4bit_color[16];
extern uint16_t vga_4bit_expanded_pairs[256];

#define HSYNC 16   
#define VSYNC 17
// VGA timing constants
#define H_ACTIVE   655    // (active + frontporch - 1) - one cycle delay for mov
#define V_ACTIVE   479    // (active - 1)
#define RGB_ACTIVE 639    // (horizontal active) - 1
// Screen width/height
#define _width 640
#define _height 480
extern const int memory_size;

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

// Global lookup table for bit shifts (used in pixel operations)
extern const uint8_t shift_lookup[4];

extern volatile int readCount;
extern volatile bool extChar;
extern volatile bool release;
extern volatile bool readCountSet;
extern volatile uint8_t key_code;

// Legacy PS/2-to-character translation table. Key state and normalized events
// live in keyboard_runtime; column 1 is retained only for source compatibility.
extern uint16_t non_ext_make_LUT[256][2];

PSRAM extern char gridBuffer_char[40][60];

PSRAM extern uint8_t gridBuffer_charColor[40][60];
PSRAM extern uint16_t gridBuffer_backColor[40][60];

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

// Shared firmware graphics/font arena, counted in 32-bit words. The permanent
// font, half-resolution color, depth, and depth-stamp partitions require
// 116,224 words in total; 128 Ki words leaves 14,848 words of headroom.
#define memory_size (128u * 1024u) // 512 KiB


extern int SYS_FONT_PARTITION_INDEX;
extern int SYS_HALF_RES_BUFFER_PARTITION_INDEX;
extern int SYS_Z_BUFFER_PARTITION_INDEX;
extern int SYS_Z_STAMP_BUFFER_PARTITION_INDEX; // 19200 words, 4 stamp bytes per word

#define font_start_address 0
#define vbuff_start_address font_start_address + (256*8)

PSRAM extern var memory[memory_size];

struct line_pointer
{
  bool drawn = false;
  bool useBackColor = true;
  int color = 0;
  int backColor = 255;

  String line_pointer_str = ">:";
};

struct cursor
{
  int locationX = 2;
  int locationY = 0;

  bool showing = true;
  int blink_time_ms = 500;
  int ms = 0;
  char symbol = '_';
  int color = 0;
  int backColor = 255;
  bool useBackColor = true;
};

struct terminal_aspects
{
  struct cursor cur;
  struct line_pointer LP;

  int inputColor = 0;
  int inputBackColor = 255;
  bool useInputBackColor = true;
};

extern struct terminal_aspects terminal_aspects;

extern bool run_program;

extern String line;
extern bool graphics_mode;
extern bool terminal_on;

#endif
