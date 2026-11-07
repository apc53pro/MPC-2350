#ifndef NATIVE_RUNTIME_H
#define NATIVE_RUNTIME_H

#include <Arduino.h>

#include "native_image.h"

static const uint32_t NARM_MAX_PROCESSES = 4u;
// The target Waveshare board has 8 MiB PSRAM. Retained process infrastructure
// reserves 6 MiB; GPU display storage no longer occupies RP2350 PSRAM.
static const uint32_t NARM_PSRAM_ARENA_BYTES = 6u * 1024u * 1024u;
// Keep this deliberately bounded: the audio and filesystem stacks still need
// ordinary internal heap at run time. 32 KiB gives four typical 8 KiB native
// stacks guaranteed low-latency storage while leaving over 100 KiB unreserved.
static const uint32_t NARM_SRAM_ARENA_BYTES = 32u * 1024u;

enum NativeProcessState : uint8_t
{
  NATIVE_PROCESS_EMPTY = 0,
  NATIVE_PROCESS_LOADING,
  NATIVE_PROCESS_READY,
  NATIVE_PROCESS_RUNNING,
  NATIVE_PROCESS_WAITING,
  NATIVE_PROCESS_SUSPENDED,
  NATIVE_PROCESS_EXITED,
  NATIVE_PROCESS_FAULTED
};

struct NativeTaskInfo
{
  uint32_t pid;
  NativeProcessState state;
  NativeRunMode selected_mode;
  NativeRunMode effective_mode;
  bool focused;
  uint32_t memory_bytes;
  uint32_t cpu_slices;
  char name[64];
};

NarmStatus native_run_sd(const String &path);
NarmStatus native_run_fs(const String &path);

NarmStatus native_last_status();
int32_t native_last_exit_status();
uint32_t native_running_processes();
// Core 0 terminal playback must leave NARM-reserved mixer channels alone.
bool native_audio_voice_reserved(uint8_t voice);
bool native_request_stop();
bool native_request_focus_next();
bool native_request_toggle_mode();
void native_scheduler_begin();
void native_scheduler_poll();
bool native_serial_dispatch(const char *line);
void native_scheduler_print_diagnostics();
void native_memory_trace_enable(bool enabled);
enum NativeCore1Stage : uint32_t
{
  NARM_CORE1_FLASH = 1u, NARM_CORE1_KEYBOARD = 2u,
  NARM_CORE1_AUDIO_BEGIN = 3u, NARM_CORE1_AUDIO_FIRST = 4u,
  NARM_CORE1_GRAPHICS = 5u, NARM_CORE1_AUDIO_LAST = 6u,
  NARM_CORE1_IDLE = 7u
};
void native_core1_checkpoint(NativeCore1Stage stage);
bool native_focus_next();
bool native_toggle_focused_mode();
bool native_close_focused();
uint32_t native_focused_pid();
bool native_terminal_has_focus();
uint32_t native_task_count();
bool native_task_info(uint32_t index, NativeTaskInfo &info);
bool native_task_set_mode(uint32_t pid, NativeRunMode mode);
bool native_task_focus(uint32_t pid);
bool native_task_close(uint32_t pid);

#endif
