#include "mouse_trace.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

static void verify(const MouseTraceSnapshot &trace)
{
  assert(trace.byte_count <= MOUSE_TRACE_BYTES && trace.packet_count <= MOUSE_TRACE_PACKETS);
  assert(trace.invalid == (trace.received + 16u) / 17u);
  assert(trace.reports <= trace.received && trace.received - trace.reports <= 1u);
  for(uint32_t i = 0; i < trace.byte_count; ++i)
  {
    const auto &byte = trace.bytes[i];
    const uint32_t sample = trace.received - trace.byte_count + i;
    assert(byte.timestamp_us == sample + 1234u);
    assert((byte.value & 255u) == (sample & 255u));
    assert(bool(byte.value & 256u) == bool(sample % 17u));
    assert(byte.status == 0x20303u);
  }
  for(uint32_t i = 0; i < trace.packet_count; ++i)
  {
    const auto &packet = trace.packets[i];
    const uint32_t sample = trace.reports - trace.packet_count + i;
    assert(packet.timestamp_us == sample + 1234u && packet.raw == sample && packet.device_id == 3);
    assert(packet.dx == -(int32_t)sample && packet.dy == (int32_t)sample);
    assert(packet.wheel == (int32_t)(sample % 3u) - 1);
    assert(packet.x == (int32_t)(sample % 640u) && packet.y == (int32_t)(sample % 480u));
  }
}

int main()
{
  MouseTraceBuffer buffer;
  MouseTraceSnapshot trace = {};
  assert(buffer.snapshot(trace) && trace.received == 0 && trace.reports == 0);
  std::atomic<bool> running{true};
  std::thread writer([&]() {
    for(uint32_t i = 0; i < 100000u; ++i)
    {
      buffer.record_byte(i + 1234u, (uint8_t)i, i % 17u != 0u, 0x20303u);
      buffer.record_packet({i + 1234u, i, 3, -(int32_t)i, (int32_t)i,
          (int32_t)(i % 3u) - 1, (int32_t)(i % 640u), (int32_t)(i % 480u)});
    }
    running.store(false);
  });
  uint32_t successful = 0, busy = 0;
  do
  {
    if(buffer.snapshot(trace)) { verify(trace); ++successful; }
    else ++busy;
  } while(running.load());
  writer.join();
  assert(buffer.snapshot(trace) && trace.received == 100000u && trace.reports == 100000u);
  verify(trace);
  assert(trace.byte_count == 16 && trace.packet_count == 4);
  buffer.reset();
  assert(buffer.snapshot(trace) && trace.byte_count == 0 && trace.packet_count == 0 && trace.invalid == 0);
  std::cout << "Mouse trace tests passed: concurrent consistent snapshots, bounded busy return, ring wrap, signed deltas, reset ("
            << successful << " snapshots, " << busy << " busy).\n";
}
