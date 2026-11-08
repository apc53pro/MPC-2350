// Test the actual firmware client through the GPU software oracle's clocked
// host interface. This is protocol/rendering evidence, not board qualification.
#include "nano_gpu.h"
#include "font.h"
#include "nano_gpu/hw/system.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <initializer_list>
#include <stdexcept>
#include <vector>

struct Bus {
  nano_gpu::hw::SystemModel model;
  unsigned writes = 0;
  bool write_timeout = false, read_timeout = false, corrupt_reply = false;
  bool expect_reply_stream = false;
  uint8_t last_read_address = 0;
  // Reply-wait fault injection on status register 1: hide "reply available"
  // (with a clock that jumps 1 s per call) or report a transport fault.
  bool hide_reply = false, fake_fault = false;
  uint32_t skew_ms = 0;
  uint32_t now_ms() {
    if (hide_reply) skew_ms += 1000;
    return (uint32_t)(model.system_statistics().core_cycles / 50000) + skew_ms;
  }
  void check(uint64_t start) {
    if (model.faulted()) throw std::runtime_error(model.fault_message());
    if (model.system_statistics().core_cycles - start > 50000000)
      throw std::runtime_error("model timeout");
  }
  void align() {
    const auto &c = model.config().host;
    const auto phase = (c.transfer_phase + 1) % c.clock_divider;
    while (model.host_statistics().cycles % c.clock_divider != phase) model.tick();
  }
  void hold() {
    for (unsigned i = 0; i < model.config().host.hold_cycles; ++i) model.tick();
  }
  bool write(uint8_t byte) {
    if (write_timeout) return false;
    ++writes; align();
    const auto start = model.system_statistics().core_cycles;
    nano_gpu::hw::HostPins pins; pins.write_valid = true; pins.data = byte;
    for (;;) {
      const auto result = model.tick(pins); check(start);
      if (result.write_accepted) { hold(); return true; }
    }
  }
  bool read(uint8_t address, uint8_t *byte) {
    if (!address && read_timeout) return false;
    if (!address && expect_reply_stream) {
      assert(last_read_address == 1);
      expect_reply_stream = false;
    }
    last_read_address = address;
    align();
    const auto start = model.system_statistics().core_cycles;
    nano_gpu::hw::HostPins pins; pins.read = true; pins.register_address = address;
    for (;;) {
      const auto result = model.tick(pins); check(start);
      if (result.read_valid) {
        *byte = result.read_data;
        if (!address && corrupt_reply) { *byte ^= 1; corrupt_reply = false; }
        if (address == 1 && hide_reply) *byte &= (uint8_t)~0x10u;
        if (address == 1 && fake_fault) *byte |= 0x80u;
        hold(); return true;
      }
    }
  }
  NanoGpuTransport transport() {
    return {this,
      [](void *p, uint8_t b) { return static_cast<Bus *>(p)->write(b); },
      [](void *p, uint8_t *b) { return static_cast<Bus *>(p)->read(0, b); },
      [](void *p, uint8_t a, uint8_t *b) { return static_cast<Bus *>(p)->read(a, b); },
      [](void *p) { return static_cast<Bus *>(p)->now_ms(); }};
  }
  void boot() {
    const auto start = model.system_statistics().core_cycles;
    while (!model.ready()) { model.tick(); check(start); }
  }
};

void verify(Bus &bus, NanoGpu &gpu, const char grid[40][60], int scroll,
            int pointer_x = -1, int pointer_y = -1) {
  // SystemModel's color() is a cached diagnostic view updated only by its own
  // convenience driver. Read actual SDRAM through the firmware packet client.
  std::vector<uint16_t> pixels(640 * 480);
  uint8_t bytes[512];
  for (unsigned offset = 0; offset < pixels.size() * 2; offset += sizeof(bytes)) {
    assert(gpu.read_memory((uint32_t)bus.model.front_id() * 0x96000u + offset,
                           sizeof(bytes), bytes));
    for (unsigned i = 0; i < 256; ++i)
      pixels[offset / 2 + i] = (uint16_t)(bytes[2 * i] | ((unsigned)bytes[2 * i + 1] << 8));
  }
  for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 640; ++x) {
    uint8_t c = (uint8_t)grid[x / 16][y / 16 + scroll];
    if (c < 32 || c > 159) c = '?';
    const auto bits = font[c - 32][y % 16];
    uint16_t expected = (bits & (0x8000u >> (x % 16))) ? 0 : 0xffffu;
    const int dx = (int)x - pointer_x, dy = (int)y - pointer_y;
    constexpr const char *arrow[] = {"#", "##", "#+#", "#++#", "#+++#", "#++++#", "#++###", "#####", "##"};
    if (pointer_x >= 0 && dx >= 0 && dy >= 0 && dy < 9 && (size_t)dx < strlen(arrow[dy]))
      expected = arrow[dy][dx] == '+' ? 0xffffu : 0;
    if (pixels[y * 640 + x] != expected) {
      std::cerr << "Pixel mismatch at " << x << ',' << y << ", front=" << bus.model.front_id()
                << ", actual=" << pixels[y * 640 + x] << ", expected=" << expected
                << ", glyph=" << (unsigned)c << '\n';
      throw std::runtime_error("terminal pixel mismatch");
    }
  }
}

void write_words(NanoGpu &gpu, uint32_t address, std::initializer_list<uint32_t> words) {
  std::vector<uint8_t> bytes;
  for (uint32_t word : words)
    for (unsigned b = 0; b < 4; ++b) bytes.push_back((uint8_t)(word >> (8 * b)));
  assert(gpu.write_heap(address, bytes.data(), (uint32_t)bytes.size()));
}

int main() {
  try {
    Bus bus; bus.boot(); NanoGpu gpu;
    assert(gpu.begin(bus.transport(), font));
    assert(bus.model.system_statistics().uploaded_bytes == 65536);
    char grid[40][60]; memset(grid, ' ', sizeof(grid));
    memcpy(grid[0], "A", 1); grid[1][0] = 'B'; grid[39][29] = 'Z';
    assert(gpu.terminal_present(grid, 0)); verify(bus, gpu, grid, 0);
    const auto initial = bus.model.system_statistics().copies_completed;
    assert(initial == 1200 && gpu.presents() == 1);
    const auto writes = bus.writes;
    assert(gpu.terminal_present(grid, 0) && bus.writes == writes);
    grid[1][0] = 'C';
    assert(gpu.terminal_present(grid, 0)); verify(bus, gpu, grid, 0);
    assert(bus.model.system_statistics().copies_completed == initial + 2);
    grid[0][0] = ' '; grid[0][1] = 'D';
    assert(gpu.terminal_present(grid, 0)); verify(bus, gpu, grid, 0);
    grid[0][30] = 'E'; grid[39][59] = 'F'; grid[2][30] = 1;
    assert(gpu.terminal_present(grid, 30)); verify(bus, gpu, grid, 30);
    // Both buffers are tracked: a cursor-style toggle redraws nothing once the
    // back buffer already holds the frame before last.
    grid[5][30] = '_'; assert(gpu.terminal_present(grid, 30)); verify(bus, gpu, grid, 30);
    grid[5][30] = ' '; assert(gpu.terminal_present(grid, 30)); verify(bus, gpu, grid, 30);
    auto copies = bus.model.system_statistics().copies_completed;
    grid[5][30] = '_'; assert(gpu.terminal_present(grid, 30)); verify(bus, gpu, grid, 30);
    assert(bus.model.system_statistics().copies_completed == copies);
    grid[5][30] = ' '; assert(gpu.terminal_present(grid, 30)); verify(bus, gpu, grid, 30);
    assert(bus.model.system_statistics().copies_completed == copies);
    // Scrolling one line is one shifted buffer copy plus the new bottom line.
    for (unsigned x = 0; x < 40; ++x) for (unsigned y = 0; y < 60; ++y)
      grid[x][y] = (char)(33 + (x * 7 + y * 3) % 90);
    assert(gpu.terminal_present(grid, 20)); verify(bus, gpu, grid, 20);
    assert(gpu.terminal_present(grid, 21)); verify(bus, gpu, grid, 21);
    copies = bus.model.system_statistics().copies_completed;
    assert(gpu.terminal_present(grid, 22)); verify(bus, gpu, grid, 22);
    assert(bus.model.system_statistics().copies_completed - copies <= 1 + 40);
    uint8_t bytes[2];
    assert(gpu.read_memory((uint32_t)bus.model.front_id() * 0x96000u, 2, bytes));
    assert((bytes[0] | ((unsigned)bytes[1] << 8)) ==
           ((font['E' - 32][0] & 0x8000u) ? 0u : 0xffffu));
    // Move across glyph boundaries, scroll with the old pointer in the
    // front buffer, then clip at the screen edge and hide it.
    gpu.terminal_pointer(13, 30, true);
    assert(gpu.terminal_present(grid, 22)); verify(bus, gpu, grid, 22, 13, 30);
    gpu.terminal_pointer(33, 50, true);
    assert(gpu.terminal_present(grid, 23)); verify(bus, gpu, grid, 23, 33, 50);
    gpu.terminal_pointer(639, 479, true);
    assert(gpu.terminal_present(grid, 23)); verify(bus, gpu, grid, 23, 639, 479);
    gpu.terminal_pointer(0, 0, false);
    assert(gpu.terminal_present(grid, 23)); verify(bus, gpu, grid, 23);

    // Exercise the same session a focused application uses. Present its
    // cleared back buffer, then let the terminal take the display again.
    uint8_t bank[64];
    assert(gpu.get_caps(bank) && bank[1] == 2);
    assert(gpu.get_status(bank) && bank[14] == bus.model.front_id());
    const uint32_t heap = NanoGpu::kUserHeap, table = heap + 32;
    const uint32_t target = gpu.back_target();
    const uint8_t empty_tiles[2400] = {};
    assert(!gpu.write_heap(heap - 4, empty_tiles, 4) && gpu.ready());
    assert(!gpu.present(target ^ 1u, 100) && gpu.ready());
    assert(gpu.write_heap(table, empty_tiles, sizeof(empty_tiles)));
    write_words(gpu, heap, {0x3153504eu, target, 3, 0x1234, 0xffffff, table, 300, 0});
    assert(gpu.submit_pass(heap) && gpu.fence(100) && gpu.present(target, 101));
    assert(gpu.read_memory(target * 0x96000u, 2, bytes) && bytes[0] == 0x34 && bytes[1] == 0x12);
    assert(gpu.terminal_present(grid, 23)); verify(bus, gpu, grid, 23);
    // A copy into the next back buffer must invalidate the terminal cache.
    const uint32_t sample = table + sizeof(empty_tiles);
    write_words(gpu, sample, {0x12341234u});
    const uint32_t copy[14] = {1, 0, gpu.back_target(), 0, 0, 0, 0, 1, 1, sample, 2, 1, 4, 0};
    assert(gpu.copy_rect(copy));
    grid[0][23] = 'Y';
    assert(gpu.terminal_present(grid, 23)); verify(bus, gpu, grid, 23);
    // An accepted asynchronous present leaves the host free while the GPU
    // waits for vertical blank. It cannot send a duplicate or change ownership
    // until it consumes the matching completion.
    const auto before_async = gpu.presents();
    const auto async_target = gpu.back_target();
    assert(gpu.try_present(async_target, 102) == NanoGpu::FrameReady);
    bus.expect_reply_stream = true;
    const auto async_writes = bus.writes;
    assert(gpu.present_pending() && gpu.back_target() == async_target);
    assert(gpu.try_present(async_target, 103) == NanoGpu::FrameBusy);
    assert(bus.writes == async_writes && gpu.presents() == before_async);
    const auto start = bus.model.system_statistics().core_cycles;
    NanoGpu::FrameResult frame;
    do {
      for (unsigned i = 0; i < 512; ++i) bus.model.tick();
      frame = gpu.frame_ready();
      bus.check(start);
    } while (frame == NanoGpu::FrameBusy);
    assert(frame == NanoGpu::FrameReady && !gpu.present_pending());
    assert(!bus.expect_reply_stream);
    assert(gpu.back_target() == (async_target ^ 1u) && bus.model.front_id() == (int)async_target);
    assert(gpu.presents() == before_async + 1);
    assert(gpu.get_status(bank) && bank[36] == 102);
    const auto &host = bus.model.host_statistics();
    assert(!host.setup_violations && !host.hold_violations &&
           !host.unstable_writes && !host.protocol_faults);
    // A failed transport is latched; it cannot publish a half-finished frame.
    bus.write_timeout = true; grid[3][30] = 'X';
    const auto presents = gpu.presents();
    assert(!gpu.terminal_present(grid, 30) && !gpu.ready());
    assert(gpu.presents() == presents);
    assert(!gpu.terminal_present(grid, 30));
    bus.write_timeout = false;
    bus.model.abort();
    assert(gpu.can_resume() && gpu.resume_after_abort());
    assert(gpu.ready() && gpu.last_sequence() == 0 && gpu.back_target() == (bus.model.front_id() ^ 1u));
    assert(gpu.get_status(bank) && bank[2] == 2 && gpu.last_sequence() == 1);
    for (unsigned mode = 0; mode < 2; ++mode) {
      Bus bad; bad.boot(); NanoGpu client;
      bad.read_timeout = mode == 0; bad.corrupt_reply = mode == 1;
      assert(!client.begin(bad.transport(), font) && !client.ready());
    }
    // Replies are awaited through sideband status, bounded by a CPU deadline,
    // and a fault flag ends the wait at once (the RTL sends no reply after it).
    assert(gpu.max_reply_wait_ms() >= gpu.last_reply_wait_ms());
    for (unsigned mode = 0; mode < 2; ++mode) {
      Bus slow; slow.boot(); NanoGpu client;
      assert(client.begin(slow.transport(), font));
      char text[40][60]; memset(text, ' ', sizeof(text)); text[0][0] = 'Q';
      slow.hide_reply = mode == 0; slow.fake_fault = mode == 1;
      assert(!client.terminal_present(text, 0) && !client.ready() && client.presents() == 0);
      assert(!strcmp(client.error(), mode == 0 ? "reply wait timeout" : "GPU fault while awaiting reply"));
      assert(client.awaited_opcode() == 6); // The frame's fence.
    }
    std::cout << "PASS firmware GPU client: font upload, full terminal, unchanged frame, "
                 "edits across swaps, scroll, glyph fallback, packet readback, latched failures, "
                 "status-polled reply deadline and fault stop, per-buffer glyph tracking "
                 "(zero-copy text cursor toggle), one-copy line scroll, mouse pointer, "
                 "GPU commands, asynchronous presentation, abort recovery, and terminal restore.\n";
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
