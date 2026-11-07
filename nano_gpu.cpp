#include "nano_gpu.h"
#include <string.h>

namespace {
constexpr uint32_t kHeap = 0x258000u;
constexpr uint32_t kMemory = 0x800000u;
void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void put32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8));
}
uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
}

bool NanoGpu::fail(const char *message) {
  online_ = false; error_ = message; return false;
}

bool NanoGpu::read_bytes(uint8_t *out, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i)
    if (!bus_.read(bus_.user, out + i)) return fail("read timeout");
  return true;
}

bool NanoGpu::send(uint16_t opcode, const uint8_t *payload,
                   uint32_t count, uint32_t &sequence) {
  if (count > 1024 || sequence_ == UINT32_MAX) return fail("packet limit");
  uint8_t header[16] = {};
  sequence = sequence_++;
  put32(header, 0x5550474eu); header[5] = 2;
  put16(header + 6, opcode); put32(header + 8, count);
  put32(header + 12, sequence);
  for (uint8_t byte : header)
    if (!bus_.write(bus_.user, byte)) return fail("write timeout");
  for (uint32_t i = 0; i < count; ++i)
    if (!bus_.write(bus_.user, payload[i])) return fail("write timeout");
  return true;
}

bool NanoGpu::await_reply() {
  // A reply exists only after every earlier command retires, so its first
  // byte may lag far behind a 250 ms byte transfer. Poll sideband status:
  // bit 4 = reply available; bit 3 = core fault, bit 7 = transport fault (the
  // RTL emits no reply after a fault, so stop instead of waiting it out).
  const uint32_t start = bus_.now_ms(bus_.user);
  for (;;) {
    uint8_t flags;
    if (!bus_.read_register(bus_.user, 1, &flags)) return fail("status read timeout");
    const uint32_t waited = bus_.now_ms(bus_.user) - start;
    if (flags & 0x10) {
      last_wait_ms_ = waited;
      if (waited > max_wait_ms_) max_wait_ms_ = waited;
      return true;
    }
    if (flags & 0x88) return fail("GPU fault while awaiting reply");
    if (waited > kReplyDeadlineMs) return fail("reply wait timeout");
  }
}

bool NanoGpu::reply(uint16_t opcode, uint32_t sequence, uint32_t token,
                    uint8_t *extra, uint32_t count) {
  uint8_t header[16], common[12];
  awaited_opcode_ = opcode; awaited_sequence_ = sequence;
  if (!await_reply()) return false;
  if (!read_bytes(header, sizeof(header))) return false;
  if (get32(header) != 0x5550474eu || header[4] != 0 || header[5] != 2 ||
      (uint16_t)(header[6] | ((uint16_t)header[7] << 8)) != (opcode | 0x8000u) ||
      get32(header + 8) != 12 + count || get32(header + 12) != sequence)
    return fail("invalid reply header");
  if (!read_bytes(common, sizeof(common))) return false;
  const uint32_t epoch = get32(common);
  if (!epoch || epoch != epoch_ || get32(common + 4) != 0 ||
      get32(common + 8) != token) return fail("GPU epoch/status/token mismatch");
  return read_bytes(extra, count);
}

bool NanoGpu::upload(uint32_t address, const uint8_t *data, uint32_t count) {
  if (address < kHeap || (uint64_t)address + count > kMemory)
    return fail("heap range");
  while (count) {
    const uint32_t chunk = count > 956 ? 956 : count;
    uint8_t *payload = packet_;
    put32(payload, address); put32(payload + 4, chunk);
    uint32_t source = 0, size = 8;
    while (source < chunk) {
      const uint32_t descriptor = size++;
      payload[descriptor] = 0;
      for (unsigned slot = 0; slot < 4 && source < chunk; ++slot) {
        uint32_t word = 0;
        for (unsigned b = 0; b < 4 && source < chunk; ++b)
          word |= (uint32_t)data[source++] << (8 * b);
        unsigned code;
        if (!word) code = 0;
        else if ((word & 0xffffff80u) == 0 || (word & 0xffffff80u) == 0xffffff80u) code = 1;
        else if ((word & 0xffff8000u) == 0 || (word & 0xffff8000u) == 0xffff8000u) code = 2;
        else code = 3;
        payload[descriptor] |= (uint8_t)(code << (2 * slot));
        const unsigned bytes = code == 3 ? 4 : code;
        for (unsigned b = 0; b < bytes; ++b) payload[size++] = (uint8_t)(word >> (8 * b));
      }
    }
    uint32_t sequence;
    if (!send(3, payload, size, sequence)) return false;
    address += chunk; data += chunk; count -= chunk;
  }
  return true;
}

bool NanoGpu::begin(NanoGpuTransport transport, const uint16_t font[128][16]) {
  bus_ = transport; online_ = false; known_[0] = known_[1] = false;
  sequence_ = 1; epoch_ = 0; front_ = 0; presents_ = 0;
  error_ = "not ready";
  if (!bus_.write || !bus_.read || !bus_.read_register || !bus_.now_ms || !font)
    return fail("missing transport/font");
  memset(boot_status_, 0, sizeof(boot_status_));
  uint8_t *status = boot_status_;
  uint8_t caps[64];
  // These immutable sideband bytes do not depend on SDRAM initialization.
  // Check the bus fingerprint before interpreting any startup/fault status.
  const uint8_t prefix[4] = {0, 2, 8, 32};
  for (unsigned i = 0; i < 4; ++i) {
    uint8_t byte;
    if (!bus_.read_register(bus_.user, (uint8_t)(64 + i), &byte))
      return fail("capability read timeout");
    if (byte != prefix[i]) return fail("invalid capability readback");
  }
  for (unsigned poll = 0; poll < 2048; ++poll) {
    for (unsigned i = 1; i < 64; ++i)
      if (!bus_.read_register(bus_.user, (uint8_t)i, status + i))
        return fail("status read timeout");
    bool all_ones = true;
    for (unsigned i = 1; i < 64; ++i) all_ones &= status[i] == 0xff;
    if (all_ones) return fail("all-ones startup readback");
    if ((status[1] & 0x88) || get32(status + 32) || status[56])
      return fail("GPU startup fault");
    if (!(status[1] & 1)) continue;
    epoch_ = get32(status + 2); front_ = status[14];
    if (!epoch_ || front_ > 1) return fail("invalid boot state");
    uint32_t sequence;
    if (!send(1, nullptr, 0, sequence) || !reply(1, sequence, 0, caps, 64)) return false;
    if (caps[0] != 0 || caps[1] != 2 || caps[2] != 8 || caps[3] != 32 ||
        get32(caps + 4) != kMemory || get32(caps + 8) != kHeap ||
        get32(caps + 12) != 1024 || get32(caps + 16) != 320 ||
        get32(caps + 52) != 640 || get32(caps + 56) != 480)
      return fail("unsupported GPU capabilities");
    // Decode the immutable bitmap font into a 256x128 RGB565 texture. This
    // prepares an asset once; the GPU copies its glyphs into its own buffers.
    uint8_t *row = row_;
    for (unsigned y = 0; y < 128; ++y) {
      for (unsigned x = 0; x < 256; ++x) {
        const uint16_t bits = font[(y / 16) * 16 + x / 16][y % 16];
        put16(row + 2 * x, (bits & (0x8000u >> (x % 16))) ? 0 : 0xffffu);
      }
      if (!upload(kHeap + y * 512, row, sizeof(row_))) return false;
    }
    if (!fence()) return false;
    online_ = true; error_ = "none"; return true;
  }
  return fail("GPU initialization timeout");
}

bool NanoGpu::copy(uint32_t target, uint32_t source_x, uint32_t source_y,
                   uint32_t x, uint32_t y, uint32_t width, uint32_t height, bool font) {
  uint8_t payload[56] = {};
  put32(payload, font ? 1 : 0); put32(payload + 4, font ? 0 : front_);
  put32(payload + 8, target); put32(payload + 12, source_x);
  put32(payload + 16, source_y); put32(payload + 20, x); put32(payload + 24, y);
  put32(payload + 28, width); put32(payload + 32, height);
  if (font) {
    put32(payload + 36, kHeap); put32(payload + 40, 256);
    put32(payload + 44, 128); put32(payload + 48, 512);
  }
  uint32_t sequence;
  return send(5, payload, sizeof(payload), sequence);
}

bool NanoGpu::fence() {
  uint8_t payload[4]; uint32_t sequence;
  const uint32_t token = sequence_;
  put32(payload, token);
  return send(6, payload, sizeof(payload), sequence) && reply(6, sequence, token, nullptr, 0);
}

bool NanoGpu::terminal_present(const char grid[40][60], int scroll) {
  if (!online_) return false;
  if (!grid || scroll < 0 || scroll > 30) return fail("terminal scroll range");
  char *next = next_;
  for (unsigned y = 0; y < 30; ++y) for (unsigned x = 0; x < 40; ++x) {
    uint8_t c = (uint8_t)grid[x][y + scroll];
    if (c < 32 || c > 159) c = (uint8_t)'?';
    next[y * 40 + x] = (char)c;
  }
  if (known_[front_] && !memcmp(next, contents_[front_], sizeof(next_))) return true;
  const uint32_t back = front_ ^ 1u;
  // The back buffer still shows the frame before last. Choose the cheapest
  // way to make it equal `next`: keep it and draw differing glyphs, or first
  // copy the front buffer shifted up by `shift` rows (0 = aligned; >0 serves
  // scrolling), leaving the bottom `shift` rows as the back already had them.
  // Cells of an unknown buffer always count as different.
  auto cost_of = [&](int shift) {
    unsigned cost = shift < 0 ? 0 : kCopyCost;
    for (unsigned i = 0; i < 1200; ++i) {
      const unsigned row = i / 40;
      bool same;
      if (shift >= 0 && row + (unsigned)shift < 30)
        same = contents_[front_][i + (unsigned)shift * 40] == next[i];
      else
        same = known_[back] && contents_[back][i] == next[i];
      cost += !same;
    }
    return cost;
  };
  int plan = -1;
  unsigned best = cost_of(-1);
  if (known_[front_])
    for (int shift = 0; shift < 30; ++shift) {
      const unsigned cost = cost_of(shift);
      if (cost < best) { best = cost; plan = shift; }
    }
  char *base = base_;
  bool *base_known = base_known_;
  for (unsigned i = 0; i < 1200; ++i) {
    const unsigned row = i / 40;
    if (plan >= 0 && row + (unsigned)plan < 30) {
      base[i] = contents_[front_][i + (unsigned)plan * 40]; base_known[i] = true;
    } else {
      base[i] = contents_[back][i]; base_known[i] = known_[back];
    }
  }
  // The back is invalid from here until this frame is fully drawn.
  known_[back] = false;
  if (plan >= 0 && !copy(back, 0, (uint32_t)plan * 16, 0, 0, 640, (30u - (uint32_t)plan) * 16, false))
    return false;
  for (unsigned i = 0; i < 1200; ++i) {
    if (base_known[i] && base[i] == next[i]) continue;
    const unsigned glyph = (uint8_t)next[i] - 32u;
    if (!copy(back, (glyph % 16) * 16, (glyph / 16) * 16,
              (i % 40) * 16, (i / 40) * 16, 16, 16, true)) return false;
  }
  memcpy(contents_[back], next, sizeof(next_)); known_[back] = true;
  if (!fence()) return false;
  uint8_t payload[8]; uint32_t sequence;
  const uint32_t token = sequence_;
  put32(payload, back); put32(payload + 4, token);
  if (!send(7, payload, sizeof(payload), sequence) ||
      !reply(7, sequence, token, nullptr, 0)) return false;
  front_ = back; ++presents_;
  return true;
}

bool NanoGpu::read_memory(uint32_t address, uint32_t length, uint8_t *out) {
  if (!online_) return false;
  if (!out || !length || length > 512 || (uint64_t)address + length > kMemory)
    return false;
  uint8_t payload[12], *bytes = readback_; uint32_t sequence;
  const uint32_t token = sequence_;
  put32(payload, address); put32(payload + 4, length); put32(payload + 8, token);
  if (!send(8, payload, sizeof(payload), sequence) ||
      !reply(8, sequence, token, bytes, 8 + length)) return false;
  if (get32(bytes) != address || get32(bytes + 4) != length)
    return fail("invalid readback range");
  memcpy(out, bytes + 8, length); return true;
}
