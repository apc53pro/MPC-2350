#ifndef MOUSE_TRACE_H
#define MOUSE_TRACE_H

#include <stdint.h>
#include <string.h>

constexpr uint32_t MOUSE_TRACE_BYTES = 16;
constexpr uint32_t MOUSE_TRACE_PACKETS = 4;

struct MouseByteTrace {
  uint32_t timestamp_us;
  uint32_t value; // Low byte = data; bit 8 = valid frame.
  // Low byte = decoder position; next byte = device ID;
  // bit 16 = recovering, bit 17 = streaming, bit 18 = wire idle before byte;
  // top byte = initialization state.
  uint32_t status;
};
struct MousePacketTrace {
  uint32_t timestamp_us, raw, device_id;
  int32_t dx, dy, wheel, x, y;
};
struct MouseTraceSnapshot {
  uint32_t received, invalid, reports, byte_count, packet_count;
  MouseByteTrace bytes[MOUSE_TRACE_BYTES];
  MousePacketTrace packets[MOUSE_TRACE_PACKETS];
};
static_assert(sizeof(MouseByteTrace) == 12, "Mouse byte trace layout");
static_assert(sizeof(MousePacketTrace) == 32, "Mouse packet trace layout");

// One Core-1 writer, with local IRQs masked if called from foreground code.
// Core 0 reads only atomic words and never waits for the writer or a lock.
class MouseTraceBuffer {
public:
  void reset()
  {
    begin_write();
    put(received_, 0); put(invalid_, 0); put(reports_, 0);
    end_write();
  }

  void record_byte(uint32_t now, uint8_t byte, bool valid, uint32_t status)
  {
    begin_write();
    const uint32_t head = get(received_);
    uint32_t *slot = bytes_[head % MOUSE_TRACE_BYTES];
    put(slot[0], now); put(slot[1], byte | (valid ? 256u : 0u)); put(slot[2], status);
    if(!valid) put(invalid_, get(invalid_) + 1u);
    put(received_, head + 1u);
    end_write();
  }

  void record_packet(const MousePacketTrace &packet)
  {
    uint32_t words[8];
    memcpy(words, &packet, sizeof(words));
    begin_write();
    const uint32_t head = get(reports_);
    for(uint32_t i = 0; i < 8; ++i) put(packets_[head % MOUSE_TRACE_PACKETS][i], words[i]);
    put(reports_, head + 1u);
    end_write();
  }

  bool snapshot(MouseTraceSnapshot &out) const
  {
    // A stopped Core 1 must not make NARM DIAG hang on an odd sequence.
    for(uint32_t attempt = 0; attempt < 3; ++attempt)
    {
      const uint32_t before = __atomic_load_n(&sequence_, __ATOMIC_ACQUIRE);
      if(before & 1u) continue;
      out.received = get(received_); out.invalid = get(invalid_); out.reports = get(reports_);
      out.byte_count = out.received < MOUSE_TRACE_BYTES ? out.received : MOUSE_TRACE_BYTES;
      out.packet_count = out.reports < MOUSE_TRACE_PACKETS ? out.reports : MOUSE_TRACE_PACKETS;
      for(uint32_t i = 0; i < out.byte_count; ++i)
      {
        const uint32_t *slot = bytes_[(out.received - out.byte_count + i) % MOUSE_TRACE_BYTES];
        out.bytes[i] = {get(slot[0]), get(slot[1]), get(slot[2])};
      }
      for(uint32_t i = 0; i < out.packet_count; ++i)
      {
        const uint32_t *slot = packets_[(out.reports - out.packet_count + i) % MOUSE_TRACE_PACKETS];
        uint32_t words[8];
        for(uint32_t j = 0; j < 8; ++j) words[j] = get(slot[j]);
        memcpy(&out.packets[i], words, sizeof(words));
      }
      __atomic_thread_fence(__ATOMIC_ACQUIRE);
      if(before == __atomic_load_n(&sequence_, __ATOMIC_ACQUIRE)) return true;
    }
    return false;
  }

private:
  static uint32_t get(const uint32_t &value) { return __atomic_load_n(&value, __ATOMIC_RELAXED); }
  static void put(uint32_t &target, uint32_t value) { __atomic_store_n(&target, value, __ATOMIC_RELAXED); }
  void begin_write() { __atomic_fetch_add(&sequence_, 1u, __ATOMIC_ACQ_REL); }
  void end_write() { __atomic_fetch_add(&sequence_, 1u, __ATOMIC_RELEASE); }
  uint32_t sequence_ = 0, received_ = 0, invalid_ = 0, reports_ = 0;
  uint32_t bytes_[MOUSE_TRACE_BYTES][3] = {};
  uint32_t packets_[MOUSE_TRACE_PACKETS][8] = {};
};

#endif
