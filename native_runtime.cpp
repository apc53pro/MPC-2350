#include "native_runtime.h"
#include "native_scheduler_arch.h"

#include "SDmgr.h"
#include "globals.h"
#include "graphics_3d_runtime.h"
#include "graphics_runtime.h"
#include "keyboard_runtime.h"
#include "audio_runtime.h"
#include "file_io_runtime.h"
#include "system_overlay.h"
#include "terminal.h"
#include "ps2_keyboard_driver.h"
#include "usb_storage_mode.h"

#include <FatFS.h>
#include <hardware/regs/addressmap.h>
#include <hardware/irq.h>
#include <hardware/clocks.h>
#include <hardware/sync.h>
#include <hardware/exception.h>
#include <hardware/structs/m33.h>
#include <pico/multicore.h>
#include <pico/critical_section.h>
#include <pico/rand.h>
#include <pico/time.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace
{
static_assert((uint32_t)NARM_USB_STORAGE_DISABLED == (uint32_t)USB_STORAGE_DISABLED &&
              (uint32_t)NARM_USB_STORAGE_ENABLED == (uint32_t)USB_STORAGE_ENABLED &&
              (uint32_t)NARM_USB_STORAGE_HOST_OWNED == (uint32_t)USB_STORAGE_HOST_OWNED &&
              (uint32_t)NARM_USB_STORAGE_EJECTED == (uint32_t)USB_STORAGE_EJECTED &&
              (uint32_t)NARM_USB_STORAGE_ERROR == (uint32_t)USB_STORAGE_ERROR,
              "NARM and firmware USB storage states must remain identical");

static const uintptr_t NARM_XIP_CACHE_LINE_SIZE = 8u;
static const uintptr_t NARM_XIP_CACHE_INVALIDATE_BY_ADDRESS = 2u;
static const uintptr_t NARM_XIP_CACHE_CLEAN_BY_ADDRESS = 3u;

// The Arduino core does not link the SDK hardware_xip_cache library into
// sketches. This is the RP2350 maintenance-alias operation used by that SDK,
// kept in SRAM so the operation never depends on an XIP instruction fetch.
static void __attribute__((noinline, section(".time_critical.narm_xip_cache")))
narm_xip_cache_maintain(uintptr_t start_offset,
                        uintptr_t size_bytes,
                        uintptr_t operation)
{
  uintptr_t end = start_offset + size_bytes;
  for (uintptr_t offset = start_offset;
       offset < end;
       offset += NARM_XIP_CACHE_LINE_SIZE)
  {
    *(volatile uint8_t *)(XIP_MAINTENANCE_BASE + offset + operation) = 0;
  }
  __dsb();
  __isb();
}

struct NativeAllocation
{
  void *raw;
  uint8_t *aligned;
  uint32_t bytes;
  bool internal_sram;
};

struct NativeProcess
{
  uint32_t generation;
  uint32_t pid;
  NativeProcessState state;
  NativeRunMode selected_mode;
  NativeRunMode default_mode;
  NativeArchContext arch;
  uintptr_t entry;
  uint32_t wake_deadline_ms;
  uint32_t cpu_slices;
  uint32_t system_events[8];
  uint8_t system_event_read;
  uint8_t system_event_write;
  bool started;
  bool focused;
  bool close_requested;
  uint32_t close_deadline_ms;
  int32_t exit_status;
  bool exit_requested;
  NarmImageHeader image;
  NarmMemoryLayout layout;
  NativeAllocation text;
  NativeAllocation data;
  NativeAllocation stack;
  NativeContextV1 context;
  uint32_t audio_voice_mask;
  uint32_t audio_manual_pause_mask;
  uint32_t parent_pid;
  char launch_path[256];
  uint32_t launch_storage;
  bool launch_requested;
  bool spawn_requested;
  int32_t spawn_result;
  char name[64];
};

static const uint32_t NARM_ARENA_MAX_ALLOCATIONS = NARM_MAX_PROCESSES * 3u;

struct NativeArenaAllocation
{
  uintptr_t start;
  uint32_t bytes;
  bool used;
};

struct NativeArena
{
  uint8_t *base;
  uint32_t bytes;
  NativeArenaAllocation allocations[NARM_ARENA_MAX_ALLOCATIONS];
};

// These ordinary globals reserve NARM capacity at firmware link time while
// allowing the Arduino core to keep generating the linker script. Program
// text, data, heap and stacks are suballocated from these two private pools;
// the general firmware malloc/pmalloc heaps can never consume them.
PSRAM __attribute__((aligned(4096)))
uint8_t narm_psram_storage[NARM_PSRAM_ARENA_BYTES];

__attribute__((aligned(4096)))
uint8_t narm_sram_storage[NARM_SRAM_ARENA_BYTES];

NativeArena narm_psram_arena = {
  narm_psram_storage,
  NARM_PSRAM_ARENA_BYTES,
  {}
};

NativeArena narm_sram_arena = {
  narm_sram_storage,
  NARM_SRAM_ARENA_BYTES,
  {}
};

PSRAM NativeProcess processes[NARM_MAX_PROCESSES];
// Control words are shared between the keyboard core, the timer ISR, and
// generated native code. Keep their atomic read/modify/write operations in
// internal SRAM, independently of the larger process records in PSRAM.
uint32_t process_control_flags[NARM_MAX_PROCESSES] = {};
uint32_t *volatile scheduler_active_control_flags = nullptr;
bool process_table_initialized = false;
NarmStatus last_status = NARM_OK;
int32_t last_exit_status = 0;
NativeProcess *volatile active_process = nullptr;
NativeProcess *volatile focused_process = nullptr;
uint32_t scheduler_focused_pid = 0u;
NativeProcess *audio_voice_owners[AUDIO_VOICE_CHANNELS] = {};
bool terminal_has_focus = true;
uint32_t scheduler_cursor = 0;
uint8_t foreground_budget = 0;
repeating_timer_t scheduler_timer;
bool scheduler_timer_started = false;
bool scheduler_initialized = false;
bool scheduler_selftest_passed = false;
volatile uint32_t scheduler_timer_ticks = 0u;
volatile uint32_t scheduler_timer_core = 0xffffffffu;
uint32_t scheduler_timeslices = 0u;
uint32_t scheduler_yields = 0u;
uint32_t scheduler_commands_applied = 0u;
uint32_t scheduler_command_requests[3] = {};
critical_section_t scheduler_command_lock;
uint32_t scheduler_close_requested_pid = 0u;
uint32_t scheduler_mode_requested_pid = 0u;
volatile int32_t scheduler_doorbell = -1;
static const uint32_t SCHEDULER_COMMAND_FOCUS_NEXT = 1u << 0;
static const uint32_t SCHEDULER_COMMAND_TOGGLE_MODE = 1u << 1;
static const uint32_t SCHEDULER_COMMAND_CLOSE_FOCUSED = 1u << 2;
uint64_t terminal_background_due_us = 0u;
uint8_t native_graphics_frame_id = 1u;
bool native_graphics_depth_initialized = false;

extern "C"
{
volatile NativeArchContext *narm_arch_current_context = nullptr;
volatile uint32_t narm_arch_firmware_exc_return = 0xfffffff9u;
volatile uint32_t narm_arch_firmware_control = 0u;
volatile uint32_t narm_arch_switch_reason = NARM_ARCH_SWITCH_NONE;
volatile uint32_t narm_arch_requested_reason = NARM_ARCH_SWITCH_YIELD;
volatile int32_t narm_arch_exit_status = 0;
volatile uintptr_t narm_arch_text_begin = 0u;
volatile uintptr_t narm_arch_text_end = 0u;
// Core 1 publishes commands here. Core 0 applies them only after a native
// timeslice or a cooperative service-entry checkpoint has returned safely.
volatile uint32_t narm_arch_scheduler_commands = 0u;
}

uintptr_t align_up(uintptr_t value, uint32_t alignment)
{
  return (value + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
}

uint32_t native_arena_free_bytes(const NativeArena &arena)
{
  uint32_t used = 0;
  for (uint32_t i = 0; i < NARM_ARENA_MAX_ALLOCATIONS; ++i)
  {
    if (arena.allocations[i].used)
    {
      used += arena.allocations[i].bytes;
    }
  }
  return used <= arena.bytes ? arena.bytes - used : 0u;
}

uint32_t native_arena_total_free_bytes()
{
  return native_arena_free_bytes(narm_psram_arena) +
         native_arena_free_bytes(narm_sram_arena);
}

void *native_arena_allocate(NativeArena &arena,
                            uint32_t bytes,
                            uint32_t alignment)
{
  NativeArenaAllocation *record = nullptr;
  for (uint32_t i = 0; i < NARM_ARENA_MAX_ALLOCATIONS; ++i)
  {
    if (!arena.allocations[i].used)
    {
      record = &arena.allocations[i];
      break;
    }
  }
  if (!record)
  {
    return nullptr;
  }

  const uintptr_t arena_begin = (uintptr_t)arena.base;
  const uintptr_t arena_end = arena_begin + arena.bytes;
  uintptr_t candidate = align_up(arena_begin, alignment);

  while (candidate <= arena_end && bytes <= arena_end - candidate)
  {
    bool overlaps = false;
    uintptr_t next_candidate = candidate;
    const uintptr_t candidate_end = candidate + bytes;

    for (uint32_t i = 0; i < NARM_ARENA_MAX_ALLOCATIONS; ++i)
    {
      const NativeArenaAllocation &active = arena.allocations[i];
      if (!active.used)
      {
        continue;
      }

      const uintptr_t active_end = active.start + active.bytes;
      if (candidate < active_end && active.start < candidate_end)
      {
        overlaps = true;
        const uintptr_t after_active = align_up(active_end, alignment);
        if (after_active > next_candidate)
        {
          next_candidate = after_active;
        }
      }
    }

    if (!overlaps)
    {
      record->start = candidate;
      record->bytes = bytes;
      record->used = true;
      return (void *)candidate;
    }

    if (next_candidate <= candidate)
    {
      return nullptr;
    }
    candidate = next_candidate;
  }

  return nullptr;
}

bool native_arena_release(NativeArena &arena, void *address)
{
  if (!address)
  {
    return true;
  }

  const uintptr_t target = (uintptr_t)address;
  for (uint32_t i = 0; i < NARM_ARENA_MAX_ALLOCATIONS; ++i)
  {
    NativeArenaAllocation &record = arena.allocations[i];
    if (record.used && record.start == target)
    {
      record.start = 0;
      record.bytes = 0;
      record.used = false;
      return true;
    }
  }
  return false;
}

void release_allocation(NativeAllocation &allocation)
{
  if (allocation.raw)
  {
    NativeArena &arena = allocation.internal_sram
                           ? narm_sram_arena
                           : narm_psram_arena;
    native_arena_release(arena, allocation.raw);
  }
  allocation.raw = nullptr;
  allocation.aligned = nullptr;
  allocation.bytes = 0;
  allocation.internal_sram = false;
}

void release_process(NativeProcess &process)
{
  release_allocation(process.stack);
  release_allocation(process.data);
  release_allocation(process.text);
  __atomic_store_n(&process_control_flags[&process - processes], 0u,
                   __ATOMIC_RELEASE);
  uint32_t generation = process.generation;
  memset(&process, 0, sizeof(process));
  process.generation = generation;
  process.state = NATIVE_PROCESS_EMPTY;
}

bool aligned_native_allocate(uint32_t bytes,
                             uint32_t alignment,
                             bool prefer_internal_sram,
                             NativeAllocation &allocation)
{
  allocation.raw = nullptr;
  allocation.aligned = nullptr;
  allocation.bytes = 0;
  allocation.internal_sram = false;

  if (bytes == 0)
  {
    return true;
  }
  if (alignment < 8u)
  {
    alignment = 8u;
  }
  if ((alignment & (alignment - 1u)) != 0 || alignment > 4096u ||
      bytes > UINT32_MAX - (alignment - 1u))
  {
    return false;
  }

  void *raw = nullptr;
  if (prefer_internal_sram)
  {
    raw = native_arena_allocate(narm_sram_arena, bytes, alignment);
    allocation.internal_sram = raw != nullptr;
  }
  if (!raw)
  {
    raw = native_arena_allocate(narm_psram_arena, bytes, alignment);
    allocation.internal_sram = false;
  }
  if (!raw)
  {
    return false;
  }

  allocation.raw = raw;
  allocation.aligned = (uint8_t *)raw;
  allocation.bytes = bytes;
  return true;
}

uint32_t maximum_data_alignment(const NarmImageHeader &header)
{
  uint32_t result = 8u;
  if (header.rodata_alignment > result) result = header.rodata_alignment;
  if (header.data_alignment > result) result = header.data_alignment;
  if (header.bss_alignment > result) result = header.bss_alignment;
  return result;
}

NativeProcess *claim_process()
{
  if (!process_table_initialized)
  {
    memset(processes, 0, sizeof(processes));
    process_table_initialized = true;
  }

  for (uint32_t i = 0; i < NARM_MAX_PROCESSES; ++i)
  {
    if (processes[i].state == NATIVE_PROCESS_EMPTY)
    {
      __atomic_store_n(&process_control_flags[i], 0u, __ATOMIC_RELEASE);
      ++processes[i].generation;
      if (processes[i].generation == 0)
      {
        processes[i].generation = 1;
      }
      processes[i].pid = (processes[i].generation << 8) | (i + 1u);
      processes[i].selected_mode = NARM_RUN_MODE_AUTO;
      processes[i].default_mode = NARM_RUN_MODE_FOREGROUND;
      processes[i].state = NATIVE_PROCESS_LOADING;
      return &processes[i];
    }
  }
  return nullptr;
}

NativeRunMode effective_mode(const NativeProcess &process)
{
  return process.selected_mode == NARM_RUN_MODE_AUTO
           ? process.default_mode
           : process.selected_mode;
}

void update_process_audio_pause(NativeProcess &process)
{
  const bool paused = !process.focused &&
                      effective_mode(process) == NARM_RUN_MODE_FOREGROUND;
  for (uint8_t voice = 0; voice < AUDIO_VOICE_CHANNELS; ++voice)
  {
    if ((process.audio_voice_mask & (1u << voice)) != 0u &&
        audio_voice_owners[voice] == &process)
      audio_set_voice_paused(voice, paused ||
          (process.audio_manual_pause_mask & (1u << voice)) != 0u);
  }
}

void push_system_event(NativeProcess &process, NativeSystemEvent event)
{
  const uint8_t next = (uint8_t)((process.system_event_write + 1u) & 7u);
  if (next == process.system_event_read)
  {
    process.system_event_read = (uint8_t)((process.system_event_read + 1u) & 7u);
  }
  process.system_events[process.system_event_write] = (uint32_t)event;
  process.system_event_write = next;
}

uint32_t pop_system_event(NativeProcess &process)
{
  if (process.system_event_read == process.system_event_write)
    return NARM_EVENT_NONE;
  const uint32_t event = process.system_events[process.system_event_read];
  process.system_event_read = (uint8_t)((process.system_event_read + 1u) & 7u);
  return event;
}

bool service_supported(void *, uint32_t service_id, uint16_t minimum_version)
{
  if (minimum_version > 1)
  {
    return false;
  }
  switch (service_id)
  {
    case NARM_SERVICE_YIELD:
    case NARM_SERVICE_MILLIS:
    case NARM_SERVICE_MICROS:
    case NARM_SERVICE_RANDOM_U32:
    case NARM_SERVICE_SLEEP_MS:
    case NARM_SERVICE_CHAIN_EXEC:
    case NARM_SERVICE_SIN_F32:
    case NARM_SERVICE_COS_F32:
    case NARM_SERVICE_GET_PID:
    case NARM_SERVICE_HAS_FOCUS:
    case NARM_SERVICE_GET_RUN_MODE:
    case NARM_SERVICE_GET_EFFECTIVE_RUN_MODE:
    case NARM_SERVICE_POLL_SYSTEM_EVENT:
    case NARM_SERVICE_SPAWN_PROGRAM:
    case NARM_SERVICE_USB_STORAGE_ENABLE:
    case NARM_SERVICE_USB_STORAGE_DISABLE:
    case NARM_SERVICE_USB_STORAGE_STATUS:
    case NARM_INTERFACE_KEYBOARD:
    case NARM_INTERFACE_FILE_IO:
    case NARM_INTERFACE_AUDIO:
    case NARM_INTERFACE_VIDEO_2D:
      return true;
    default:
      return false;
  }
}

bool allocation_contains_bytes(const NativeAllocation &allocation,
                               const void *address,
                               uint32_t bytes,
                               uint32_t alignment)
{
  if (!allocation.aligned || !address || bytes == 0u ||
      alignment == 0u || (alignment & (alignment - 1u)) != 0u)
  {
    return false;
  }
  const uintptr_t begin = (uintptr_t)address;
  const uintptr_t end = begin + (uintptr_t)bytes;
  const uintptr_t allocation_begin = (uintptr_t)allocation.aligned;
  const uintptr_t allocation_end = allocation_begin + allocation.bytes;
  return (begin & (alignment - 1u)) == 0u && end >= begin &&
         allocation_end >= allocation_begin && begin >= allocation_begin &&
         end <= allocation_end;
}

bool valid_native_context(NativeContextV1 *context);

bool valid_service_arguments(NativeContextV1 *context,
                             const uint32_t *args,
                             uint32_t arg_words)
{
  if (!valid_native_context(context))
  {
    return false;
  }

  if (arg_words == 0) return true;

  if (arg_words > UINT32_MAX / sizeof(uint32_t)) return false;
  NativeProcess *process = (NativeProcess *)context->process_handle;
  const uint32_t bytes = arg_words * sizeof(uint32_t);
  return allocation_contains_bytes(process->data, args, bytes, alignof(uint32_t)) ||
         allocation_contains_bytes(process->stack, args, bytes, alignof(uint32_t));
}

bool valid_native_context(NativeContextV1 *context)
{
  if (!context || !context->process_handle)
  {
    return false;
  }
  NativeProcess *process = (NativeProcess *)context->process_handle;
  return &process->context == context;
}

void terminate_native(NativeContextV1 *context, int32_t status) __attribute__((noreturn));
void terminate_native(NativeContextV1 *context, int32_t status)
{
  NativeProcess *process = (NativeProcess *)context->process_handle;
  process->exit_status = status;
  process->exit_requested = true;
  narm_arch_abort(status);
}

void abort_if_stop_requested(NativeContextV1 *context, bool allow_yield = true)
{
  if (!valid_native_context(context) || !context->yield_requested)
  {
    return;
  }

  NativeProcess *process = (NativeProcess *)context->process_handle;
  uint32_t flags = __atomic_load_n(context->yield_requested, __ATOMIC_ACQUIRE);
  if ((flags & NARM_CONTROL_STOP_REQUESTED) != 0 ||
      (process->close_requested &&
       (int32_t)(millis() - process->close_deadline_ms) >= 0))
  {
    terminate_native(context, NARM_EXIT_USER_TERMINATED);
  }

  // This checkpoint runs before the ABI service acquires shared resources.
  // Never suspend a firmware service halfway through a filesystem or draw
  // operation merely because the keyboard core requested a focus change.
  if (allow_yield && (flags & NARM_CONTROL_YIELD_REQUESTED) != 0u)
  {
    __atomic_fetch_and(context->yield_requested,
                       ~((uint32_t)NARM_CONTROL_YIELD_REQUESTED),
                       __ATOMIC_ACQ_REL);
    narm_arch_yield(NARM_ARCH_SWITCH_YIELD);
    flags = __atomic_load_n(context->yield_requested, __ATOMIC_ACQUIRE);
    if ((flags & NARM_CONTROL_STOP_REQUESTED) != 0u ||
        (process->close_requested &&
         (int32_t)(millis() - process->close_deadline_ms) >= 0))
      terminate_native(context, NARM_EXIT_USER_TERMINATED);
  }

  audio_core0_poll_if_due();
}

bool has_capability(const NativeContextV1 *context, uint64_t capability)
{
  if (!context)
  {
    return false;
  }
  uint64_t granted = (uint64_t)context->granted_capabilities_low |
                     ((uint64_t)context->granted_capabilities_high << 32);
  return (granted & capability) == capability;
}

bool native_pointer_valid(NativeContextV1 *context,
                          const void *address,
                          uint32_t bytes,
                          uint32_t alignment = 4u)
{
  if (!valid_native_context(context) || bytes == 0u) return false;
  NativeProcess *process = (NativeProcess *)context->process_handle;
  return allocation_contains_bytes(process->data, address, bytes, alignment) ||
         allocation_contains_bytes(process->stack, address, bytes, alignment);
}

bool native_c_string_valid(NativeContextV1 *context,
                           const char *text,
                           uint32_t maximum_bytes)
{
  if (!valid_native_context(context) || !text || maximum_bytes == 0u)
    return false;
  NativeProcess *process = (NativeProcess *)context->process_handle;
  const NativeAllocation *allocations[] = {&process->data, &process->stack};
  const uintptr_t address = (uintptr_t)text;
  for (const NativeAllocation *allocation : allocations)
  {
    const uintptr_t begin = (uintptr_t)allocation->aligned;
    const uintptr_t end = begin + allocation->bytes;
    if (!allocation->aligned || end < begin || address < begin || address >= end)
      continue;
    uint32_t available = (uint32_t)(end - address);
    if (available > maximum_bytes) available = maximum_bytes;
    return memchr(text, 0, available) != nullptr;
  }
  return false;
}

bool native_owns_audio(NativeContextV1 *context)
{
  if (!valid_native_context(context) || !has_capability(context, NARM_CAP_AUDIO))
    return false;
  NativeProcess *process = (NativeProcess *)context->process_handle;
  return process->state == NATIVE_PROCESS_RUNNING &&
         __atomic_load_n(&active_process, __ATOMIC_ACQUIRE) == process;
}

bool native_owns_graphics(NativeContextV1 *context)
{
  if (!valid_native_context(context) ||
      !has_capability(context, NARM_CAP_FRAMEBUFFER_2D))
  {
    return false;
  }
  NativeProcess *process = (NativeProcess *)context->process_handle;
  return process->state == NATIVE_PROCESS_RUNNING &&
         __atomic_load_n(&active_process, __ATOMIC_ACQUIRE) == process &&
         __atomic_load_n(&focused_process, __ATOMIC_ACQUIRE) == process;
}

bool native_graphics_buffers(var *&color, var *&depth, var *&stamp)
{
  if (SYS_HALF_RES_BUFFER_PARTITION_INDEX < 0 ||
      SYS_HALF_RES_BUFFER_PARTITION_INDEX >= partition_amount ||
      SYS_Z_BUFFER_PARTITION_INDEX < 0 ||
      SYS_Z_BUFFER_PARTITION_INDEX >= partition_amount ||
      SYS_Z_STAMP_BUFFER_PARTITION_INDEX < 0 ||
      SYS_Z_STAMP_BUFFER_PARTITION_INDEX >= partition_amount)
  {
    return false;
  }
  const partition &color_partition = partitions[SYS_HALF_RES_BUFFER_PARTITION_INDEX];
  const partition &depth_partition = partitions[SYS_Z_BUFFER_PARTITION_INDEX];
  const partition &stamp_partition = partitions[SYS_Z_STAMP_BUFFER_PARTITION_INDEX];
  if (color_partition.partition_type == VACANT ||
      depth_partition.partition_type == VACANT ||
      stamp_partition.partition_type == VACANT ||
      color_partition.end_addr < color_partition.start_addr ||
      depth_partition.end_addr < depth_partition.start_addr ||
      stamp_partition.end_addr < stamp_partition.start_addr ||
      color_partition.end_addr - color_partition.start_addr + 1u < 19200u ||
      depth_partition.end_addr - depth_partition.start_addr + 1u < 76800u ||
      stamp_partition.end_addr - stamp_partition.start_addr + 1u < 19200u)
  {
    return false;
  }
  color = &memory[color_partition.start_addr];
  depth = &memory[depth_partition.start_addr];
  stamp = &memory[stamp_partition.start_addr];
  return true;
}

void native_graphics_initialize_depth(var *stamp)
{
  if (native_graphics_depth_initialized)
  {
    tri3d_set_tile_scratch_enabled(true);
    return;
  }
  tri3d_flush_solid_batch();
  for (uint32_t i = 0; i < 19200u; i++) stamp[i].value = 0u;
  native_graphics_frame_id = 1u;
  native_graphics_depth_initialized = true;
  tri3d_mark_depth_pristine(native_graphics_frame_id);
  tri3d_set_tile_scratch_enabled(true);
}

void native_graphics_begin_frame(var *color, var *stamp, uint8_t clear_color)
{
  native_graphics_initialize_depth(stamp);
  tri3d_flush_solid_batch();
  graphics_half_clear(color, clear_color);
  ++native_graphics_frame_id;
  if (native_graphics_frame_id == 0u)
  {
    native_graphics_frame_id = 1u;
    for (uint32_t i = 0; i < 19200u; i++) stamp[i].value = 0u;
  }
  tri3d_mark_depth_pristine(native_graphics_frame_id);
  tri3d_set_tile_scratch_enabled(true);
  tri3d_stats_reset();
}

// A generic render-pass boundary: retain the completed color image while
// giving following 3D draws a fresh depth generation. No program pointers or
// extra storage are retained, and optional profiling state is left untouched.
void native_graphics_clear_depth(var *stamp)
{
  native_graphics_initialize_depth(stamp);
  tri3d_flush_solid_batch();
  ++native_graphics_frame_id;
  if (native_graphics_frame_id == 0u)
  {
    native_graphics_frame_id = 1u;
    for (uint32_t i = 0; i < 19200u; i++) stamp[i].value = 0u;
  }
  tri3d_mark_depth_pristine(native_graphics_frame_id);
  tri3d_set_tile_scratch_enabled(true);
}

void native_graphics_release()
{
  tri3d_flush_solid_batch();
  tri3d_set_stats_enabled(0u);
  tri3d_stats_reset();
  tri3d_set_tile_scratch_enabled(false);
  if (!native_graphics_depth_initialized) return;
  var *color = nullptr;
  var *depth = nullptr;
  var *stamp = nullptr;
  if (native_graphics_buffers(color, depth, stamp))
  {
    for (uint32_t i = 0; i < 19200u; i++) stamp[i].value = 0u;
  }
  native_graphics_depth_initialized = false;
}

float native_word_float(uint32_t word)
{
  float value;
  memcpy(&value, &word, sizeof(value));
  return value;
}

bool native_checked_pixels(int32_t width,
                           int32_t height,
                           uint32_t pixels_per_word,
                           uint32_t &word_count)
{
  if (width <= 0 || height <= 0 || pixels_per_word == 0u) return false;
  const uint64_t pixels = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height;
  const uint64_t words = (pixels + pixels_per_word - 1u) / pixels_per_word;
  if (words == 0u || words > UINT32_MAX) return false;
  word_count = (uint32_t)words;
  return true;
}

bool native_checked_texture_rows(int32_t width,
                                 int32_t height,
                                 uint32_t &word_count)
{
  if (width <= 0 || height <= 0) return false;
  const uint64_t row_words = ((uint64_t)(uint32_t)width + 3u) / 4u;
  const uint64_t words = row_words * (uint64_t)(uint32_t)height;
  if (words == 0u || words > UINT32_MAX) return false;
  word_count = (uint32_t)words;
  return true;
}

int32_t api_invoke(NativeContextV1 *context,
                   uint32_t service_id,
                   const uint32_t *args,
                   uint32_t arg_words)
{
  if (!valid_service_arguments(context, args, arg_words))
  {
    return NARM_SERVICE_ERR_ARGUMENTS;
  }
  abort_if_stop_requested(context, service_id != NARM_SERVICE_YIELD);

  switch (service_id)
  {
    case NARM_SERVICE_YIELD:
      if (arg_words != 0) return NARM_SERVICE_ERR_ARGUMENTS;
      __atomic_fetch_and(context->yield_requested,
                         ~((uint32_t)NARM_CONTROL_YIELD_REQUESTED),
                         __ATOMIC_ACQ_REL);
      narm_arch_yield(NARM_ARCH_SWITCH_YIELD);
      return NARM_SERVICE_OK;
    case NARM_SERVICE_MILLIS:
      if (arg_words != 0) return NARM_SERVICE_ERR_ARGUMENTS;
      return (int32_t)millis();
    case NARM_SERVICE_MICROS:
      if (arg_words != 0) return NARM_SERVICE_ERR_ARGUMENTS;
      return (int32_t)micros();
    case NARM_SERVICE_RANDOM_U32:
      if (arg_words != 0) return NARM_SERVICE_ERR_ARGUMENTS;
      return (int32_t)get_rand_32();
    case NARM_SERVICE_SLEEP_MS:
      if (arg_words != 1 || args[0] > 60000u) return NARM_SERVICE_ERR_ARGUMENTS;
      ((NativeProcess *)context->process_handle)->wake_deadline_ms = millis() + args[0];
      ((NativeProcess *)context->process_handle)->state = NATIVE_PROCESS_WAITING;
      narm_arch_yield(NARM_ARCH_SWITCH_YIELD);
      abort_if_stop_requested(context);
      return NARM_SERVICE_OK;
    case NARM_SERVICE_CHAIN_EXEC:
    {
      if (arg_words != 2u || args[1] > 1u ||
          !has_capability(context, NARM_CAP_FILE_IO))
        return NARM_SERVICE_ERR_ARGUMENTS;
      const char *path = (const char *)(uintptr_t)args[0];
      NativeProcess *process = (NativeProcess *)context->process_handle;
      if (!native_c_string_valid(context, path, sizeof(process->launch_path)))
        return NARM_SERVICE_ERR_ARGUMENTS;
      size_t bytes = strnlen(path, sizeof(process->launch_path));
      if (bytes == 0u || bytes >= sizeof(process->launch_path))
        return NARM_SERVICE_ERR_ARGUMENTS;
      memcpy(process->launch_path, path, bytes + 1u);
      process->launch_storage = args[1];
      process->launch_requested = true;
      terminate_native(context, NARM_EXIT_CHAIN_EXEC);
    }
    case NARM_SERVICE_SPAWN_PROGRAM:
    {
      if (arg_words != 2u || args[1] > 1u)
        return NARM_SERVICE_ERR_ARGUMENTS;
      if (!has_capability(context, NARM_CAP_FILE_IO))
        return NARM_SERVICE_ERR_CAPABILITY;
      NativeProcess *process = (NativeProcess *)context->process_handle;
      const char *path = (const char *)(uintptr_t)args[0];
      if (!native_c_string_valid(context, path, sizeof(process->launch_path)))
        return NARM_SERVICE_ERR_ARGUMENTS;
      const size_t bytes = strnlen(path, sizeof(process->launch_path));
      if (bytes == 0u || bytes >= sizeof(process->launch_path))
        return NARM_SERVICE_ERR_ARGUMENTS;
      memcpy(process->launch_path, path, bytes + 1u);
      process->launch_storage = args[1];
      process->spawn_result = NARM_SERVICE_ERR_BUSY;
      process->spawn_requested = true;
      // The loader must run on the firmware stack, with no caller service
      // holding filesystem or graphics resources. The scheduler completes
      // this request after the context switch and preserves the parent.
      narm_arch_yield(NARM_ARCH_SWITCH_YIELD);
      abort_if_stop_requested(context);
      return process->spawn_result;
    }
    case NARM_SERVICE_USB_STORAGE_ENABLE:
    {
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      if (!has_capability(context, NARM_CAP_USB_STORAGE))
        return NARM_SERVICE_ERR_CAPABILITY;
      NativeProcess *process = (NativeProcess *)context->process_handle;
      if (__atomic_load_n(&focused_process, __ATOMIC_ACQUIRE) != process)
        return NARM_SERVICE_ERR_BUSY;
      const int32_t result = usb_storage_enable(process->pid);
      if (result == USB_STORAGE_RESULT_OK ||
          result == USB_STORAGE_RESULT_UNCHANGED) return result;
      return result == USB_STORAGE_RESULT_BUSY ||
             result == USB_STORAGE_RESULT_NOT_OWNER
               ? NARM_SERVICE_ERR_BUSY : NARM_SERVICE_ERR_UNKNOWN;
    }
    case NARM_SERVICE_USB_STORAGE_DISABLE:
    {
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      if (!has_capability(context, NARM_CAP_USB_STORAGE))
        return NARM_SERVICE_ERR_CAPABILITY;
      NativeProcess *process = (NativeProcess *)context->process_handle;
      const int32_t result = usb_storage_disable(process->pid);
      if (result == USB_STORAGE_RESULT_OK ||
          result == USB_STORAGE_RESULT_UNCHANGED) return result;
      return result == USB_STORAGE_RESULT_BUSY ||
             result == USB_STORAGE_RESULT_NOT_OWNER
               ? NARM_SERVICE_ERR_BUSY : NARM_SERVICE_ERR_UNKNOWN;
    }
    case NARM_SERVICE_USB_STORAGE_STATUS:
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      if (!has_capability(context, NARM_CAP_USB_STORAGE))
        return NARM_SERVICE_ERR_CAPABILITY;
      return (int32_t)usb_storage_status();
    case NARM_SERVICE_SIN_F32:
    case NARM_SERVICE_COS_F32:
    {
      if (arg_words != 1u) return NARM_SERVICE_ERR_ARGUMENTS;
      float input = 0.0f;
      memcpy(&input, &args[0], sizeof(input));
      float output = service_id == NARM_SERVICE_SIN_F32 ? sinf(input) : cosf(input);
      int32_t bits = 0;
      memcpy(&bits, &output, sizeof(bits));
      return bits;
    }
    case NARM_SERVICE_GET_PID:
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      return (int32_t)((NativeProcess *)context->process_handle)->pid;
    case NARM_SERVICE_HAS_FOCUS:
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      return __atomic_load_n(&focused_process, __ATOMIC_ACQUIRE) ==
             (NativeProcess *)context->process_handle;
    case NARM_SERVICE_GET_RUN_MODE:
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      return (int32_t)((NativeProcess *)context->process_handle)->selected_mode;
    case NARM_SERVICE_GET_EFFECTIVE_RUN_MODE:
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      return (int32_t)effective_mode(*(NativeProcess *)context->process_handle);
    case NARM_SERVICE_POLL_SYSTEM_EVENT:
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      return (int32_t)pop_system_event(*(NativeProcess *)context->process_handle);
    default:
      return NARM_SERVICE_ERR_UNKNOWN;
  }
}

int32_t video_clear(NativeContextV1 *context,
                    const NativeVideoClearV1 *command)
{
  if (!valid_service_arguments(context, (const uint32_t *)command,
                               sizeof(*command) / sizeof(uint32_t)) ||
      command->color_index > 15u)
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_FRAMEBUFFER_2D))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (!native_owns_graphics(context)) return NARM_SERVICE_ERR_BUSY;
  tri3d_flush_solid_batch();
  tri3d_set_tile_scratch_enabled(false);
  graphics_vga4_clear((uint8_t)command->color_index);
  return NARM_SERVICE_OK;
}

int32_t native_keyboard_key_down(NativeContextV1 *context, uint32_t key)
{
  if (!valid_native_context(context) || key > 0xffu)
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_KEYBOARD))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (__atomic_load_n(&focused_process, __ATOMIC_ACQUIRE) !=
      (NativeProcess *)context->process_handle) return 0;
  return keyboard_key_down((uint16_t)key) ? 1 : 0;
}

int32_t native_keyboard_ps2_key_down(NativeContextV1 *context,
                                     uint32_t scan_code,
                                     uint32_t extended)
{
  if (!valid_native_context(context) || scan_code > 0xffu || extended > 1u)
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_KEYBOARD))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (__atomic_load_n(&focused_process, __ATOMIC_ACQUIRE) !=
      (NativeProcess *)context->process_handle) return 0;
  return keyboard_ps2_key_down((uint8_t)scan_code, extended != 0u) ? 1 : 0;
}

int32_t native_keyboard_poll_event(NativeContextV1 *context,
                                   NativeKeyboardEventV1 *event)
{
  if (!valid_service_arguments(context, (const uint32_t *)event,
                               sizeof(*event) / sizeof(uint32_t)))
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_KEYBOARD))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (__atomic_load_n(&focused_process, __ATOMIC_ACQUIRE) !=
      (NativeProcess *)context->process_handle) return 0;

  KeyboardEvent source = {};
  if (!keyboard_poll_event(KEYBOARD_FOCUS_NARM, source)) return 0;
  event->key = source.key;
  event->action = source.action;
  event->modifiers = source.modifiers;
  event->character = source.character;
  event->physical_code = source.physical_code;
  event->reserved = 0;
  return 1;
}

uint32_t native_keyboard_dropped_events(NativeContextV1 *context)
{
  if (!valid_native_context(context)) return 0;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_KEYBOARD)) return 0;
  return keyboard_dropped_events();
}

const NativeKeyboardApiV1 native_keyboard_api = {
  sizeof(NativeKeyboardApiV1),
  1,
  1,
  native_keyboard_key_down,
  native_keyboard_poll_event,
  native_keyboard_dropped_events,
  native_keyboard_ps2_key_down
};

uint32_t native_audio_status(NativeContextV1 *context)
{
  if (!valid_native_context(context)) return 0u;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_AUDIO)) return 0u;
  uint32_t status = 0u;
  if (audio_runtime_ready()) status |= NARM_AUDIO_RUNTIME_READY;
  if (audio_output_ready()) status |= NARM_AUDIO_OUTPUT_READY;
  return status;
}

int32_t native_audio_submit(NativeContextV1 *context,
                            uint32_t command,
                            const uint32_t *args,
                            uint32_t arg_words)
{
  if (!valid_service_arguments(context, args, arg_words))
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_AUDIO))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (!native_owns_audio(context)) return NARM_SERVICE_ERR_BUSY;

  NativeProcess *process = (NativeProcess *)context->process_handle;
  auto claim_voice = [&](uint32_t voice) -> bool {
    NativeProcess *owner = audio_voice_owners[voice];
    if (owner && owner != process &&
        (!audio_command_queue_idle() || voice_channel_active((uint8_t)voice)))
      return false;
    if (owner && owner != process)
      owner->audio_voice_mask &= ~(1u << voice);
    audio_voice_owners[voice] = process;
    process->audio_voice_mask |= 1u << voice;
    process->audio_manual_pause_mask &= ~(1u << voice);
    audio_set_voice_paused((uint8_t)voice, false);
    return true;
  };
  switch (command)
  {
    case NARM_AUDIO_PLAY_WAV:
    case NARM_AUDIO_PLAY_WAV_FROM:
    {
      const bool from_frame = command == NARM_AUDIO_PLAY_WAV_FROM;
      if (arg_words != (from_frame ? 3u : 2u) ||
          args[0] >= AUDIO_VOICE_CHANNELS)
        return NARM_SERVICE_ERR_ARGUMENTS;
      const char *path = (const char *)(uintptr_t)args[1];
      if (!native_c_string_valid(context, path, 256u) || path[0] == 0)
        return NARM_SERVICE_ERR_ARGUMENTS;
      const uint32_t start_frame = from_frame ? args[2] : 0u;
      if (!claim_voice(args[0])) return NARM_SERVICE_ERR_BUSY;
      if (!play_sound_SD_from_frame((uint8_t)args[0], String(path),
                                    start_frame))
      {
        // A failed replacement can leave the previous PCM voice alive until
        // Core 1 sees a stop/invalidation. Retain ownership so exit cleanup
        // cannot release caller-owned samples while the mixer references them.
        return NARM_SERVICE_ERR_BUSY;
      }
      return NARM_SERVICE_OK;
    }
    case NARM_AUDIO_PLAY_PCM:
    case NARM_AUDIO_PLAY_PCM_FROM:
    {
      const bool from_frame = command == NARM_AUDIO_PLAY_PCM_FROM;
      if (arg_words != (from_frame ? 6u : 5u) ||
          args[0] >= AUDIO_VOICE_CHANNELS)
        return NARM_SERVICE_ERR_ARGUMENTS;
      const uint32_t frame_count = args[2];
      const uint32_t start_frame = from_frame ? args[3] : 0u;
      const uint32_t sample_rate = from_frame ? args[4] : args[3];
      const uint32_t channel_count = from_frame ? args[5] : args[4];
      if (frame_count == 0u || start_frame >= frame_count ||
          (channel_count != 1u && channel_count != 2u) ||
          sample_rate < 8000u || sample_rate > 192000u)
        return NARM_SERVICE_ERR_ARGUMENTS;
      const uint64_t sample_count = (uint64_t)frame_count * channel_count;
      const uint64_t byte_count = sample_count * sizeof(int16_t);
      if (byte_count == 0u || byte_count > UINT32_MAX)
        return NARM_SERVICE_ERR_ARGUMENTS;
      const int16_t *samples = (const int16_t *)(uintptr_t)args[1];
      if (!native_pointer_valid(context, samples, (uint32_t)byte_count,
                                alignof(int16_t)))
        return NARM_SERVICE_ERR_ARGUMENTS;
      if (!claim_voice(args[0])) return NARM_SERVICE_ERR_BUSY;
      if (!play_sound_array_from_frame((uint8_t)args[0], samples, frame_count,
                                       start_frame, sample_rate,
                                       (uint8_t)channel_count))
      {
        // Keep lifecycle responsibility for an older voice on this channel.
        return NARM_SERVICE_ERR_BUSY;
      }
      return NARM_SERVICE_OK;
    }
    case NARM_AUDIO_STOP_VOICE:
      if (arg_words != 1u || args[0] >= AUDIO_VOICE_CHANNELS)
        return NARM_SERVICE_ERR_ARGUMENTS;
      if (audio_voice_owners[args[0]] != process)
        return NARM_SERVICE_ERR_BUSY;
      return stop_voice_channel((uint8_t)args[0])
               ? NARM_SERVICE_OK : NARM_SERVICE_ERR_BUSY;
    case NARM_AUDIO_STOP_ALL:
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      for (uint8_t voice = 0; voice < AUDIO_VOICE_CHANNELS; ++voice)
        if (audio_voice_owners[voice] == process) stop_voice_channel(voice);
      return NARM_SERVICE_OK;
    case NARM_AUDIO_PAUSE:
      if (arg_words != 2u || args[0] >= AUDIO_VOICE_CHANNELS || args[1] > 1u)
        return NARM_SERVICE_ERR_ARGUMENTS;
      if (audio_voice_owners[args[0]] != process)
        return NARM_SERVICE_ERR_BUSY;
      if (args[1]) process->audio_manual_pause_mask |= 1u << args[0];
      else process->audio_manual_pause_mask &= ~(1u << args[0]);
      update_process_audio_pause(*process);
      return NARM_SERVICE_OK;
    case NARM_AUDIO_POSITION:
    {
      if (arg_words != 5u || args[0] >= AUDIO_VOICE_CHANNELS)
        return NARM_SERVICE_ERR_ARGUMENTS;
      uint32_t *outputs[4];
      for (uint32_t i = 0; i < 4u; ++i)
      {
        outputs[i] = (uint32_t *)(uintptr_t)args[i + 1u];
        if (!native_pointer_valid(context, outputs[i], sizeof(uint32_t)))
          return NARM_SERVICE_ERR_ARGUMENTS;
      }
      if (audio_voice_owners[args[0]] != process)
        return NARM_SERVICE_ERR_BUSY;
      // A successful play command may still be waiting for Core 1. Avoid
      // reporting the previous track's metadata while that command is queued.
      if (!audio_command_queue_idle()) return NARM_SERVICE_ERR_BUSY;
      AudioVoicePosition position{};
      if (!audio_get_voice_position((uint8_t)args[0], position))
        return NARM_SERVICE_ERR_BUSY;
      *outputs[0] = position.current_frame;
      *outputs[1] = position.total_frames;
      *outputs[2] = position.sample_rate;
      *outputs[3] = (position.active ? NARM_AUDIO_POSITION_ACTIVE : 0u) |
                    (position.paused ? NARM_AUDIO_POSITION_PAUSED : 0u);
      return NARM_SERVICE_OK;
    }
    default:
      return NARM_SERVICE_ERR_UNKNOWN;
  }
}

int32_t native_audio_voice_active(NativeContextV1 *context,
                                  uint32_t voice_channel)
{
  if (!valid_native_context(context) || voice_channel >= AUDIO_VOICE_CHANNELS)
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_AUDIO))
    return NARM_SERVICE_ERR_CAPABILITY;
  NativeProcess *process = (NativeProcess *)context->process_handle;
  if (audio_voice_owners[voice_channel] != process) return 0;
  return voice_channel_active((uint8_t)voice_channel) ? 1 : 0;
}

uint32_t native_audio_output_underflows(NativeContextV1 *context)
{
  if (!valid_native_context(context)) return 0u;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_AUDIO)) return 0u;
  return audio_output_underflow_count();
}

uint32_t native_audio_stream_starvations(NativeContextV1 *context)
{
  if (!valid_native_context(context)) return 0u;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_AUDIO)) return 0u;
  return audio_stream_starvation_count();
}

const NativeAudioApiV1 native_audio_api = {
  sizeof(NativeAudioApiV1),
  1,
  1,
  native_audio_status,
  native_audio_submit,
  native_audio_voice_active,
  native_audio_output_underflows,
  native_audio_stream_starvations
};

int32_t native_file_submit(NativeContextV1 *context,
                           uint32_t command,
                           const uint32_t *args,
                           uint32_t arg_words)
{
  if (!valid_service_arguments(context, args, arg_words))
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_FILE_IO))
    return NARM_SERVICE_ERR_CAPABILITY;

  auto path_argument = [&](uint32_t word) -> const char * {
    if (word >= arg_words) return nullptr;
    const char *path = (const char *)(uintptr_t)args[word];
    return native_c_string_valid(context, path, 256u) ? path : nullptr;
  };

  switch (command)
  {
    case NARM_FILE_MOUNT:
      if (arg_words != 1u) return NARM_SERVICE_ERR_ARGUMENTS;
      return file_io_mount(args[0]) ? 1 : 0;
    case NARM_FILE_EXISTS:
    case NARM_FILE_CREATE:
    case NARM_FILE_REMOVE:
    case NARM_FILE_MKDIR:
    case NARM_FILE_RMDIR:
    {
      if (arg_words != 2u) return NARM_SERVICE_ERR_ARGUMENTS;
      const char *path = path_argument(0u);
      if (!path) return NARM_SERVICE_ERR_ARGUMENTS;
      if (command == NARM_FILE_EXISTS) return file_io_exists(args[1], path) ? 1 : 0;
      if (command == NARM_FILE_CREATE) return file_io_create(args[1], path) ? 1 : 0;
      if (command == NARM_FILE_REMOVE) return file_io_remove(args[1], path) ? 1 : 0;
      if (command == NARM_FILE_MKDIR) return file_io_mkdir(args[1], path) ? 1 : 0;
      return file_io_rmdir(args[1], path) ? 1 : 0;
    }
    case NARM_FILE_RENAME:
    {
      if (arg_words != 3u) return NARM_SERVICE_ERR_ARGUMENTS;
      const char *from = path_argument(0u);
      const char *to = path_argument(1u);
      if (!from || !to) return NARM_SERVICE_ERR_ARGUMENTS;
      return file_io_rename(args[2], from, to) ? 1 : 0;
    }
    case NARM_FILE_SIZE:
    {
      if (arg_words != 3u) return NARM_SERVICE_ERR_ARGUMENTS;
      const char *path = path_argument(0u);
      uint32_t *size_out = (uint32_t *)(uintptr_t)args[2];
      if (!path || !native_pointer_valid(context, size_out, sizeof(*size_out)))
        return NARM_SERVICE_ERR_ARGUMENTS;
      uint32_t size = 0u;
      const int32_t result = file_io_size(args[1], path, size);
      if (result == 0) *size_out = size;
      return result;
    }
    case NARM_FILE_READ_AT:
    case NARM_FILE_WRITE_AT:
    {
      if (arg_words != 5u) return NARM_SERVICE_ERR_ARGUMENTS;
      const char *path = path_argument(0u);
      void *buffer = (void *)(uintptr_t)args[1];
      if (!path || (args[3] != 0u &&
                    !native_pointer_valid(context, buffer, args[3], 1u)))
        return NARM_SERVICE_ERR_ARGUMENTS;
      if (command == NARM_FILE_READ_AT)
        return file_io_read_at(args[4], path, args[2], buffer, args[3]);
      return file_io_write_at(args[4], path, args[2], buffer, args[3]);
    }
    case NARM_FILE_DIR_ENTRY:
    {
      if (arg_words != 7u || args[3] > 4096u)
        return NARM_SERVICE_ERR_ARGUMENTS;
      const char *path = path_argument(0u);
      char *name = (char *)(uintptr_t)args[2];
      uint32_t *name_length = (uint32_t *)(uintptr_t)args[4];
      uint32_t *type = (uint32_t *)(uintptr_t)args[5];
      if (!path || (args[3] != 0u &&
                    !native_pointer_valid(context, name, args[3], 1u)) ||
          !native_pointer_valid(context, name_length, sizeof(*name_length)) ||
          !native_pointer_valid(context, type, sizeof(*type)))
        return NARM_SERVICE_ERR_ARGUMENTS;
      uint32_t length_value = 0u;
      uint32_t type_value = 0u;
      const int32_t result = file_io_dir_entry(args[6], path, args[1], name,
                                               args[3], length_value, type_value);
      *name_length = length_value;
      *type = type_value;
      return result;
    }
    default:
      return NARM_SERVICE_ERR_UNKNOWN;
  }
}

const NativeFileApiV1 native_file_api = {
  sizeof(NativeFileApiV1),
  1,
  0,
  native_file_submit
};

void native_audio_release(NativeProcess &process)
{
  uint32_t pending = process.audio_voice_mask;
  if (pending == 0u) return;
  const uint32_t deadline = millis() + 250u;
  while (pending != 0u && (int32_t)(deadline - millis()) > 0)
  {
    for (uint8_t voice = 0; voice < AUDIO_VOICE_CHANNELS; ++voice)
    {
      const uint32_t bit = 1u << voice;
      if ((pending & bit) != 0u)
      {
        if (audio_voice_owners[voice] != &process)
        {
          pending &= ~bit;
          continue;
        }
        audio_set_voice_paused(voice, false);
        if (stop_voice_channel(voice)) pending &= ~bit;
      }
    }
    audio_core0_poll_if_due();
    delay(1);
  }
  while ((int32_t)(deadline - millis()) > 0)
  {
    bool active = false;
    for (uint8_t voice = 0; voice < AUDIO_VOICE_CHANNELS; ++voice)
    {
      if ((process.audio_voice_mask & (1u << voice)) != 0u &&
          voice_channel_active(voice))
      {
        active = true;
        break;
      }
    }
    if (audio_command_queue_idle() && !active) break;
    audio_core0_poll_if_due();
    delay(1);
  }
  for (uint8_t voice = 0; voice < AUDIO_VOICE_CHANNELS; ++voice)
  {
    if (audio_voice_owners[voice] == &process)
    {
      audio_set_voice_paused(voice, false);
      audio_voice_owners[voice] = nullptr;
    }
  }
  process.audio_voice_mask = 0u;
}

int32_t video_pixel(NativeContextV1 *context,
                    const NativeVideoPixelV1 *command)
{
  if (!valid_service_arguments(context, (const uint32_t *)command,
                               sizeof(*command) / sizeof(uint32_t)) ||
      command->color_index > 15u)
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_FRAMEBUFFER_2D))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (!native_owns_graphics(context)) return NARM_SERVICE_ERR_BUSY;
  tri3d_flush_solid_batch();
  tri3d_set_tile_scratch_enabled(false);
  graphics_vga4_pixel(command->x, command->y, (uint8_t)command->color_index);
  return NARM_SERVICE_OK;
}

int32_t video_fill_rect(NativeContextV1 *context,
                        const NativeVideoRectV1 *command)
{
  if (!valid_service_arguments(context, (const uint32_t *)command,
                               sizeof(*command) / sizeof(uint32_t)) ||
      command->color_index > 15u)
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_FRAMEBUFFER_2D))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (!native_owns_graphics(context)) return NARM_SERVICE_ERR_BUSY;
  tri3d_flush_solid_batch();
  tri3d_set_tile_scratch_enabled(false);
  graphics_vga4_fill_rect(command->x0, command->y0, command->x1, command->y1,
                          (uint8_t)command->color_index);
  return NARM_SERVICE_OK;
}

int32_t video_line(NativeContextV1 *context,
                   const NativeVideoLineV1 *command)
{
  if (!valid_service_arguments(context, (const uint32_t *)command,
                               sizeof(*command) / sizeof(uint32_t)) ||
      command->color_index > 15u)
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_FRAMEBUFFER_2D))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (!native_owns_graphics(context)) return NARM_SERVICE_ERR_BUSY;
  tri3d_flush_solid_batch();
  tri3d_set_tile_scratch_enabled(false);
  graphics_vga4_line(command->x0, command->y0, command->x1, command->y1,
                     (uint8_t)command->color_index);
  return NARM_SERVICE_OK;
}

int32_t video_present(NativeContextV1 *context)
{
  if (!valid_native_context(context))
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_FRAMEBUFFER_2D))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (!native_owns_graphics(context)) return NARM_SERVICE_ERR_BUSY;
  tri3d_flush_solid_batch();
  tri3d_set_tile_scratch_enabled(false);
  graphics_vga4_present();
  return NARM_SERVICE_OK;
}

int32_t native_draw_projected_command(NativeContextV1 *context,
                                      var *color,
                                      var *depth,
                                      var *stamp,
                                      const uint32_t *args,
                                      uint32_t arg_words,
                                      uint8_t vertex_count,
                                      bool gouraud,
                                      bool textured,
                                      bool tiled,
                                      bool masked)
{
  const uint32_t stride = textured ? 5u : (gouraud ? 4u : 3u);
  const uint32_t tail_words = textured ? (masked ? 6u : 5u)
                                         : (gouraud ? 1u : 2u);
  if (arg_words != (uint32_t)vertex_count * stride + tail_words)
    return NARM_SERVICE_ERR_ARGUMENTS;

  Graphics3dVertex vertices[4] = {};
  for (uint8_t i = 0; i < vertex_count; i++)
  {
    const uint32_t base = (uint32_t)i * stride;
    vertices[i].x = native_word_float(args[base + 0u]);
    vertices[i].y = native_word_float(args[base + 1u]);
    vertices[i].z = native_word_float(args[base + 2u]);
    if (textured)
    {
      vertices[i].u = native_word_float(args[base + 3u]);
      vertices[i].v = native_word_float(args[base + 4u]);
    }
    else if (gouraud)
    {
      vertices[i].color = args[base + 3u];
    }
  }

  const uint32_t tail = (uint32_t)vertex_count * stride;
  uint32_t flat_color = 0u;
  const Graphics3dTransform *transform = nullptr;
  const var *texture = nullptr;
  const var *mask = nullptr;
  int32_t texture_width = 0;
  int32_t texture_height = 0;
  uint32_t flags = 0u;

  if (textured)
  {
    texture = (const var *)(uintptr_t)args[tail + 0u];
    texture_width = (int32_t)args[tail + 1u];
    texture_height = (int32_t)args[tail + 2u];
    flags = args[tail + 3u];
    transform = (const Graphics3dTransform *)(uintptr_t)args[tail + 4u];
    if (masked) mask = (const var *)(uintptr_t)args[tail + 5u];

    uint32_t texture_word_count = 0;
    uint32_t mask_word_count = 0;
    if (!native_checked_pixels(texture_width, texture_height, 4u, texture_word_count) ||
        texture_word_count > UINT32_MAX / sizeof(var) ||
        !native_pointer_valid(context, texture, texture_word_count * sizeof(var)))
      return NARM_SERVICE_ERR_ARGUMENTS;
    if (masked &&
        (!native_checked_pixels(texture_width, texture_height, 32u, mask_word_count) ||
         mask_word_count > UINT32_MAX / sizeof(var) ||
         !native_pointer_valid(context, mask, mask_word_count * sizeof(var))))
      return NARM_SERVICE_ERR_ARGUMENTS;
  }
  else
  {
    flat_color = gouraud ? 0u : args[tail + 0u];
    transform = (const Graphics3dTransform *)(uintptr_t)
                  args[tail + (gouraud ? 0u : 1u)];
  }

  if (!native_pointer_valid(context, transform, sizeof(Graphics3dTransform)))
    return NARM_SERVICE_ERR_ARGUMENTS;
  native_graphics_initialize_depth(stamp);
  return tri3d_draw_projected(color, depth, stamp, native_graphics_frame_id,
                              vertices, vertex_count, transform, flat_color,
                              gouraud, texture, texture_width, texture_height,
                              flags, tiled, mask)
           ? NARM_SERVICE_OK : NARM_SERVICE_ERR_ARGUMENTS;
}

int32_t native_draw_terrain_command(NativeContextV1 *context,
                                    var *color,
                                    var *depth,
                                    var *stamp,
                                    const uint32_t *args,
                                    uint32_t arg_words)
{
  if (arg_words != 10u) return NARM_SERVICE_ERR_ARGUMENTS;
  int32_t mesh_index = (int32_t)args[0];
  const uint32_t *p0_values = (const uint32_t *)(uintptr_t)args[1];
  const uint32_t *p1_values = (const uint32_t *)(uintptr_t)args[2];
  const int32_t *next_values = (const int32_t *)(uintptr_t)args[3];
  const uint32_t mesh_limit = args[4];
  const float *config = (const float *)(uintptr_t)args[5];
  const uint32_t *top_textures = (const uint32_t *)(uintptr_t)args[6];
  const uint32_t *bottom_textures = (const uint32_t *)(uintptr_t)args[7];
  const uint32_t *side_textures = (const uint32_t *)(uintptr_t)args[8];
  const Graphics3dTransform *transform = (const Graphics3dTransform *)(uintptr_t)args[9];
  if (mesh_limit == 0u || mesh_limit > 65536u ||
      mesh_limit > UINT32_MAX / sizeof(uint32_t) ||
      !native_pointer_valid(context, p0_values, mesh_limit * sizeof(uint32_t)) ||
      !native_pointer_valid(context, p1_values, mesh_limit * sizeof(uint32_t)) ||
      !native_pointer_valid(context, next_values, mesh_limit * sizeof(int32_t)) ||
      !native_pointer_valid(context, config, 6u * sizeof(float)) ||
      !native_pointer_valid(context, top_textures, 16u * sizeof(uint32_t)) ||
      !native_pointer_valid(context, bottom_textures, 16u * sizeof(uint32_t)) ||
      !native_pointer_valid(context, side_textures, 16u * sizeof(uint32_t)) ||
      !native_pointer_valid(context, transform, sizeof(Graphics3dTransform)))
  {
    return NARM_SERVICE_ERR_ARGUMENTS;
  }

  const float x_offset = config[0];
  const float z_offset = config[1];
  const float scale = config[2];
  const float camera_x = config[3];
  const float camera_y = config[4];
  const float camera_z = config[5];
  if (!(scale == scale) || scale <= 0.0f) return NARM_SERVICE_ERR_ARGUMENTS;
  native_graphics_initialize_depth(stamp);

  uint32_t guard = 0u;
  while (mesh_index >= 0 && (uint32_t)mesh_index < mesh_limit && guard < mesh_limit)
  {
    // The enclosing video service already owns the shared graphics state.
    // Keep this responsiveness check, but do not suspend halfway through a
    // terrain command and let a different focused process reuse its buffers.
    if ((guard & 63u) == 0u) abort_if_stop_requested(context, false);
    const uint32_t p0 = p0_values[(uint32_t)mesh_index];
    const uint32_t p1 = p1_values[(uint32_t)mesh_index];
    const int32_t next_index = next_values[(uint32_t)mesh_index];
    const uint32_t face = (p0 >> 20) & 63u;
    const uint32_t block = (p0 >> 26) & 15u;
    const float x0 = (float)(p0 & 127u) + x_offset;
    const float z0 = (float)((p0 >> 7) & 127u) + z_offset;
    const float y0 = (float)((p0 >> 14) & 63u);
    const float x1 = (float)((p1 & 127u) + 1u) + x_offset;
    const float z1 = (float)(((p1 >> 7) & 127u) + 1u) + z_offset;
    const float y1 = (float)(((p1 >> 14) & 63u) + 1u);

    bool visible = true;
    if (face == 1u) visible = camera_y > y1;
    else if (face == 2u) visible = camera_y < y0;
    else if (face == 4u) visible = camera_z < z0;
    else if (face == 8u) visible = camera_z > z1;
    else if (face == 16u) visible = camera_x < x0;
    else if (face == 32u) visible = camera_x > x1;
    else visible = false;

    if (visible)
    {
      Graphics3dVertex vertices[4] = {};
      const uint32_t *texture_table = side_textures;
      if (face == 1u)
      {
        const float u = x1 - x0, v = z1 - z0;
        vertices[0]={x0,y1,z0,0,0,0}; vertices[1]={x1,y1,z0,u,0,0};
        vertices[2]={x1,y1,z1,u,v,0}; vertices[3]={x0,y1,z1,0,v,0};
        texture_table = top_textures;
      }
      else if (face == 2u)
      {
        const float u = x1 - x0, v = z1 - z0;
        vertices[0]={x0,y0,z1,0,0,0}; vertices[1]={x1,y0,z1,u,0,0};
        vertices[2]={x1,y0,z0,u,v,0}; vertices[3]={x0,y0,z0,0,v,0};
        texture_table = bottom_textures;
      }
      else if (face == 4u)
      {
        const float u = x1 - x0, v = y1 - y0;
        vertices[0]={x1,y0,z0,0,v,0}; vertices[1]={x0,y0,z0,u,v,0};
        vertices[2]={x0,y1,z0,u,0,0}; vertices[3]={x1,y1,z0,0,0,0};
      }
      else if (face == 8u)
      {
        const float u = x1 - x0, v = y1 - y0;
        vertices[0]={x0,y0,z1,0,v,0}; vertices[1]={x1,y0,z1,u,v,0};
        vertices[2]={x1,y1,z1,u,0,0}; vertices[3]={x0,y1,z1,0,0,0};
      }
      else if (face == 16u)
      {
        const float u = z1 - z0, v = y1 - y0;
        vertices[0]={x0,y0,z0,0,v,0}; vertices[1]={x0,y0,z1,u,v,0};
        vertices[2]={x0,y1,z1,u,0,0}; vertices[3]={x0,y1,z0,0,0,0};
      }
      else
      {
        const float u = z1 - z0, v = y1 - y0;
        vertices[0]={x1,y0,z1,0,v,0}; vertices[1]={x1,y0,z0,u,v,0};
        vertices[2]={x1,y1,z0,u,0,0}; vertices[3]={x1,y1,z1,0,0,0};
      }
      for (uint8_t i = 0; i < 4u; i++)
      {
        vertices[i].x *= scale; vertices[i].y *= scale; vertices[i].z *= scale;
      }
      const var *texture = (const var *)(uintptr_t)texture_table[block];
      if (!native_pointer_valid(context, texture, 16u * 16u))
        return NARM_SERVICE_ERR_ARGUMENTS;
      if (!tri3d_draw_projected(color, depth, stamp, native_graphics_frame_id,
                                vertices, 4u, transform, 0u, false, texture,
                                16, 16, 3u, true, nullptr))
        return NARM_SERVICE_ERR_ARGUMENTS;
    }
    mesh_index = next_index;
    ++guard;
  }
  return NARM_SERVICE_OK;
}

// A list is consumed synchronously within one video service. Validate every
// source before beginning a queue; caller-owned textures may be overwritten
// as soon as this service returns. A failed draw still closes the queue.
int32_t native_draw_textured_list(NativeContextV1 *context,
                                  var *color,
                                  var *depth,
                                  var *stamp,
                                  const uint32_t *args,
                                  uint32_t arg_words)
{
  // Only diagnostics rely on unwinding: native termination is non-unwinding.
  struct StageScope
  {
    bool enabled, validating;
    uint32_t started, phase, flush_before;
    StageScope() : enabled(tri3d_stage_profile_enabled()), validating(true),
                   started(0u), phase(0u), flush_before(0u)
    {
      if (enabled) { started = micros(); phase = started; tri3d_stage_profile_add(48u, 1u); }
    }
    void validation_done()
    {
      if (!enabled) return;
      const uint32_t now = micros();
      tri3d_stage_profile_add(36u, now - phase);
      validating = false; phase = now;
      flush_before = tri3d_stats_read(38u);
    }
    void setup_done()
    {
      if (!enabled) return;
      const uint32_t elapsed = micros() - phase;
      const uint32_t nested_flush = tri3d_stats_read(38u) - flush_before;
      tri3d_stage_profile_add(37u, elapsed >= nested_flush ? elapsed - nested_flush : 0u);
    }
    ~StageScope()
    {
      if (!enabled) return;
      const uint32_t now = micros();
      if (validating) tri3d_stage_profile_add(36u, now - phase);
      tri3d_stage_profile_add(35u, now - started);
    }
  } stage;
  if (arg_words != 4u || args[1] > 256u ||
      (args[3] & ~0x103u) || (args[3] & 3u) > 2u ||
      ((args[3] & 0x100u) && !(args[3] & 3u)))
    return NARM_SERVICE_ERR_ARGUMENTS;
  const uint32_t count = args[1];
  const uint32_t *commands = (const uint32_t *)(uintptr_t)args[0];
  const Graphics3dTransform *transform =
    (const Graphics3dTransform *)(uintptr_t)args[2];
  if (!native_pointer_valid(context, transform, sizeof(*transform)) ||
      (count != 0u && !native_pointer_valid(context, commands, count * 7u * sizeof(uint32_t))))
    return NARM_SERVICE_ERR_ARGUMENTS;
  for (uint32_t i = 0; i < 18u; ++i)
    if (!(transform->value[i] <= __FLT_MAX__ && transform->value[i] >= -__FLT_MAX__))
      return NARM_SERVICE_ERR_ARGUMENTS;
  if (!(transform->value[16] > 0.0f)) return NARM_SERVICE_ERR_ARGUMENTS;

  for (uint32_t i = 0; i < count; ++i)
  {
    const uint32_t *command = commands + i * 7u;
    const uint32_t vertex_count = command[6];
    const float *vertices = (const float *)(uintptr_t)command[0];
    const var *texture = (const var *)(uintptr_t)command[1];
    const var *mask = (const var *)(uintptr_t)command[2];
    const int32_t width = (int32_t)command[3];
    const int32_t height = (int32_t)command[4];
    uint32_t texture_words = 0u;
    uint32_t mask_words = 0u;
    if ((vertex_count != 3u && vertex_count != 4u) ||
        !native_pointer_valid(context, vertices, vertex_count * 5u * sizeof(float)) ||
        !native_checked_pixels(width, height, 4u, texture_words) ||
        texture_words > UINT32_MAX / sizeof(var) ||
        !native_pointer_valid(context, texture, texture_words * sizeof(var)) ||
        (mask && (!native_checked_pixels(width, height, 32u, mask_words) ||
                  mask_words > UINT32_MAX / sizeof(var) ||
                  !native_pointer_valid(context, mask, mask_words * sizeof(var)))))
      return NARM_SERVICE_ERR_ARGUMENTS;
    for (uint32_t element = 0; element < vertex_count * 5u; ++element)
      if (!(vertices[element] <= __FLT_MAX__ && vertices[element] >= -__FLT_MAX__))
        return NARM_SERVICE_ERR_ARGUMENTS;
    // No renderer job is active here, so termination cannot abandon a queue.
    if ((i & 31u) == 31u) abort_if_stop_requested(context, false);
  }
  if (count == 0u) return NARM_SERVICE_OK;
  abort_if_stop_requested(context, false);
  stage.validation_done();
  native_graphics_initialize_depth(stamp);
  if (!tri3d_begin_textured_list(color, depth, stamp, native_graphics_frame_id, args[3]))
  {
    stage.setup_done();
    return NARM_SERVICE_ERR_BUSY;
  }
  bool succeeded = true;
  const bool optimized_geometry = tri3d_geometry_optimized_enabled();
  Graphics3dTransform local_transform;
  if (optimized_geometry) local_transform = *transform;
  for (uint32_t i = 0; i < count; ++i)
  {
    const uint32_t *command = commands + i * 7u;
    const float *packed = (const float *)(uintptr_t)command[0];
    if (optimized_geometry)
    {
      // Validation already checked this packed input before any draw began.
      // Consume it directly; no RGB fields or intermediate vertex copy needed.
      if (!tri3d_draw_projected_textured_packed(color, depth, stamp, native_graphics_frame_id,
                                                packed, (uint8_t)command[6], &local_transform,
                                                (const var *)(uintptr_t)command[1],
                                                (int32_t)command[3], (int32_t)command[4],
                                                command[5], (const var *)(uintptr_t)command[2]))
      { succeeded = false; break; }
      continue;
    }
    Graphics3dVertex vertices[4] = {};
    for (uint32_t vertex = 0; vertex < command[6]; ++vertex)
    {
      const float *source = packed + vertex * 5u;
      vertices[vertex].x = source[0]; vertices[vertex].y = source[1];
      vertices[vertex].z = source[2]; vertices[vertex].u = source[3];
      vertices[vertex].v = source[4];
    }
    if (!tri3d_draw_projected(color, depth, stamp, native_graphics_frame_id,
                              vertices, (uint8_t)command[6], transform, 0u, false,
                              (const var *)(uintptr_t)command[1],
                              (int32_t)command[3], (int32_t)command[4],
                              command[5], false, (const var *)(uintptr_t)command[2]))
    {
      succeeded = false;
      break;
    }
  }
  // Stop requests are checked only after both workers have finished; the
  // native abort mechanism does not unwind C++ scopes or renderer ownership.
  stage.setup_done(); // Excludes only flushes nested inside preparation.
  const bool completed = tri3d_end_textured_list();
  abort_if_stop_requested(context, false);
  return succeeded && completed ? NARM_SERVICE_OK : NARM_SERVICE_ERR_ARGUMENTS;
}

// FNV-1a over little-endian RGB332 bytes, or canonical per-pixel depth state:
// one validity byte followed by four little-endian depth bytes (zero when
// invalid). Frame-id generations and stale depth never affect the latter.
uint32_t native_graphics_hash(const var *color,
                              const var *depth,
                              const var *stamp,
                              uint8_t frame_id,
                              bool initialized,
                              bool hash_depth)
{
  uint32_t hash = 2166136261u;
  for (uint32_t slot = 0; slot < 19200u; ++slot)
  {
    const uint32_t word = hash_depth ? stamp[slot].value : color[slot].value;
    for (uint32_t lane = 0; lane < 4u; ++lane)
    {
      const uint32_t byte = (word >> (lane * 8u)) & 255u;
      if (!hash_depth) hash = (hash ^ byte) * 16777619u;
      else
      {
        const bool valid = initialized && byte == frame_id;
        hash = (hash ^ (valid ? 1u : 0u)) * 16777619u;
        uint32_t value = valid ? depth[slot * 4u + lane].value : 0u;
        for (uint32_t part = 0; part < 4u; ++part)
        {
          hash = (hash ^ (value & 255u)) * 16777619u;
          value >>= 8;
        }
      }
    }
  }
  return hash;
}

int32_t video_submit(NativeContextV1 *context,
                     uint32_t command,
                     const uint32_t *args,
                     uint32_t arg_words)
{
  if (!valid_service_arguments(context, args, arg_words))
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_FRAMEBUFFER_2D))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (!native_owns_graphics(context)) return NARM_SERVICE_ERR_BUSY;

  if (command == NARM_GRAPHICS_STATS_ENABLE)
  {
    if (arg_words != 1u) return NARM_SERVICE_ERR_ARGUMENTS;
    tri3d_set_stats_enabled(args[0]);
    return NARM_SERVICE_OK;
  }
  if (command == NARM_GRAPHICS_STATS_READ)
  {
    if (arg_words != 1u) return NARM_SERVICE_ERR_ARGUMENTS;
    if (args[0] == 76u) return (int32_t)0x44430001u;
    if (args[0] == 22u) return (int32_t)clock_get_hz(clk_sys);
    if (args[0] == 23u) return (int32_t)clock_get_hz(clk_peri);
    if (args[0] != 32u && args[0] != 33u)
      return (int32_t)tri3d_stats_read(args[0]);
  }

  var *color = nullptr;
  var *depth = nullptr;
  var *stamp = nullptr;
  if (!native_graphics_buffers(color, depth, stamp))
    return NARM_SERVICE_ERR_CAPABILITY;

  if (command == NARM_GRAPHICS_STATS_READ)
  {
    tri3d_flush_solid_batch();
    return (int32_t)native_graphics_hash(color, depth, stamp,
                                         native_graphics_frame_id,
                                         native_graphics_depth_initialized,
                                         args[0] == 33u);
  }

  switch (command)
  {
    case NARM_GRAPHICS_HALF_PRESENT:
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      tri3d_flush_solid_batch(); graphics_half_present(color); return NARM_SERVICE_OK;
    case NARM_GRAPHICS_HALF_CLEAR:
      if (arg_words != 1u) return NARM_SERVICE_ERR_ARGUMENTS;
      native_graphics_begin_frame(color, stamp, (uint8_t)args[0]); return NARM_SERVICE_OK;
    case NARM_GRAPHICS_HALF_CLEAR_DEPTH:
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      native_graphics_clear_depth(stamp); return NARM_SERVICE_OK;
    case NARM_GRAPHICS_HALF_PIXEL:
      if (arg_words != 2u) return NARM_SERVICE_ERR_ARGUMENTS;
      tri3d_flush_solid_batch(); graphics_half_put_linear_pixel(color, args[0], (uint8_t)args[1]); return NARM_SERVICE_OK;
    case NARM_GRAPHICS_HALF_FILL_RECT:
      if (arg_words != 5u) return NARM_SERVICE_ERR_ARGUMENTS;
      tri3d_flush_solid_batch(); graphics_half_fill_rect(color, (int32_t)args[0], (int32_t)args[1], (int32_t)args[2], (int32_t)args[3], (uint8_t)args[4]); return NARM_SERVICE_OK;
    case NARM_GRAPHICS_HALF_LINE:
      if (arg_words != 5u) return NARM_SERVICE_ERR_ARGUMENTS;
      tri3d_flush_solid_batch(); graphics_half_draw_line(color, (int32_t)args[0], (int32_t)args[1], (int32_t)args[2], (int32_t)args[3], (uint8_t)args[4]); return NARM_SERVICE_OK;
    case NARM_GRAPHICS_HALF_FILL_TRIANGLE:
      if (arg_words != 7u) return NARM_SERVICE_ERR_ARGUMENTS;
      tri3d_flush_solid_batch(); graphics_half_fill_triangle(color, (int32_t)args[0], (int32_t)args[1], (int32_t)args[2], (int32_t)args[3], (int32_t)args[4], (int32_t)args[5], (uint8_t)args[6]); return NARM_SERVICE_OK;
    case NARM_GRAPHICS_HALF_BLIT:
    case NARM_GRAPHICS_HALF_BLIT_MASKED:
    {
      const bool masked = command == NARM_GRAPHICS_HALF_BLIT_MASKED;
      if (arg_words != (masked ? 6u : 5u)) return NARM_SERVICE_ERR_ARGUMENTS;
      const int32_t width = (int32_t)args[2], height = (int32_t)args[3];
      uint32_t texture_words = 0u, mask_words = 0u;
      const var *texture = (const var *)(uintptr_t)args[4];
      const var *mask = masked ? (const var *)(uintptr_t)args[5] : nullptr;
      if (!native_checked_texture_rows(width, height, texture_words) ||
          texture_words > UINT32_MAX / sizeof(var) ||
          !native_pointer_valid(context, texture, texture_words * sizeof(var)) ||
          (masked && (!native_checked_pixels(width, height, 32u, mask_words) ||
                      mask_words > UINT32_MAX / sizeof(var) ||
                      !native_pointer_valid(context, mask, mask_words * sizeof(var)))))
        return NARM_SERVICE_ERR_ARGUMENTS;
      tri3d_flush_solid_batch();
      const bool ok = masked
        ? graphics_half_blit_masked(color, (int32_t)args[0], (int32_t)args[1], width, height, texture, texture_words, mask, mask_words)
        : graphics_half_blit(color, (int32_t)args[0], (int32_t)args[1], width, height, texture, texture_words);
      return ok ? NARM_SERVICE_OK : NARM_SERVICE_ERR_ARGUMENTS;
    }
    case NARM_GRAPHICS_HALF_GLYPH:
    case NARM_GRAPHICS_VGA4_GLYPH:
    {
      if (arg_words != 6u) return NARM_SERVICE_ERR_ARGUMENTS;
      const int32_t width = (int32_t)args[2], height = (int32_t)args[3];
      uint32_t glyph_words = 0u;
      const var *glyph = (const var *)(uintptr_t)args[4];
      if (!native_checked_pixels(width, height, 32u, glyph_words) ||
          glyph_words > UINT32_MAX / sizeof(var) ||
          !native_pointer_valid(context, glyph, glyph_words * sizeof(var)))
        return NARM_SERVICE_ERR_ARGUMENTS;
      tri3d_flush_solid_batch();
      if (command == NARM_GRAPHICS_HALF_GLYPH)
        return graphics_half_glyph(color, (int32_t)args[0], (int32_t)args[1], width, height, glyph, glyph_words, (uint8_t)args[5]) ? NARM_SERVICE_OK : NARM_SERVICE_ERR_ARGUMENTS;
      tri3d_set_tile_scratch_enabled(false);
      graphics_vga4_glyph((int32_t)args[0], (int32_t)args[1], width, height, glyph, (uint8_t)args[5]);
      return NARM_SERVICE_OK;
    }
    case NARM_GRAPHICS_SCREEN_GOURAUD_TRIANGLE:
    {
      if (arg_words != 12u) return NARM_SERVICE_ERR_ARGUMENTS;
      native_graphics_initialize_depth(stamp);
      Graphics3dVertex vertices[3] = {};
      for (uint8_t i = 0; i < 3u; i++)
      {
        const uint32_t base = (uint32_t)i * 4u;
        vertices[i].x = (float)(int32_t)args[base];
        vertices[i].y = (float)(int32_t)args[base + 1u];
        vertices[i].z = (float)args[base + 2u];
        vertices[i].color = args[base + 3u];
      }
      tri3d_draw_screen_gouraud(color, depth, stamp, native_graphics_frame_id, vertices);
      return NARM_SERVICE_OK;
    }
    case NARM_GRAPHICS_TRI3D_FLAT:
      return native_draw_projected_command(context, color, depth, stamp, args, arg_words, 3u, false, false, false, false);
    case NARM_GRAPHICS_QUAD3D_FLAT:
      return native_draw_projected_command(context, color, depth, stamp, args, arg_words, 4u, false, false, false, false);
    case NARM_GRAPHICS_TRI3D_GOURAUD:
      return native_draw_projected_command(context, color, depth, stamp, args, arg_words, 3u, true, false, false, false);
    case NARM_GRAPHICS_QUAD3D_GOURAUD:
      return native_draw_projected_command(context, color, depth, stamp, args, arg_words, 4u, true, false, false, false);
    case NARM_GRAPHICS_TRI3D_TEXTURED:
      return native_draw_projected_command(context, color, depth, stamp, args, arg_words, 3u, false, true, false, false);
    case NARM_GRAPHICS_QUAD3D_TEXTURED:
      return native_draw_projected_command(context, color, depth, stamp, args, arg_words, 4u, false, true, false, false);
    case NARM_GRAPHICS_QUAD3D_TEXTURED_TILED:
      return native_draw_projected_command(context, color, depth, stamp, args, arg_words, 4u, false, true, true, false);
    case NARM_GRAPHICS_TRI3D_TEXTURED_MASKED:
      return native_draw_projected_command(context, color, depth, stamp, args, arg_words, 3u, false, true, false, true);
    case NARM_GRAPHICS_QUAD3D_TEXTURED_MASKED:
      return native_draw_projected_command(context, color, depth, stamp, args, arg_words, 4u, false, true, false, true);
    case NARM_GRAPHICS_PACKED_TERRAIN_MESH_LIST:
      return native_draw_terrain_command(context, color, depth, stamp, args, arg_words);
    case NARM_GRAPHICS_TEXTURED_LIST:
      return native_draw_textured_list(context, color, depth, stamp, args, arg_words);
    default:
      return NARM_SERVICE_ERR_UNKNOWN;
  }
}

const NativeVideoApiV1 native_video_api = {
  sizeof(NativeVideoApiV1),
  1,
  1,
  video_clear,
  video_pixel,
  video_fill_rect,
  video_line,
  video_present,
  video_submit
};

const void *api_get_interface(NativeContextV1 *context,
                              uint32_t interface_id,
                              uint16_t interface_major,
                              uint16_t minimum_minor)
{
  if (!valid_native_context(context))
  {
    return nullptr;
  }
  abort_if_stop_requested(context);
  if (interface_id == NARM_INTERFACE_KEYBOARD)
  {
    if (interface_major != native_keyboard_api.interface_major ||
        minimum_minor > native_keyboard_api.interface_minor ||
        !has_capability(context, NARM_CAP_KEYBOARD))
      return nullptr;
    return &native_keyboard_api;
  }
  if (interface_id == NARM_INTERFACE_AUDIO)
  {
    if (interface_major != native_audio_api.interface_major ||
        minimum_minor > native_audio_api.interface_minor ||
        !has_capability(context, NARM_CAP_AUDIO))
      return nullptr;
    return &native_audio_api;
  }
  if (interface_id == NARM_INTERFACE_FILE_IO)
  {
    if (interface_major != native_file_api.interface_major ||
        minimum_minor > native_file_api.interface_minor ||
        !has_capability(context, NARM_CAP_FILE_IO))
      return nullptr;
    return &native_file_api;
  }
  if (interface_id == NARM_INTERFACE_VIDEO_2D)
  {
    if (interface_major != native_video_api.interface_major ||
        minimum_minor > native_video_api.interface_minor ||
        !has_capability(context, NARM_CAP_FRAMEBUFFER_2D))
      return nullptr;
    return &native_video_api;
  }
  return nullptr;
}

void api_yield(NativeContextV1 *context, uint32_t)
{
  if (!valid_native_context(context) || !context->yield_requested)
  {
    return;
  }
  abort_if_stop_requested(context, false);
  __atomic_fetch_and(context->yield_requested,
                     ~((uint32_t)NARM_CONTROL_YIELD_REQUESTED),
                     __ATOMIC_ACQ_REL);
  narm_arch_yield(NARM_ARCH_SWITCH_YIELD);
}

void api_exit(NativeContextV1 *context, int32_t status)
{
  if (!valid_native_context(context))
  {
    return;
  }
  terminate_native(context, status);
}

const NativeApiV1 native_api = {
  sizeof(NativeApiV1),
  NARM_API_MAJOR,
  NARM_API_MINOR,
  (uint32_t)(NARM_CAP_FRAMEBUFFER_2D | NARM_CAP_KEYBOARD | NARM_CAP_AUDIO |
             NARM_CAP_FILE_IO | NARM_CAP_USB_STORAGE),
  0,
  api_invoke,
  api_yield,
  api_exit,
  api_get_interface
};

bool copy_from_reader(const NarmReader &reader,
                      uint32_t file_offset,
                      uint8_t *destination,
                      uint32_t bytes)
{
  uint8_t buffer[512];
  uint32_t copied = 0;
  while (copied < bytes)
  {
    uint32_t remaining = bytes - copied;
    uint32_t chunk = remaining < sizeof(buffer) ? remaining : (uint32_t)sizeof(buffer);
    if (!narm_read_exact(reader, file_offset + copied, buffer, chunk))
    {
      return false;
    }
    memcpy(destination + copied, buffer, chunk);
    copied += chunk;
  }
  return true;
}

bool apply_relocations(const NarmReader &reader, NativeProcess &process)
{
  for (uint32_t i = 0; i < process.image.reloc_count; ++i)
  {
    NarmRelocation relocation = {};
    if (narm_read_relocation(reader, process.image, i, relocation) != NARM_OK)
    {
      return false;
    }

    uint32_t value = 0;
    switch (relocation.type)
    {
      case NARM_RELOC_NONE:
        continue;
      case NARM_RELOC_DATA_PTR32:
        value = (uint32_t)(uintptr_t)(process.data.aligned + relocation.target_addend);
        break;
      case NARM_RELOC_TEXT_PTR32:
        value = (uint32_t)(uintptr_t)(process.text.aligned + relocation.target_addend);
        break;
      case NARM_RELOC_THUMB_FUNC32:
        value = (uint32_t)(uintptr_t)(process.text.aligned + relocation.target_addend) | 1u;
        break;
      case NARM_RELOC_CONTEXT_PTR32:
        value = (uint32_t)(uintptr_t)&process.context;
        break;
      default:
        return false;
    }
    memcpy(process.data.aligned + relocation.patch_data_offset, &value, sizeof(value));
  }
  return true;
}

bool synchronize_executable_text(const NativeAllocation &text)
{
  if (!text.aligned || text.bytes == 0)
  {
    return false;
  }

  uintptr_t start = (uintptr_t)text.aligned & ~(NARM_XIP_CACHE_LINE_SIZE - 1u);
  uintptr_t end_unaligned = (uintptr_t)text.aligned + text.bytes;
  uintptr_t end = (end_unaligned + NARM_XIP_CACHE_LINE_SIZE - 1u) &
                  ~(NARM_XIP_CACHE_LINE_SIZE - 1u);
  if (start < XIP_BASE || end > XIP_END || end < start)
  {
    return false;
  }

  uintptr_t offset = start - XIP_BASE;
  uintptr_t bytes = end - start;
  narm_xip_cache_maintain(offset, bytes, NARM_XIP_CACHE_CLEAN_BY_ADDRESS);
  narm_xip_cache_maintain(offset, bytes, NARM_XIP_CACHE_INVALIDATE_BY_ADDRESS);
  return true;
}

void print_failure(const char *stage,
                   NarmStatus status,
                   const NarmDiagnostic &diagnostic,
                   uint32_t requested_bytes)
{
  Serial.print("\nNARM ");
  Serial.print(stage);
  Serial.print(" FAILED: ");
  Serial.print(narm_status_string(status));
  Serial.print(" (");
  Serial.print((int32_t)status);
  Serial.print(") FIELD=0x");
  Serial.print(diagnostic.field_offset, HEX);
  Serial.print(" DETAIL=");
  Serial.print(diagnostic.detail);
  Serial.print(" REQUESTED=");
  Serial.print(requested_bytes);
  Serial.print(" FREE_NARM_PSRAM=");
  Serial.print(native_arena_free_bytes(narm_psram_arena));
  Serial.print(" FREE_NARM_SRAM=");
  Serial.println(native_arena_free_bytes(narm_sram_arena));
}

bool initialize_process_context(NativeProcess &process)
{
  uintptr_t top = (uintptr_t)(process.stack.aligned + process.image.stack_bytes);
  top &= ~(uintptr_t)7u;
  if (top < (uintptr_t)process.stack.aligned + 64u) return false;

  uint32_t *hardware_frame = (uint32_t *)top - 8;
  memset(hardware_frame, 0, 8u * sizeof(uint32_t));
  hardware_frame[0] = (uint32_t)(uintptr_t)&process.context;
  hardware_frame[5] = (uint32_t)(uintptr_t)narm_arch_task_return | 1u;
  hardware_frame[6] = (uint32_t)process.entry & ~1u;
  hardware_frame[7] = 0x01000000u;

  uint32_t *software_frame = hardware_frame - 8;
  memset(software_frame, 0, 8u * sizeof(uint32_t));
  software_frame[4] = (uint32_t)(uintptr_t)process.text.aligned; // r8
  software_frame[5] = (uint32_t)(uintptr_t)process.data.aligned; // r9
  software_frame[6] = (uint32_t)(uintptr_t)&process.context;     // r10

  process.arch.saved_psp = software_frame;
  process.arch.exception_return = 0xfffffffdu;
  process.arch.control = 0u;
  process.arch.psplim = (uint32_t)(uintptr_t)process.stack.aligned;
  return true;
}

NarmStatus load_process(const NarmReader &reader, NativeProcess *&loaded_process)
{
  loaded_process = nullptr;
  native_scheduler_begin();
  if (!scheduler_selftest_passed) return NARM_ERR_FAULT;
  NativeProcess *process = claim_process();
  if (!process)
  {
    return NARM_ERR_NO_PROCESS_SLOT;
  }

  NarmDiagnostic diagnostic = {};
  NarmValidationPolicy policy = narm_default_validation_policy();
  policy.service_supported = service_supported;
  policy.capabilities_low = (uint32_t)(NARM_CAP_FRAMEBUFFER_2D | NARM_CAP_KEYBOARD |
                                       NARM_CAP_AUDIO | NARM_CAP_FILE_IO |
                                       NARM_CAP_USB_STORAGE);
  NarmStatus status = narm_validate_image(reader,
                                          policy,
                                          process->image,
                                          process->layout,
                                          &diagnostic);
  if (status != NARM_OK)
  {
    print_failure("VALIDATION", status, diagnostic, 0);
    release_process(*process);
    return status;
  }

  process->default_mode =
      (process->image.flags & NARM_FLAG_BACKGROUND_DEFAULT) != 0u
        ? NARM_RUN_MODE_BACKGROUND
        : NARM_RUN_MODE_FOREGROUND;

  const uint32_t free_native_memory = native_arena_total_free_bytes();
  if (process->layout.total_allocation_bytes > free_native_memory)
  {
    diagnostic.code = NARM_ERR_NO_MEMORY;
    diagnostic.field_offset = 0x14u;
    diagnostic.detail = free_native_memory;
    print_failure("ALLOCATION", NARM_ERR_NO_MEMORY, diagnostic,
                  process->layout.total_allocation_bytes);
    release_process(*process);
    return NARM_ERR_NO_MEMORY;
  }

  if (!aligned_native_allocate(process->image.text_bytes,
                               process->image.text_alignment,
                               false,
                               process->text) ||
      // Allocate the hottest region first. Generated calls and any remaining
      // expression spills hit the native stack continuously.
      !aligned_native_allocate(process->image.stack_bytes,
                               8u,
                               true,
                               process->stack) ||
      !aligned_native_allocate(process->layout.data_arena_bytes,
                               maximum_data_alignment(process->image),
                               true,
                               process->data))
  {
    diagnostic.code = NARM_ERR_NO_MEMORY;
    diagnostic.field_offset = 0x14u;
    diagnostic.detail = native_arena_total_free_bytes();
    print_failure("ALLOCATION", NARM_ERR_NO_MEMORY, diagnostic,
                  process->layout.total_allocation_bytes);
    release_process(*process);
    return NARM_ERR_NO_MEMORY;
  }

  if (process->layout.data_arena_bytes)
  {
    memset(process->data.aligned, 0, process->layout.data_arena_bytes);
  }
  memset(process->stack.aligned, 0xcd, process->image.stack_bytes);

  uint8_t *rodata_destination = process->data.aligned
                                  ? process->data.aligned + process->layout.rodata_runtime_offset
                                  : nullptr;
  uint8_t *data_destination = process->data.aligned
                                ? process->data.aligned + process->layout.data_runtime_offset
                                : nullptr;

  if (!copy_from_reader(reader,
                        process->image.text_file_offset,
                        process->text.aligned,
                        process->image.text_bytes) ||
      !copy_from_reader(reader,
                        process->image.rodata_file_offset,
                        rodata_destination,
                        process->image.rodata_bytes) ||
      !copy_from_reader(reader,
                        process->image.data_file_offset,
                        data_destination,
                        process->image.data_bytes))
  {
    diagnostic.code = NARM_ERR_IO;
    diagnostic.field_offset = 0x1cu;
    diagnostic.detail = 0;
    print_failure("COPY", NARM_ERR_IO, diagnostic,
                  process->layout.total_allocation_bytes);
    release_process(*process);
    return NARM_ERR_IO;
  }

  memset(process->name, 0, sizeof(process->name));
  uint32_t name_bytes = process->image.name_bytes;
  if (name_bytes >= sizeof(process->name))
  {
    name_bytes = sizeof(process->name) - 1u;
  }
  if (name_bytes && !narm_read_exact(reader,
                                     process->image.name_file_offset,
                                     process->name,
                                     name_bytes))
  {
    release_process(*process);
    return NARM_ERR_IO;
  }

  process->context.struct_bytes = sizeof(NativeContextV1);
  process->context.abi_major = NARM_ABI_MAJOR;
  process->context.abi_minor = NARM_ABI_MINOR;
  process->context.api = &native_api;
  process->context.process_handle = process;
  process->context.text_base = process->text.aligned;
  process->context.text_bytes = process->image.text_bytes;
  process->context.data_base = process->data.aligned;
  process->context.data_bytes = process->layout.data_arena_bytes;
  process->context.heap_base = process->data.aligned
                                 ? process->data.aligned + process->layout.heap_runtime_offset
                                 : nullptr;
  process->context.heap_bytes = process->image.heap_bytes;
  process->context.yield_requested = &process_control_flags[process - processes];
  process->context.granted_capabilities_low = process->image.capabilities_low;
  process->context.granted_capabilities_high = process->image.capabilities_high;
  process->context.keyboard_api =
      (process->image.capabilities_low & (uint32_t)NARM_CAP_KEYBOARD)
        ? &native_keyboard_api : nullptr;
  process->context.video_api =
      (process->image.capabilities_low & (uint32_t)NARM_CAP_FRAMEBUFFER_2D)
        ? &native_video_api : nullptr;
  process->context.audio_api =
      (process->image.capabilities_low & (uint32_t)NARM_CAP_AUDIO)
        ? &native_audio_api : nullptr;
  process->context.file_api =
      (process->image.capabilities_low & (uint32_t)NARM_CAP_FILE_IO)
        ? &native_file_api : nullptr;

  if (!apply_relocations(reader, *process))
  {
    diagnostic.code = NARM_ERR_RELOCATION;
    diagnostic.field_offset = 0x50u;
    diagnostic.detail = 0;
    print_failure("RELOCATION", NARM_ERR_RELOCATION, diagnostic,
                  process->layout.total_allocation_bytes);
    release_process(*process);
    return NARM_ERR_RELOCATION;
  }
  if (!synchronize_executable_text(process->text))
  {
    diagnostic.code = NARM_ERR_CACHE;
    diagnostic.field_offset = 0x1cu;
    diagnostic.detail = (uint32_t)(uintptr_t)process->text.aligned;
    print_failure("CACHE", NARM_ERR_CACHE, diagnostic,
                  process->layout.total_allocation_bytes);
    release_process(*process);
    return NARM_ERR_CACHE;
  }

  process->entry = (uintptr_t)(process->text.aligned + process->image.entry_text_offset) | 1u;
  if (!initialize_process_context(*process))
  {
    release_process(*process);
    return NARM_ERR_LAYOUT;
  }
  process->state = NATIVE_PROCESS_READY;

  Serial.print("\nNARM START: ");
  Serial.print(process->name[0] ? process->name : "<unnamed>");
  Serial.print(" TEXT=0x");
  Serial.print((uint32_t)(uintptr_t)process->text.aligned, HEX);
  Serial.print(process->data.internal_sram ? " DATA(SRAM)=0x" : " DATA(PSRAM)=0x");
  Serial.print((uint32_t)(uintptr_t)process->data.aligned, HEX);
  Serial.print(process->stack.internal_sram ? " STACK(SRAM)=0x" : " STACK(PSRAM)=0x");
  Serial.println((uint32_t)(uintptr_t)(process->stack.aligned +
                                      process->image.stack_bytes), HEX);
  loaded_process = process;
  return NARM_OK;
}

bool sd_read_at(void *user, uint32_t offset, void *destination, uint32_t bytes)
{
  // The NARM file remains open while the native process runs, but the SD bus
  // must only be owned while a loader read is actually in progress.  Holding
  // the bus for the lifetime of load_and_run() prevents Core 1 from refilling
  // streamed audio after its startup ring has drained.
  SdBusGuard sdBusGuard;
  FirmwareFile *file = (FirmwareFile *)user;
  return file->seekSet(offset) && file->read(destination, bytes) == (int)bytes;
}

bool fs_read_at(void *user, uint32_t offset, void *destination, uint32_t bytes)
{
  File *file = (File *)user;
  return file->seek(offset) && file->read((uint8_t *)destination, bytes) == (int)bytes;
}

void set_process_focus(NativeProcess *next)
{
  NativeProcess *previous = __atomic_load_n(&focused_process, __ATOMIC_ACQUIRE);
  if (previous == next && terminal_has_focus == (next == nullptr)) return;
  if (previous)
  {
    previous->focused = false;
    update_process_audio_pause(*previous);
    push_system_event(*previous, NARM_EVENT_FOCUS_LOST);
    if (effective_mode(*previous) == NARM_RUN_MODE_FOREGROUND &&
        previous->state == NATIVE_PROCESS_READY)
      previous->state = NATIVE_PROCESS_SUSPENDED;
  }
  __atomic_store_n(&focused_process, next, __ATOMIC_RELEASE);
  __atomic_store_n(&scheduler_focused_pid, next ? next->pid : 0u,
                   __ATOMIC_RELEASE);
  terminal_has_focus = next == nullptr;
  keyboard_clear_events();
  keyboard_set_focus(next ? KEYBOARD_FOCUS_NARM : KEYBOARD_FOCUS_TERMINAL);
  if (next)
  {
    next->focused = true;
    update_process_audio_pause(*next);
    if (next->state == NATIVE_PROCESS_SUSPENDED)
      next->state = NATIVE_PROCESS_READY;
    push_system_event(*next, NARM_EVENT_FOCUS_GAINED);
    system_overlay_show_focus(next->pid, next->name);
  }
  else system_overlay_show_focus(0u, "TERMINAL");
}

bool scheduler_timer_callback(repeating_timer_t *)
{
  ++scheduler_timer_ticks;
  scheduler_timer_core = get_core_num();
  // NARMCC emits safepoints which observe this flag. Keep that cooperative
  // path active even though PendSV normally provides lower-latency preemption;
  // it is the portable correctness path when an exception cannot safely cut
  // through the code currently executing on Core 0.
  uint32_t *control = __atomic_load_n(&scheduler_active_control_flags,
                                      __ATOMIC_ACQUIRE);
  if (control)
  {
    __atomic_fetch_or(control,
                      (uint32_t)NARM_CONTROL_YIELD_REQUESTED,
                      __ATOMIC_ACQ_REL);
  }
  if (get_core_num() == 0u &&
      __atomic_load_n(&narm_arch_current_context, __ATOMIC_ACQUIRE))
  {
    *(volatile uint32_t *)0xe000ed04u = 1u << 28;
    __dsb();
    __isb();
  }
  return true;
}

void scheduler_pend_context_switch()
{
  if (__atomic_load_n(&narm_arch_current_context, __ATOMIC_ACQUIRE))
  {
    *(volatile uint32_t *)0xe000ed04u = 1u << 28;
    __dsb();
    __isb();
  }
}

void scheduler_doorbell_irq()
{
  const int32_t doorbell = __atomic_load_n(&scheduler_doorbell,
                                            __ATOMIC_ACQUIRE);
  if (doorbell < 0 ||
      !multicore_doorbell_is_set_current_core((uint)doorbell))
  {
    return;
  }

  // Acknowledge first. The command bits remain latched until Core 0 consumes
  // them, so another key transition can safely ring the same doorbell again.
  multicore_doorbell_clear_current_core((uint)doorbell);
  scheduler_pend_context_switch();
}

void scheduler_signal_core0()
{
  const int32_t doorbell = __atomic_load_n(&scheduler_doorbell,
                                            __ATOMIC_ACQUIRE);
  if (doorbell < 0) return;

  if (get_core_num() == 0u)
  {
    scheduler_pend_context_switch();
  }
  else
  {
    multicore_doorbell_set_other_core((uint)doorbell);
  }
}

void scheduler_request_active_safepoint(uint32_t flags)
{
  // Publish only an address in the fixed SRAM array. Core 1 must not chase a
  // process record whose context can be cleared by Core 0 during termination.
  uint32_t *control = __atomic_load_n(&scheduler_active_control_flags,
                                      __ATOMIC_ACQUIRE);
  if (control) __atomic_fetch_or(control, flags, __ATOMIC_ACQ_REL);
}

bool scheduler_preemption_selftest()
{
  alignas(8) uint32_t stack[128] = {};
  uint32_t *hardware_frame = stack + 128u - 8u;
  hardware_frame[5] = (uint32_t)(uintptr_t)narm_arch_task_return | 1u;
  hardware_frame[6] = (uint32_t)(uintptr_t)narm_arch_probe_entry & ~1u;
  hardware_frame[7] = 0x01000000u;
  NativeArchContext probe = {hardware_frame - 8u, 0xfffffffdu, 0u,
                             (uint32_t)(uintptr_t)stack};
  narm_arch_text_begin = (uintptr_t)narm_arch_probe_entry & ~(uintptr_t)1u;
  narm_arch_text_end = (uintptr_t)narm_arch_probe_end;
  bool passed = true;
  for (uint32_t attempt = 0; attempt < 2u && passed; ++attempt)
  {
    __atomic_store_n(&narm_arch_current_context, &probe, __ATOMIC_RELEASE);
    const uint32_t reason = narm_arch_resume(&probe);
    __atomic_store_n(&narm_arch_current_context,
                     (NativeArchContext *)nullptr, __ATOMIC_RELEASE);
    passed = reason == NARM_ARCH_SWITCH_TIMESLICE &&
             (probe.exception_return & 0x10u) == 0u;
    if (!passed) break;
    const uint32_t *saved = probe.saved_psp;
    for (uint32_t reg = 0; reg < 8u; ++reg)
      passed = passed && saved[reg] == 0x44u + reg * 0x11u;
    // Eight integer + sixteen software-saved FP registers precede the
    // hardware core frame. s0 follows its eight core registers on Armv8-M.
    passed = passed && saved[8] == 0x3f800000u &&
             saved[23] == 0x3f800000u && saved[32] == 0x3f800000u;
  }
  narm_arch_text_begin = narm_arch_text_end = 0u;
  narm_arch_switch_reason = NARM_ARCH_SWITCH_NONE;
  return passed && scheduler_timer_core == 0u;
}

bool process_runnable(NativeProcess &process)
{
  if (process.close_requested)
  {
    if (process.state == NATIVE_PROCESS_SUSPENDED ||
        process.state == NATIVE_PROCESS_WAITING)
      process.state = NATIVE_PROCESS_READY;
    return process.state == NATIVE_PROCESS_READY;
  }
  if (process.state == NATIVE_PROCESS_WAITING &&
      (int32_t)(millis() - process.wake_deadline_ms) >= 0)
    process.state = NATIVE_PROCESS_READY;
  if (process.state != NATIVE_PROCESS_READY) return false;
  return process.focused || effective_mode(process) == NARM_RUN_MODE_BACKGROUND;
}

void finish_process(NativeProcess &process, int32_t status)
{
  const bool was_focused = process.focused;
  const uint32_t parent_pid = process.parent_pid;
  process.exit_status = status;
  process.state = NATIVE_PROCESS_EXITED;
  last_exit_status = status;
  native_audio_release(process);
  if (was_focused) native_graphics_release();
  Serial.print("NARM EXIT PID=");
  Serial.print(process.pid);
  Serial.print(" STATUS=");
  Serial.println(status);
  if (was_focused) set_process_focus(nullptr);
  release_process(process);
  if (was_focused && parent_pid != 0u)
  {
    for (uint32_t i = 0; i < NARM_MAX_PROCESSES; ++i)
    {
      NativeProcess &parent = processes[i];
      if (parent.pid == parent_pid &&
          (parent.state == NATIVE_PROCESS_READY ||
           parent.state == NATIVE_PROCESS_WAITING ||
           parent.state == NATIVE_PROCESS_SUSPENDED))
      {
        set_process_focus(&parent);
        break;
      }
    }
  }
}

NativeProcess *find_process_by_pid(uint32_t pid)
{
  for (uint32_t i = 0; i < NARM_MAX_PROCESSES; ++i)
    if (processes[i].state != NATIVE_PROCESS_EMPTY && processes[i].pid == pid)
      return &processes[i];
  return nullptr;
}
} // namespace

NarmStatus native_run_sd(const String &path)
{
  FirmwareFile file;
  uint64_t size = 0;
  {
    SdBusGuard sdBusGuard;
    if (sdEnsureReady() != 1)
    {
      last_status = NARM_ERR_IO;
      return last_status;
    }

    file = SD.open(path.c_str(), FIRMWARE_FILE_READ);
    if (!file)
    {
      last_status = NARM_ERR_IO;
      return last_status;
    }

    size = file.fileSize();
    if (size > UINT32_MAX)
    {
      file.close();
      last_status = NARM_ERR_SIZE;
      return last_status;
    }
  }

  NarmReader reader = {&file, (uint32_t)size, sd_read_at};
  NativeProcess *loaded = nullptr;
  last_status = load_process(reader, loaded);
  {
    SdBusGuard sdBusGuard;
    file.close();
  }
  if (last_status == NARM_OK && loaded) set_process_focus(loaded);
  return last_status;
}

NarmStatus native_run_fs(const String &path)
{
  if (!flash_storage_begin())
  {
    last_status = NARM_ERR_IO;
    return last_status;
  }
  String normalized = path;
  if (!normalized.startsWith("/"))
  {
    normalized = "/" + normalized;
  }

  File file = FatFS.open(normalized, "r");
  if (!file)
  {
    last_status = NARM_ERR_IO;
    return last_status;
  }

  size_t size = file.size();
  if (size > UINT32_MAX)
  {
    file.close();
    last_status = NARM_ERR_SIZE;
    return last_status;
  }

  NarmReader reader = {&file, (uint32_t)size, fs_read_at};
  NativeProcess *loaded = nullptr;
  last_status = load_process(reader, loaded);
  file.close();
  if (last_status == NARM_OK && loaded) set_process_focus(loaded);
  return last_status;
}

NarmStatus native_last_status()
{
  return last_status;
}

int32_t native_last_exit_status()
{
  return last_exit_status;
}

uint32_t native_running_processes()
{
  if (!process_table_initialized)
  {
    return 0;
  }

  uint32_t count = 0;
  for (uint32_t i = 0; i < NARM_MAX_PROCESSES; ++i)
  {
    if (processes[i].state == NATIVE_PROCESS_RUNNING ||
        processes[i].state == NATIVE_PROCESS_READY ||
        processes[i].state == NATIVE_PROCESS_WAITING ||
        processes[i].state == NATIVE_PROCESS_SUSPENDED)
    {
      ++count;
    }
  }
  return count;
}

bool native_request_stop()
{
  const uint32_t pid = __atomic_load_n(&scheduler_focused_pid, __ATOMIC_ACQUIRE);
  if (pid == 0u) return false;
  __atomic_fetch_add(&scheduler_command_requests[2], 1u, __ATOMIC_RELAXED);
  critical_section_enter_blocking(&scheduler_command_lock);
  scheduler_close_requested_pid = pid;
  __atomic_fetch_or(&narm_arch_scheduler_commands,
                    SCHEDULER_COMMAND_CLOSE_FOCUSED,
                    __ATOMIC_ACQ_REL);
  critical_section_exit(&scheduler_command_lock);
  scheduler_request_active_safepoint(NARM_CONTROL_YIELD_REQUESTED);
  scheduler_signal_core0();
  return true;
}

bool native_request_focus_next()
{
  __atomic_fetch_add(&scheduler_command_requests[0], 1u, __ATOMIC_RELAXED);
  __atomic_fetch_or(&narm_arch_scheduler_commands,
                    SCHEDULER_COMMAND_FOCUS_NEXT,
                    __ATOMIC_ACQ_REL);
  scheduler_request_active_safepoint(
      (uint32_t)NARM_CONTROL_YIELD_REQUESTED);
  scheduler_signal_core0();
  return true;
}

bool native_request_toggle_mode()
{
  __atomic_fetch_add(&scheduler_command_requests[1], 1u, __ATOMIC_RELAXED);
  const uint32_t pid = __atomic_load_n(&scheduler_focused_pid, __ATOMIC_ACQUIRE);
  if (pid == 0u) return false;
  critical_section_enter_blocking(&scheduler_command_lock);
  scheduler_mode_requested_pid = pid;
  __atomic_fetch_or(&narm_arch_scheduler_commands,
                    SCHEDULER_COMMAND_TOGGLE_MODE,
                    __ATOMIC_ACQ_REL);
  critical_section_exit(&scheduler_command_lock);
  scheduler_request_active_safepoint(
      (uint32_t)NARM_CONTROL_YIELD_REQUESTED);
  scheduler_signal_core0();
  return true;
}

void native_scheduler_begin()
{
  if (scheduler_initialized) return;
  scheduler_initialized = true;
  critical_section_init(&scheduler_command_lock);
  // PendSV must run after peripheral IRQ handlers return to Thread/PSP. At
  // reset priority zero it instead preempts the timer ISR on MSP, and the
  // trampoline correctly rejects that handler frame, losing every switch.
  exception_set_priority(PENDSV_EXCEPTION, PICO_LOWEST_EXCEPTION_PRIORITY);
  exception_set_priority(SVCALL_EXCEPTION, 0x80u);
  // Explicit eager FP stacking preserves s0-s15/FPSCR on every context switch
  // without leaving a lazy firmware MSP frame live while restoring an app.
  m33_hw->fpccr = (m33_hw->fpccr | M33_FPCCR_ASPEN_BITS) &
                  ~M33_FPCCR_LSPEN_BITS;
  __dsb();
  __isb();
  exception_set_exclusive_handler(SVCALL_EXCEPTION, narm_arch_svcall_handler);
  exception_set_exclusive_handler(PENDSV_EXCEPTION, narm_arch_pendsv_handler);

  const int32_t doorbell = multicore_doorbell_claim_unused(1u << 0, false);
  if (doorbell >= 0)
  {
    __atomic_store_n(&scheduler_doorbell, doorbell, __ATOMIC_RELEASE);
    irq_add_shared_handler(SIO_IRQ_BELL,
                           scheduler_doorbell_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(SIO_IRQ_BELL, true);
  }
  scheduler_timer_started = add_repeating_timer_us(-5000,
                                                    scheduler_timer_callback,
                                                    nullptr,
                                                    &scheduler_timer);
  scheduler_selftest_passed = scheduler_timer_started &&
                              scheduler_preemption_selftest();
  const char *result = scheduler_selftest_passed ? "OK" : "FAILED";
  Serial.print("NARM MT-R3: scheduler self-test ");
  Serial.println(result);
  echo_message(String("=>NARM MT-R3: scheduler self-test ") + result);
}

void native_scheduler_poll()
{
  native_scheduler_begin();
  critical_section_enter_blocking(&scheduler_command_lock);
  const uint32_t commands = __atomic_exchange_n(&narm_arch_scheduler_commands,
                                                 0u,
                                                 __ATOMIC_ACQ_REL);
  const uint32_t close_pid = scheduler_close_requested_pid;
  const uint32_t mode_pid = scheduler_mode_requested_pid;
  scheduler_close_requested_pid = scheduler_mode_requested_pid = 0u;
  critical_section_exit(&scheduler_command_lock);
  if (commands != 0u) ++scheduler_commands_applied;
  if ((commands & SCHEDULER_COMMAND_FOCUS_NEXT) != 0u) native_focus_next();
  if ((commands & SCHEDULER_COMMAND_TOGGLE_MODE) != 0u)
  {
    NativeProcess *changing = find_process_by_pid(mode_pid);
    if (changing)
      native_task_set_mode(mode_pid,
          effective_mode(*changing) == NARM_RUN_MODE_BACKGROUND
            ? NARM_RUN_MODE_FOREGROUND : NARM_RUN_MODE_BACKGROUND);
  }
  if ((commands & SCHEDULER_COMMAND_CLOSE_FOCUSED) != 0u)
  {
    // Target the instance focused when F4 was pressed, even if an F2 command
    // was collected in the same scheduling boundary.
    NativeProcess *closing = find_process_by_pid(close_pid);
    if (closing)
    {
      if (!closing->close_requested)
      {
        closing->close_requested = true;
        closing->close_deadline_ms = millis() + 250u;
        push_system_event(*closing, NARM_EVENT_CLOSE_REQUESTED);
      }
    }
  }
  NativeProcess *focused = __atomic_load_n(&focused_process, __ATOMIC_ACQUIRE);
  NativeProcess *selected = nullptr;
  const bool storage_mode = usb_storage_active();
  const uint32_t storage_owner = usb_storage_owner_pid();
  const auto storage_allows = [storage_mode, storage_owner](const NativeProcess &process)
  {
    return !storage_mode || (storage_owner != 0u && process.pid == storage_owner);
  };

  if (focused && storage_allows(*focused) && process_runnable(*focused) &&
      foreground_budget < 4u)
  {
    selected = focused;
    ++foreground_budget;
  }
  else
  {
    foreground_budget = 0u;
    const bool background_due = focused != nullptr ||
                                time_us_64() >= terminal_background_due_us;
    for (uint32_t checked = 0; background_due && checked < NARM_MAX_PROCESSES; ++checked)
    {
      scheduler_cursor = (scheduler_cursor + 1u) % NARM_MAX_PROCESSES;
      NativeProcess &candidate = processes[scheduler_cursor];
      if (&candidate != focused && storage_allows(candidate) &&
          process_runnable(candidate) &&
          (candidate.close_requested ||
           effective_mode(candidate) == NARM_RUN_MODE_BACKGROUND))
      {
        selected = &candidate;
        break;
      }
    }
    if (!selected && focused && storage_allows(*focused) &&
        process_runnable(*focused)) selected = focused;
  }
  if (!selected) return;

  selected->state = NATIVE_PROCESS_RUNNING;
  __atomic_store_n(&scheduler_active_control_flags,
                   &process_control_flags[selected - processes],
                   __ATOMIC_RELEASE);
  __atomic_store_n(&active_process, selected, __ATOMIC_RELEASE);
  __atomic_store_n(&narm_arch_current_context, &selected->arch, __ATOMIC_RELEASE);
  narm_arch_text_begin = (uintptr_t)selected->text.aligned;
  narm_arch_text_end = narm_arch_text_begin + selected->text.bytes;
  narm_arch_requested_reason = NARM_ARCH_SWITCH_YIELD;
  narm_arch_switch_reason = NARM_ARCH_SWITCH_NONE;
  ++selected->cpu_slices;
  const uint32_t reason = narm_arch_resume(&selected->arch);
  __atomic_store_n(&scheduler_active_control_flags, (uint32_t *)nullptr,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&narm_arch_current_context,
                   (NativeArchContext *)nullptr,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&active_process, (NativeProcess *)nullptr, __ATOMIC_RELEASE);
  if (reason == NARM_ARCH_SWITCH_TIMESLICE) ++scheduler_timeslices;
  else if (reason == NARM_ARCH_SWITCH_YIELD) ++scheduler_yields;

  if (!focused && selected != focused)
    terminal_background_due_us = time_us_64() + 20000u;

  if (reason == NARM_ARCH_SWITCH_EXIT)
  {
    const bool chain = selected->launch_requested &&
                       narm_arch_exit_status == NARM_EXIT_CHAIN_EXEC;
    String chain_path;
    uint32_t chain_storage = 0u;
    if (chain)
    {
      chain_path = String(selected->launch_path);
      chain_storage = selected->launch_storage;
    }
    finish_process(*selected, narm_arch_exit_status);
    if (chain && chain_path.length() != 0u)
    {
      if (chain_storage == 1u) native_run_sd(chain_path);
      else native_run_fs(chain_path);
    }
    return;
  }
  if (selected->close_requested &&
      (int32_t)(millis() - selected->close_deadline_ms) >= 0)
  {
    finish_process(*selected, NARM_EXIT_USER_TERMINATED);
    return;
  }
  if (selected->spawn_requested)
  {
    selected->spawn_requested = false;
    const NarmStatus status = selected->launch_storage == 1u
        ? native_run_sd(String(selected->launch_path))
        : native_run_fs(String(selected->launch_path));
    NativeProcess *child = __atomic_load_n(&focused_process, __ATOMIC_ACQUIRE);
    if (status == NARM_OK && child && child != selected)
    {
      child->parent_pid = selected->pid;
      selected->spawn_result = (int32_t)child->pid;
    }
    else selected->spawn_result = status == NARM_OK
        ? NARM_SERVICE_ERR_BUSY : (int32_t)status;
  }
  if (selected->state == NATIVE_PROCESS_RUNNING)
    selected->state = (!selected->focused &&
                       effective_mode(*selected) == NARM_RUN_MODE_FOREGROUND)
                        ? NATIVE_PROCESS_SUSPENDED
                        : NATIVE_PROCESS_READY;
}

bool native_focus_next()
{
  if (usb_storage_active()) return false;
  NativeProcess *current = __atomic_load_n(&focused_process, __ATOMIC_ACQUIRE);
  int32_t start = -1;
  if (current) start = (int32_t)(current - processes);
  for (int32_t i = start + 1; i < (int32_t)NARM_MAX_PROCESSES; ++i)
  {
    if (processes[i].state != NATIVE_PROCESS_EMPTY &&
        processes[i].state != NATIVE_PROCESS_EXITED &&
        processes[i].state != NATIVE_PROCESS_FAULTED)
    {
      set_process_focus(&processes[i]);
      return true;
    }
  }
  if (!current) system_overlay_show_focus(0u, "TERMINAL");
  else set_process_focus(nullptr);
  return true;
}

bool native_toggle_focused_mode()
{
  NativeProcess *process = __atomic_load_n(&focused_process, __ATOMIC_ACQUIRE);
  if (!process) return false;
  process->selected_mode = effective_mode(*process) == NARM_RUN_MODE_BACKGROUND
                             ? NARM_RUN_MODE_FOREGROUND
                             : NARM_RUN_MODE_BACKGROUND;
  update_process_audio_pause(*process);
  push_system_event(*process, NARM_EVENT_MODE_CHANGED);
  system_overlay_show_mode(process->pid, process->name,
                           process->selected_mode, effective_mode(*process));
  return true;
}

bool native_close_focused() { return native_request_stop(); }

uint32_t native_focused_pid()
{
  return __atomic_load_n(&scheduler_focused_pid, __ATOMIC_ACQUIRE);
}

bool native_terminal_has_focus() { return terminal_has_focus; }

void native_scheduler_print_diagnostics()
{
  const auto report = [](const String &line)
  {
    Serial.println(line);
    echo_message(line);
  };
  Ps2KeyboardDiagnostics keyboard = {};
  ps2_keyboard_get_diagnostics(keyboard);
  report(String("=>NARM MT-R3 SELFTEST=") +
         (scheduler_selftest_passed ? "OK" : "FAILED"));
  report(" TIMER=" + String(scheduler_timer_started ? "ON" : "OFF") +
         " CORE=" + String((uint32_t)scheduler_timer_core) +
         " TICKS=" + String((uint32_t)scheduler_timer_ticks) +
         " PENDSV=" + String(exception_get_priority(PENDSV_EXCEPTION)));
  report(" SLICES=" + String(scheduler_timeslices) +
         " YIELDS=" + String(scheduler_yields) +
         " COMMANDS=" + String(scheduler_commands_applied));
  report(" PS2 BYTES=" + String(keyboard.received_bytes) +
         " RECOVERIES=" + String(keyboard.recoveries) +
         " LAST=0x" + String(keyboard.last_transition, HEX));
  report(" ALT F2/F3/F4=" + String(keyboard.shortcut_presses[0]) + "/" +
         String(keyboard.shortcut_presses[1]) + "/" +
         String(keyboard.shortcut_presses[2]));
  report(" REQUESTS=" + String(__atomic_load_n(&scheduler_command_requests[0], __ATOMIC_ACQUIRE)) + "/" +
         String(__atomic_load_n(&scheduler_command_requests[1], __ATOMIC_ACQUIRE)) + "/" +
         String(__atomic_load_n(&scheduler_command_requests[2], __ATOMIC_ACQUIRE)) +
         " PENDING=0x" + String((uint32_t)__atomic_load_n(&narm_arch_scheduler_commands, __ATOMIC_ACQUIRE), HEX));
}

uint32_t native_task_count() { return native_running_processes(); }

bool native_task_info(uint32_t index, NativeTaskInfo &info)
{
  uint32_t seen = 0;
  for (uint32_t i = 0; i < NARM_MAX_PROCESSES; ++i)
  {
    NativeProcess &process = processes[i];
    if (process.state == NATIVE_PROCESS_EMPTY || process.state == NATIVE_PROCESS_EXITED)
      continue;
    if (seen++ != index) continue;
    info.pid = process.pid;
    info.state = process.state;
    info.selected_mode = process.selected_mode;
    info.effective_mode = effective_mode(process);
    info.focused = process.focused;
    info.memory_bytes = process.text.bytes + process.data.bytes + process.stack.bytes;
    info.cpu_slices = process.cpu_slices;
    memcpy(info.name, process.name, sizeof(info.name));
    return true;
  }
  return false;
}

bool native_task_set_mode(uint32_t pid, NativeRunMode mode)
{
  if (mode > NARM_RUN_MODE_BACKGROUND) return false;
  NativeProcess *process = find_process_by_pid(pid);
  if (!process) return false;
  process->selected_mode = mode;
  update_process_audio_pause(*process);
  push_system_event(*process, NARM_EVENT_MODE_CHANGED);
  system_overlay_show_mode(process->pid, process->name,
                           process->selected_mode, effective_mode(*process));
  if (!process->focused && effective_mode(*process) == NARM_RUN_MODE_FOREGROUND &&
      process->state == NATIVE_PROCESS_READY)
    process->state = NATIVE_PROCESS_SUSPENDED;
  else if (process->state == NATIVE_PROCESS_SUSPENDED)
    process->state = NATIVE_PROCESS_READY;
  return true;
}

bool native_task_focus(uint32_t pid)
{
  if (usb_storage_active()) return false;
  NativeProcess *process = find_process_by_pid(pid);
  if (!process) return false;
  set_process_focus(process);
  return true;
}

bool native_task_close(uint32_t pid)
{
  NativeProcess *process = find_process_by_pid(pid);
  if (!process) return false;
  if (!process->close_requested)
  {
    process->close_requested = true;
    process->close_deadline_ms = millis() + 250u;
    push_system_event(*process, NARM_EVENT_CLOSE_REQUESTED);
  }
  return true;
}
