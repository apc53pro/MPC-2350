#include "mouse_runtime.h"
#include "mouse_protocol.h"
#include "ps2_pio_transport.h"
#include "mouse_cursor.h"
#include "mouse_input_state.h"

#include "hardware/sync.h"
#include "pico/platform.h"
#include "pico/time.h"

namespace
{
constexpr uint8_t kResponseQueueSize = 16;
constexpr uint8_t kResponseQueueMask = kResponseQueueSize - 1;
constexpr uint32_t kRetryUs = 2000000;
constexpr uint32_t kAckTimeoutUs = 250000;
constexpr uint32_t kSelfTestTimeoutUs = 1500000;
constexpr uint8_t kMaxResetAttempts = 3;

enum class InitState : uint8_t { RetryWait, Quiet, NeedSend, AwaitAck, AwaitBat, AwaitId, AwaitWheelId, Ready };
enum class Command : uint8_t {
  Reset = 0xff, Defaults = 0xf6, SetRate = 0xf3, RateValue = 0,
  ReadId = 0xf2, Stream = 0xea, Enable = 0xf4
};
constexpr uint8_t kWheelRates[] = {200, 100, 80};

// Streaming writes occur only on Core 1. Foreground state changes mask its IRQs
// briefly, so there is always a single snapshot writer. All shared fields are
// atomics: the sequence check does not rely on racing ordinary C++ accesses.
struct PublishedState
{
  uint32_t sequence = 0;
  int32_t x = 320;
  int32_t y = 240;
  uint32_t buttons = 0;
  int32_t wheel = 0;
  uint32_t reports = 0;
  uint32_t connected = 0;
} g_published;

MouseState g_state = {320, 240, 0, 0, 0, false};
ps2mouse::Decoder g_decoder;
uint8_t g_streaming = 0;
struct ReceivedByte
{
  uint8_t data;
  bool after_idle;
};
ReceivedByte g_responses[kResponseQueueSize] = {};
uint8_t g_response_head = 0;
uint8_t g_response_tail = 0;
uint8_t g_response_error = 0;
bool g_initialized = false;
bool g_polling = false;
InitState g_init_state = InitState::RetryWait;
Command g_command = Command::Reset;
uint8_t g_command_resends = 0;
uint8_t g_reset_attempts = 0;
bool g_wake_bat_pending = false;
uint32_t g_wake_deadline_us = 0;
uint32_t g_deadline_us = 0;
bool g_skip_wheel_probe = false;
bool g_probing_wheel = false;
uint8_t g_wheel_rate_step = 0;
MouseInputState g_input;
MouseTraceBuffer g_trace;
uint32_t g_input_lock = 0;

class InputGuard
{
public:
  InputGuard() : irq_(save_and_disable_interrupts())
  {
    uint32_t expected = 0;
    while(!__atomic_compare_exchange_n(&g_input_lock, &expected, 1u, false,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) expected = 0;
  }
  ~InputGuard()
  {
    __atomic_store_n(&g_input_lock, 0u, __ATOMIC_RELEASE);
    restore_interrupts(irq_);
  }
  InputGuard(const InputGuard &) = delete;
  InputGuard &operator=(const InputGuard &) = delete;
private:
  uint32_t irq_;
};

void publish_cursor_locked()
{
  int32_t x, y;
  bool visible;
  g_input.cursor(x, y, visible);
  mouse_cursor_set_target(x, y, visible);
}

static void __not_in_flash_func(publish_state_locked)(const NativeMouseMotionV1 &motion,
                                                       uint32_t timestamp)
{
  g_input.report(g_state.x, g_state.y, g_state.buttons, g_state.connected, motion, timestamp);
  __atomic_fetch_add(&g_published.sequence, 1u, __ATOMIC_SEQ_CST);
  __atomic_store_n(&g_published.x, g_state.x, __ATOMIC_RELAXED);
  __atomic_store_n(&g_published.y, g_state.y, __ATOMIC_RELAXED);
  __atomic_store_n(&g_published.buttons, g_state.buttons, __ATOMIC_RELAXED);
  __atomic_store_n(&g_published.wheel, g_state.wheel, __ATOMIC_RELAXED);
  __atomic_store_n(&g_published.reports, g_state.reports, __ATOMIC_RELAXED);
  __atomic_store_n(&g_published.connected, g_state.connected ? 1u : 0u, __ATOMIC_RELAXED);
  __atomic_fetch_add(&g_published.sequence, 1u, __ATOMIC_RELEASE);
  publish_cursor_locked();
}

static void publish_state()
{
  InputGuard guard;
  publish_state_locked({}, time_us_32());
}

static void __not_in_flash_func(process_stream_byte)(ReceivedByte received)
{
  InputGuard guard;
  const uint32_t now = time_us_32();
  NativeMouseMotionV1 motion = {};
  if(g_decoder.feed(received.data, received.after_idle, g_state, &motion, !g_input.relative_mode()))
  {
    uint32_t raw = (uint32_t)g_decoder.bytes[0] | ((uint32_t)g_decoder.bytes[1] << 8) |
                   ((uint32_t)g_decoder.bytes[2] << 16);
    if(g_decoder.device_id == 3 || g_decoder.device_id == 4) raw |= (uint32_t)g_decoder.bytes[3] << 24;
    g_trace.record_packet({now, raw, g_decoder.device_id,
                           motion.dx, motion.dy, motion.wheel, g_state.x, g_state.y});
    publish_state_locked(motion, now);
  }
}

void discard_responses()
{
  const uint32_t irq_state = save_and_disable_interrupts();
  __atomic_store_n(&g_response_tail, __atomic_load_n(&g_response_head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
  __atomic_store_n(&g_response_error, 0u, __ATOMIC_RELEASE);
  restore_interrupts(irq_state);
}

bool pop_response(ReceivedByte &byte)
{
  const uint8_t tail = __atomic_load_n(&g_response_tail, __ATOMIC_RELAXED);
  if(tail == __atomic_load_n(&g_response_head, __ATOMIC_ACQUIRE)) return false;
  byte = g_responses[tail];
  __atomic_store_n(&g_response_tail, (uint8_t)((tail + 1u) & kResponseQueueMask), __ATOMIC_RELEASE);
  return true;
}

void retry_later(uint32_t now_us)
{
  // The rate sequence can change packet length even if its ID reply is lost.
  // Reset to a known format before falling back, with no repeated probing.
  if(g_command == Command::SetRate || g_command == Command::RateValue ||
     g_command == Command::ReadId)
  {
    if(!g_skip_wheel_probe && g_reset_attempts >= kMaxResetAttempts)
      g_reset_attempts = kMaxResetAttempts - 1u; // Reserve one basic fallback.
    g_skip_wheel_probe = true;
  }
  g_probing_wheel = false;
  g_wheel_rate_step = 0;
  const uint32_t irq_state = save_and_disable_interrupts();
  __atomic_store_n(&g_streaming, 0u, __ATOMIC_RELEASE);
  g_decoder.reset();
  g_state.connected = false;
  g_state.buttons = 0;
  publish_state();
  restore_interrupts(irq_state);
  discard_responses();
  g_command = Command::Reset;
  g_command_resends = 0;
  // An absent mouse must not take 20 ms out of Core 1 every two seconds
  // forever. After a few attempts, only a new power-on BAT/ID or an explicit
  // reconnect request rearms command transmission.
  g_init_state = g_reset_attempts >= kMaxResetAttempts ? InitState::Quiet : InitState::RetryWait;
  g_wake_bat_pending = false;
  g_deadline_us = now_us + kRetryUs;
}

void next_command(Command command)
{
  g_command = command;
  g_command_resends = 0;
  g_init_state = InitState::NeedSend;
}

void enable_streaming()
{
  const uint32_t irq_state = save_and_disable_interrupts();
  g_decoder.reset();
  g_state.buttons = 0;
  g_state.connected = true;
  publish_state();
  restore_interrupts(irq_state);
  g_reset_attempts = 0;
  g_init_state = InitState::Ready;
}

void process_response(uint8_t byte, uint32_t now_us)
{
  if(g_init_state == InitState::Quiet)
  {
    if(byte == 0xaau)
    {
      g_wake_bat_pending = true;
      g_wake_deadline_us = now_us + kAckTimeoutUs;
    }
    else if(g_wake_bat_pending && (int32_t)(now_us - g_wake_deadline_us) < 0 &&
            (byte == 0 || byte == 3 || byte == 4))
    {
      g_wake_bat_pending = false;
      g_reset_attempts = 0;
      g_skip_wheel_probe = false;
      next_command(Command::Reset);
    }
    else g_wake_bat_pending = false;
  }
  else if(g_init_state == InitState::AwaitAck)
  {
    if(byte == 0xfeu)
    {
      if(g_command_resends++ < 2) g_init_state = InitState::NeedSend;
      else retry_later(now_us);
    }
    else if(byte == 0xfcu || byte == 0xfdu)
    {
      retry_later(now_us);
    }
    else if(byte == 0xfau)
    {
      switch(g_command)
      {
        case Command::Reset:
          g_init_state = InitState::AwaitBat;
          g_deadline_us = now_us + kSelfTestTimeoutUs;
          break;
        case Command::Defaults:
          if(g_skip_wheel_probe) next_command(Command::Stream);
          else
          {
            g_probing_wheel = true;
            g_wheel_rate_step = 0;
            next_command(Command::SetRate);
          }
          break;
        case Command::SetRate: next_command(Command::RateValue); break;
        case Command::RateValue:
          if(!g_probing_wheel) next_command(Command::Stream);
          else if(++g_wheel_rate_step < sizeof(kWheelRates)) next_command(Command::SetRate);
          else next_command(Command::ReadId);
          break;
        case Command::ReadId:
          g_init_state = InitState::AwaitWheelId;
          g_deadline_us = now_us + kAckTimeoutUs;
          break;
        case Command::Stream: next_command(Command::Enable); break;
        case Command::Enable: enable_streaming(); break;
      }
    }
  }
  else if(g_init_state == InitState::AwaitBat)
  {
    if(byte == 0xaau)
    {
      g_init_state = InitState::AwaitId;
      g_deadline_us = now_us + kAckTimeoutUs;
    }
    else if(byte == 0xfcu || byte == 0xfdu) retry_later(now_us);
  }
  else if(g_init_state == InitState::AwaitId)
  {
    if(byte == 0 || byte == 3 || byte == 4)
    {
      g_decoder.set_device_id(byte);
      next_command(Command::Defaults);
    }
    else retry_later(now_us);
  }
  else if(g_init_state == InitState::AwaitWheelId)
  {
    if(byte == 0 || byte == 3 || byte == 4)
    {
      // ID 0: basic; ID 3: wheel; ID 4: wheel with extra buttons.
      // Select the actual post-negotiation format, not the reset-time ID.
      g_decoder.set_device_id(byte);
      g_probing_wheel = false;
      next_command(Command::SetRate); // Restore normal 100 Hz reporting.
    }
    else retry_later(now_us);
  }
}
}

void mouse_runtime_init()
{
  const uint32_t irq_state = save_and_disable_interrupts();
  { InputGuard guard; g_input.reset(); }
  g_trace.reset();
  __atomic_store_n(&g_streaming, 0u, __ATOMIC_RELEASE);
  g_state = {320, 240, 0, 0, 0, false};
  g_decoder.set_device_id(0);
  g_response_head = g_response_tail = g_response_error = 0;
  g_command = Command::Reset;
  g_command_resends = 0;
  g_reset_attempts = 0;
  g_wake_bat_pending = false;
  g_skip_wheel_probe = g_probing_wheel = false;
  g_wheel_rate_step = 0;
  g_init_state = InitState::RetryWait;
  // Allow the mouse's power-on self-test before requesting a known reset.
  g_deadline_us = time_us_32() + 500000u;
  g_initialized = true;
  publish_state();
  restore_interrupts(irq_state);
}

void mouse_runtime_reconnect()
{
  if(!g_initialized) return;
  g_reset_attempts = 0;
  retry_later(time_us_32());
  g_skip_wheel_probe = false; // Reconnect may attach a different mouse.
  g_deadline_us = time_us_32();
}

void __not_in_flash_func(mouse_runtime_receive)(uint8_t byte, bool valid, bool after_idle)
{
  const uint32_t trace_status = (uint32_t)g_decoder.count | ((uint32_t)g_decoder.device_id << 8) |
      (g_decoder.recovering ? 1u << 16 : 0u) |
      (__atomic_load_n(&g_streaming, __ATOMIC_ACQUIRE) ? 1u << 17 : 0u) |
      (after_idle ? 1u << 18 : 0u) |
      ((uint32_t)g_init_state << 24);
  g_trace.record_byte(time_us_32(), byte, valid, trace_status);
  if(!valid)
  {
    if(__atomic_load_n(&g_streaming, __ATOMIC_ACQUIRE))
    {
      g_decoder.invalidate();
      // A lost button-release packet must not leave a button latched down.
      g_state.buttons = 0;
      publish_state();
    }
    else
    {
      g_decoder.reset();
      __atomic_store_n(&g_response_error, 1u, __ATOMIC_RELEASE);
    }
    return;
  }

  if(__atomic_load_n(&g_streaming, __ATOMIC_ACQUIRE))
  {
    process_stream_byte({byte, after_idle});
    return;
  }

  const uint8_t head = __atomic_load_n(&g_response_head, __ATOMIC_RELAXED);
  const uint8_t next = (uint8_t)((head + 1u) & kResponseQueueMask);
  if(next == __atomic_load_n(&g_response_tail, __ATOMIC_ACQUIRE))
  {
    __atomic_store_n(&g_response_error, 1u, __ATOMIC_RELEASE);
    return;
  }
  g_responses[head] = {byte, after_idle};
  __atomic_store_n(&g_response_head, next, __ATOMIC_RELEASE);
}

void mouse_runtime_poll()
{
  if(!g_initialized || g_polling) return;
  g_polling = true;
  const uint32_t now_us = time_us_32();
  if(__atomic_exchange_n(&g_response_error, 0u, __ATOMIC_ACQ_REL)) retry_later(now_us);

  ReceivedByte byte;
  for(uint8_t budget = 0; budget < kResponseQueueSize &&
      g_init_state != InitState::Ready && pop_response(byte); ++budget)
    process_response(byte.data, now_us);

  if(g_init_state == InitState::Ready && !__atomic_load_n(&g_streaming, __ATOMIC_ACQUIRE))
  {
    // Bytes queued after F4's ACK are mouse reports. Drain them in order
    // before the IRQ can send a newer byte directly to the decoder.
    const uint32_t irq_state = save_and_disable_interrupts();
    if(__atomic_exchange_n(&g_response_error, 0u, __ATOMIC_ACQ_REL)) retry_later(now_us);
    else
    {
      while(pop_response(byte)) process_stream_byte(byte);
      __atomic_store_n(&g_streaming, 1u, __ATOMIC_RELEASE);
    }
    restore_interrupts(irq_state);
  }

  if(g_init_state == InitState::RetryWait && (int32_t)(now_us - g_deadline_us) >= 0)
  {
    discard_responses();
    next_command(Command::Reset);
  }
  else if((g_init_state == InitState::AwaitAck || g_init_state == InitState::AwaitBat ||
           g_init_state == InitState::AwaitId || g_init_state == InitState::AwaitWheelId) &&
          (int32_t)(now_us - g_deadline_us) >= 0)
  {
    retry_later(now_us);
  }

  if(g_init_state == InitState::NeedSend)
  {
    if(g_command == Command::Reset && !g_command_resends) ++g_reset_attempts;
    g_init_state = InitState::AwaitAck;
    g_deadline_us = time_us_32() + kAckTimeoutUs;
    const uint8_t command_byte = g_command == Command::RateValue
        ? (g_probing_wheel ? kWheelRates[g_wheel_rate_step] : 100u)
        : (uint8_t)g_command;
    if(!ps2_pio_send_byte(Ps2Port::Mouse, command_byte)) retry_later(time_us_32());
  }
  g_polling = false;
}

void mouse_runtime_get_state(MouseState &state)
{
  uint32_t before;
  do
  {
    before = __atomic_load_n(&g_published.sequence, __ATOMIC_ACQUIRE);
    if(before & 1u) continue;
    state.x = __atomic_load_n(&g_published.x, __ATOMIC_RELAXED);
    state.y = __atomic_load_n(&g_published.y, __ATOMIC_RELAXED);
    state.buttons = __atomic_load_n(&g_published.buttons, __ATOMIC_RELAXED);
    state.wheel = __atomic_load_n(&g_published.wheel, __ATOMIC_RELAXED);
    state.reports = __atomic_load_n(&g_published.reports, __ATOMIC_RELAXED);
    state.connected = __atomic_load_n(&g_published.connected, __ATOMIC_RELAXED) != 0;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
  }
  while((before & 1u) || before != __atomic_load_n(&g_published.sequence, __ATOMIC_ACQUIRE));
}

void mouse_input_set_focus(uint32_t pid)
{
  InputGuard guard;
  g_input.focus(pid);
  publish_cursor_locked();
}

bool mouse_runtime_get_trace(MouseTraceSnapshot &trace)
{
  return g_trace.snapshot(trace);
}

void mouse_input_release(uint32_t pid)
{
  InputGuard guard;
  g_input.release(pid);
  publish_cursor_locked();
}

void mouse_input_get_state(uint32_t pid, NativeMouseStateV1 &state)
{
  InputGuard guard;
  g_input.state(pid, state);
}

bool mouse_input_poll_event(uint32_t pid, NativeMouseEventV1 &event)
{
  InputGuard guard;
  return g_input.poll(pid, event);
}

void mouse_input_read_motion(uint32_t pid, NativeMouseMotionV1 &motion)
{
  InputGuard guard;
  g_input.read_motion(pid, motion);
}

bool mouse_input_set_cursor_visible(uint32_t pid, bool visible)
{
  InputGuard guard;
  const bool allowed = g_input.set_cursor(pid, visible);
  if(allowed) publish_cursor_locked();
  return allowed;
}

bool mouse_input_set_relative_mode(uint32_t pid, bool enabled)
{
  InputGuard guard;
  const bool allowed = g_input.set_relative(pid, enabled);
  if(allowed) publish_cursor_locked();
  return allowed;
}

uint32_t mouse_input_dropped_events(uint32_t pid)
{
  InputGuard guard;
  return g_input.dropped(pid);
}
