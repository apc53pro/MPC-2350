#ifndef NATIVE_ABI_H
#define NATIVE_ABI_H

#include <stddef.h>
#include <stdint.h>

static const uint16_t NARM_ABI_MAJOR = 1;
static const uint16_t NARM_ABI_MINOR = 0;
static const uint16_t NARM_API_MAJOR = 1;
static const uint16_t NARM_API_MINOR = 12;

enum NativeControlFlag : uint32_t
{
  NARM_CONTROL_YIELD_REQUESTED = 1u << 0,
  NARM_CONTROL_STOP_REQUESTED = 1u << 1
};

enum NativeExitStatus : int32_t
{
  NARM_EXIT_USER_TERMINATED = -256,
  NARM_EXIT_CHAIN_EXEC = -257
};

enum NativeRunMode : uint32_t
{
  NARM_RUN_MODE_AUTO = 0,
  NARM_RUN_MODE_FOREGROUND = 1,
  NARM_RUN_MODE_BACKGROUND = 2
};

enum NativeSystemEvent : uint32_t
{
  NARM_EVENT_NONE = 0,
  NARM_EVENT_FOCUS_GAINED = 1,
  NARM_EVENT_FOCUS_LOST = 2,
  NARM_EVENT_CLOSE_REQUESTED = 3,
  NARM_EVENT_MODE_CHANGED = 4
};

enum NativeCapability : uint64_t
{
  NARM_CAP_NONE = 0,
  NARM_CAP_FRAMEBUFFER_2D = 1ull << 0,
  NARM_CAP_KEYBOARD = 1ull << 1,
  NARM_CAP_AUDIO = 1ull << 2,
  NARM_CAP_FILE_IO = 1ull << 3,
  NARM_CAP_USB_STORAGE = 1ull << 4
};

enum NativeServiceId : uint32_t
{
  NARM_SERVICE_YIELD = 0x0000,
  NARM_SERVICE_MILLIS = 0x0001,
  NARM_SERVICE_MICROS = 0x0002,
  NARM_SERVICE_RANDOM_U32 = 0x0003,
  NARM_SERVICE_SLEEP_MS = 0x0004,
  NARM_SERVICE_CHAIN_EXEC = 0x0005,
  NARM_SERVICE_SIN_F32 = 0x0006,
  NARM_SERVICE_COS_F32 = 0x0007,
  NARM_SERVICE_GET_PID = 0x0008,
  NARM_SERVICE_HAS_FOCUS = 0x0009,
  NARM_SERVICE_GET_RUN_MODE = 0x000a,
  NARM_SERVICE_GET_EFFECTIVE_RUN_MODE = 0x000b,
  NARM_SERVICE_POLL_SYSTEM_EVENT = 0x000c,
  NARM_SERVICE_SPAWN_PROGRAM = 0x000d,
  NARM_SERVICE_USB_STORAGE_ENABLE = 0x000e,
  NARM_SERVICE_USB_STORAGE_DISABLE = 0x000f,
  NARM_SERVICE_USB_STORAGE_STATUS = 0x0010
};

enum NativeUsbStorageStatus : uint32_t
{
  NARM_USB_STORAGE_DISABLED = 0,
  NARM_USB_STORAGE_ENABLED = 1,
  NARM_USB_STORAGE_HOST_OWNED = 2,
  NARM_USB_STORAGE_EJECTED = 3,
  NARM_USB_STORAGE_ERROR = 4
};

enum NativeInterfaceId : uint32_t
{
  NARM_INTERFACE_KEYBOARD = 0x0100,
  NARM_INTERFACE_FILE_IO = 0x0300,
  NARM_INTERFACE_AUDIO = 0x0700,
  NARM_INTERFACE_VIDEO_2D = 0x0400
};

enum NativeAudioStatusV1 : uint32_t
{
  NARM_AUDIO_RUNTIME_READY = 1u << 0,
  NARM_AUDIO_OUTPUT_READY = 1u << 1
};

enum NativeAudioCommandV1 : uint32_t
{
  NARM_AUDIO_PLAY_WAV = 0x0000,
  NARM_AUDIO_PLAY_PCM = 0x0001,
  NARM_AUDIO_STOP_VOICE = 0x0002,
  NARM_AUDIO_STOP_ALL = 0x0003,
  NARM_AUDIO_PLAY_WAV_FROM = 0x0004,
  NARM_AUDIO_PLAY_PCM_FROM = 0x0005,
  NARM_AUDIO_POSITION = 0x0006,
  NARM_AUDIO_PAUSE = 0x0007
};

enum NativeAudioPositionFlag : uint32_t
{
  NARM_AUDIO_POSITION_ACTIVE = 1u << 0,
  NARM_AUDIO_POSITION_PAUSED = 1u << 1
};

enum NativeFileCommandV1 : uint32_t
{
  NARM_FILE_MOUNT = 0x0000,
  NARM_FILE_EXISTS = 0x0001,
  NARM_FILE_CREATE = 0x0002,
  NARM_FILE_REMOVE = 0x0003,
  NARM_FILE_RENAME = 0x0004,
  NARM_FILE_SIZE = 0x0005,
  NARM_FILE_READ_AT = 0x0006,
  NARM_FILE_WRITE_AT = 0x0007,
  NARM_FILE_MKDIR = 0x0008,
  NARM_FILE_RMDIR = 0x0009,
  NARM_FILE_DIR_ENTRY = 0x000a
};

enum NativeServiceResult : int32_t
{
  NARM_SERVICE_OK = 0,
  NARM_SERVICE_ERR_UNKNOWN = -1,
  NARM_SERVICE_ERR_ARGUMENTS = -2,
  NARM_SERVICE_ERR_CAPABILITY = -3,
  NARM_SERVICE_ERR_BUSY = -4
};

// Extensible commands accepted by NativeVideoApiV1::submit. Commands operate
// on the firmware's shared frame, depth, and stamp buffers; no framebuffer is
// allocated in a NARM process. Arguments are fixed-width 32-bit words in the
// order used by the source-level NARMCC built-ins. Float arguments contain
// their IEEE-754 binary32 representation and pointer arguments are validated
// against the calling process before they are dereferenced.
enum NativeGraphicsCommandV1 : uint32_t
{
  NARM_GRAPHICS_HALF_PRESENT = 0x0000,
  NARM_GRAPHICS_HALF_CLEAR = 0x0001,
  NARM_GRAPHICS_HALF_PIXEL = 0x0002,
  NARM_GRAPHICS_HALF_FILL_RECT = 0x0003,
  NARM_GRAPHICS_HALF_LINE = 0x0004,
  NARM_GRAPHICS_HALF_FILL_TRIANGLE = 0x0005,
  NARM_GRAPHICS_HALF_BLIT = 0x0006,
  NARM_GRAPHICS_HALF_BLIT_MASKED = 0x0007,
  NARM_GRAPHICS_HALF_GLYPH = 0x0008,
  // Optional depth-only pass boundary. No arguments; preserves color, render
  // flags and counters. Flushes prior draws and invalidates depth/HiZ exactly
  // like a frame clear. Probe STATS_READ(76)==0x44430001 before calling.
  NARM_GRAPHICS_HALF_CLEAR_DEPTH = 0x0009,

  NARM_GRAPHICS_VGA4_GLYPH = 0x0100,

  NARM_GRAPHICS_SCREEN_GOURAUD_TRIANGLE = 0x0200,
  NARM_GRAPHICS_TRI3D_FLAT = 0x0201,
  NARM_GRAPHICS_QUAD3D_FLAT = 0x0202,
  NARM_GRAPHICS_TRI3D_GOURAUD = 0x0203,
  NARM_GRAPHICS_QUAD3D_GOURAUD = 0x0204,
  NARM_GRAPHICS_TRI3D_TEXTURED = 0x0205,
  NARM_GRAPHICS_QUAD3D_TEXTURED = 0x0206,
  NARM_GRAPHICS_QUAD3D_TEXTURED_TILED = 0x0207,
  NARM_GRAPHICS_TRI3D_TEXTURED_MASKED = 0x0208,
  NARM_GRAPHICS_QUAD3D_TEXTURED_MASKED = 0x0209,
  NARM_GRAPHICS_PACKED_TERRAIN_MESH_LIST = 0x020a,
  // Synchronous general-purpose textured list: args are descriptor pointer,
  // count (0..256), transform pointer, mode (0 immediate, 1/2 tiled workers).
  // Optional mode bit0x100 enables conservative whole-triangle occlusion with
  // workers1/2 only; 257/258 are valid, 256 is not. renderStat70=0x43550001 is
  // the optional capability guard. Order, cutout masking and depth ties stay
  // unchanged; summaries derive solely from fully covered actual depth cells.
  // Each descriptor is seven uint32 words: packed float xyzuv vertex pointer,
  // RGB332 texture pointer, optional mask pointer, width, height, flags,
  // vertex count (3 or 4). Sources remain owned by the caller and are consumed
  // completely before submit returns; no pointer is retained across calls.
  NARM_GRAPHICS_TEXTURED_LIST = 0x020b,

  NARM_GRAPHICS_STATS_ENABLE = 0x0300,
  NARM_GRAPHICS_STATS_READ = 0x0301
};

struct NativeContextV1;
struct NativeKeyboardApiV1;
struct NativeAudioApiV1;
struct NativeFileApiV1;
struct NativeVideoApiV1;

typedef int32_t (*NativeInvokeFn)(NativeContextV1 *ctx,
                                  uint32_t service_id,
                                  const uint32_t *args,
                                  uint32_t arg_words);
typedef void (*NativeYieldFn)(NativeContextV1 *ctx, uint32_t reason);
typedef void (*NativeExitFn)(NativeContextV1 *ctx, int32_t status);
typedef const void *(*NativeGetInterfaceFn)(NativeContextV1 *ctx,
                                            uint32_t interface_id,
                                            uint16_t interface_major,
                                            uint16_t minimum_minor);

struct NativeApiV1
{
  uint32_t struct_bytes;
  uint16_t api_major;
  uint16_t api_minor;
  uint32_t capabilities_low;
  uint32_t capabilities_high;
  NativeInvokeFn invoke;
  NativeYieldFn yield_fn;
  NativeExitFn exit_fn;
  NativeGetInterfaceFn get_interface;
};

// Stable key values, actions, and modifier flags are declared in
// keyboard_runtime.h. This ABI record deliberately contains only fixed-width
// fields so NARM programs do not depend on firmware compiler layout choices.
struct NativeKeyboardEventV1
{
  uint16_t key;
  uint8_t action;
  uint8_t modifiers;
  uint32_t character;
  uint16_t physical_code;
  uint16_t reserved;
};

typedef int32_t (*NativeKeyboardKeyDownFn)(NativeContextV1 *ctx,
                                           uint32_t key);
typedef int32_t (*NativeKeyboardPollEventFn)(NativeContextV1 *ctx,
                                             NativeKeyboardEventV1 *event);
typedef uint32_t (*NativeKeyboardDroppedEventsFn)(NativeContextV1 *ctx);
typedef int32_t (*NativeKeyboardPs2KeyDownFn)(NativeContextV1 *ctx,
                                              uint32_t scan_code,
                                              uint32_t extended);

struct NativeKeyboardApiV1
{
  uint32_t struct_bytes;
  uint16_t interface_major;
  uint16_t interface_minor;
  NativeKeyboardKeyDownFn key_down;
  NativeKeyboardPollEventFn poll_event;
  NativeKeyboardDroppedEventsFn dropped_events;
  // Added in keyboard interface 1.1 for source compatibility with legacy
  // programs. New programs should prefer stable HID key identifiers.
  NativeKeyboardPs2KeyDownFn ps2_key_down;
};

typedef uint32_t (*NativeAudioStatusFn)(NativeContextV1 *ctx);
typedef int32_t (*NativeAudioSubmitFn)(NativeContextV1 *ctx,
                                       uint32_t command,
                                       const uint32_t *args,
                                       uint32_t arg_words);
typedef int32_t (*NativeAudioVoiceActiveFn)(NativeContextV1 *ctx,
                                            uint32_t voice_channel);
typedef uint32_t (*NativeAudioCounterFn)(NativeContextV1 *ctx);

// Audio uses the firmware's existing eight-voice mixer. PCM pointers remain
// owned by the NARM process and are stopped before its data arena is released.
struct NativeAudioApiV1
{
  uint32_t struct_bytes;
  uint16_t interface_major;
  uint16_t interface_minor;
  NativeAudioStatusFn status;
  NativeAudioSubmitFn submit;
  NativeAudioVoiceActiveFn voice_active;
  NativeAudioCounterFn output_underflows;
  NativeAudioCounterFn stream_starvations;
};

typedef int32_t (*NativeFileSubmitFn)(NativeContextV1 *ctx,
                                      uint32_t command,
                                      const uint32_t *args,
                                      uint32_t arg_words);

struct NativeFileApiV1
{
  uint32_t struct_bytes;
  uint16_t interface_major;
  uint16_t interface_minor;
  NativeFileSubmitFn submit;
};

struct NativeVideoClearV1
{
  uint32_t color_index;
};

struct NativeVideoPixelV1
{
  int32_t x;
  int32_t y;
  uint32_t color_index;
};

struct NativeVideoRectV1
{
  int32_t x0;
  int32_t y0;
  int32_t x1;
  int32_t y1;
  uint32_t color_index;
};

struct NativeVideoLineV1
{
  int32_t x0;
  int32_t y0;
  int32_t x1;
  int32_t y1;
  uint32_t color_index;
};

typedef int32_t (*NativeVideoClearFn)(NativeContextV1 *ctx,
                                      const NativeVideoClearV1 *command);
typedef int32_t (*NativeVideoPixelFn)(NativeContextV1 *ctx,
                                      const NativeVideoPixelV1 *command);
typedef int32_t (*NativeVideoRectFn)(NativeContextV1 *ctx,
                                     const NativeVideoRectV1 *command);
typedef int32_t (*NativeVideoLineFn)(NativeContextV1 *ctx,
                                     const NativeVideoLineV1 *command);
typedef int32_t (*NativeVideoPresentFn)(NativeContextV1 *ctx);
typedef int32_t (*NativeVideoSubmitFn)(NativeContextV1 *ctx,
                                       uint32_t command,
                                       const uint32_t *args,
                                       uint32_t arg_words);

struct NativeVideoApiV1
{
  uint32_t struct_bytes;
  uint16_t interface_major;
  uint16_t interface_minor;
  NativeVideoClearFn clear;
  NativeVideoPixelFn pixel;
  NativeVideoRectFn fill_rect;
  NativeVideoLineFn line;
  NativeVideoPresentFn present;
  // Added in video interface 1.1. Older 1.0 programs see the unchanged prefix.
  NativeVideoSubmitFn submit;
};

struct NativeContextV1
{
  uint32_t struct_bytes;
  uint16_t abi_major;
  uint16_t abi_minor;
  const NativeApiV1 *api;
  void *process_handle;
  uint8_t *text_base;
  uint32_t text_bytes;
  uint8_t *data_base;
  uint32_t data_bytes;
  uint8_t *heap_base;
  uint32_t heap_bytes;
  volatile uint32_t *yield_requested;
  uint32_t granted_capabilities_low;
  uint32_t granted_capabilities_high;
  // Added in API 1.5. These validated interface pointers avoid performing an
  // indirect get_interface call for every draw and keyboard state query.
  const NativeKeyboardApiV1 *keyboard_api;
  const NativeVideoApiV1 *video_api;
  // Added in API 1.6.
  const NativeAudioApiV1 *audio_api;
  // Added in API 1.7.
  const NativeFileApiV1 *file_api;
};

#if defined(__cplusplus) && UINTPTR_MAX == 0xffffffffu
static_assert(offsetof(NativeApiV1, invoke) == 16, "NativeApiV1 ABI drift");
static_assert(offsetof(NativeApiV1, yield_fn) == 20, "NativeApiV1 ABI drift");
static_assert(offsetof(NativeApiV1, exit_fn) == 24, "NativeApiV1 ABI drift");
static_assert(offsetof(NativeApiV1, get_interface) == 28, "NativeApiV1 ABI drift");
static_assert(sizeof(NativeApiV1) == 32, "NativeApiV1 size drift");

static_assert(sizeof(NativeKeyboardEventV1) == 12,
              "NativeKeyboardEventV1 size drift");
static_assert(offsetof(NativeKeyboardApiV1, key_down) == 8,
              "NativeKeyboardApiV1 ABI drift");
static_assert(offsetof(NativeKeyboardApiV1, poll_event) == 12,
              "NativeKeyboardApiV1 ABI drift");
static_assert(offsetof(NativeKeyboardApiV1, dropped_events) == 16,
              "NativeKeyboardApiV1 ABI drift");
static_assert(offsetof(NativeKeyboardApiV1, ps2_key_down) == 20,
              "NativeKeyboardApiV1 ABI drift");
static_assert(sizeof(NativeKeyboardApiV1) == 24,
              "NativeKeyboardApiV1 size drift");

static_assert(offsetof(NativeAudioApiV1, status) == 8,
              "NativeAudioApiV1 ABI drift");
static_assert(offsetof(NativeAudioApiV1, submit) == 12,
              "NativeAudioApiV1 ABI drift");
static_assert(offsetof(NativeAudioApiV1, voice_active) == 16,
              "NativeAudioApiV1 ABI drift");
static_assert(sizeof(NativeAudioApiV1) == 28,
              "NativeAudioApiV1 size drift");
static_assert(offsetof(NativeFileApiV1, submit) == 8,
              "NativeFileApiV1 ABI drift");
static_assert(sizeof(NativeFileApiV1) == 12,
              "NativeFileApiV1 size drift");

static_assert(offsetof(NativeVideoApiV1, clear) == 8, "NativeVideoApiV1 ABI drift");
static_assert(offsetof(NativeVideoApiV1, present) == 24, "NativeVideoApiV1 ABI drift");
static_assert(offsetof(NativeVideoApiV1, submit) == 28, "NativeVideoApiV1 ABI drift");
static_assert(sizeof(NativeVideoApiV1) == 32, "NativeVideoApiV1 size drift");
static_assert(sizeof(NativeVideoRectV1) == 20, "NativeVideoRectV1 ABI drift");
static_assert(sizeof(NativeVideoLineV1) == 20, "NativeVideoLineV1 ABI drift");

static_assert(offsetof(NativeContextV1, api) == 8, "NativeContextV1 ABI drift");
static_assert(offsetof(NativeContextV1, process_handle) == 12, "NativeContextV1 ABI drift");
static_assert(offsetof(NativeContextV1, text_base) == 16, "NativeContextV1 ABI drift");
static_assert(offsetof(NativeContextV1, data_base) == 24, "NativeContextV1 ABI drift");
static_assert(offsetof(NativeContextV1, heap_base) == 32, "NativeContextV1 ABI drift");
static_assert(offsetof(NativeContextV1, yield_requested) == 40, "NativeContextV1 ABI drift");
static_assert(offsetof(NativeContextV1, keyboard_api) == 52, "NativeContextV1 ABI drift");
static_assert(offsetof(NativeContextV1, video_api) == 56, "NativeContextV1 ABI drift");
static_assert(offsetof(NativeContextV1, audio_api) == 60, "NativeContextV1 ABI drift");
static_assert(offsetof(NativeContextV1, file_api) == 64, "NativeContextV1 ABI drift");
static_assert(sizeof(NativeContextV1) == 68, "NativeContextV1 size drift");
#endif

#endif
