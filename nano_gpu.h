#ifndef NANO_GPU_H
#define NANO_GPU_H

#include <stdint.h>

// Serialized N20K-SYS-0.2 client. Call only from Core 0; callbacks are bounded
// byte transfers on a freshly reset GPU. No CPU pixel framebuffer is used.
struct NanoGpuTransport {
  void *user;
  bool (*write)(void *, uint8_t);
  bool (*read)(void *, uint8_t *);
  bool (*read_register)(void *, uint8_t, uint8_t *);
  uint32_t (*now_ms)(void *); // Monotonic CPU milliseconds for reply deadlines.
};

class NanoGpu {
public:
  bool begin(NanoGpuTransport transport, const uint16_t font[128][16]);
  bool terminal_present(const char grid[40][60], int scroll);
  void terminal_pointer(int32_t x, int32_t y, bool visible);
  bool get_caps(uint8_t *out);
  bool get_status(uint8_t *out);
  bool write_heap(uint32_t address, const uint8_t *data, uint32_t count);
  bool submit_pass(uint32_t address);
  bool copy_rect(const uint32_t words[14]);
  bool fence(uint32_t token);
  bool present(uint32_t target, uint32_t token);
  bool read_memory(uint32_t address, uint32_t length, uint8_t *out);
  uint32_t back_target() const { return front_ ^ 1u; }
  static constexpr uint32_t kUserHeap = 0x280000u;
  bool ready() const { return online_; }
  const char *error() const { return error_; }
  uint32_t presents() const { return presents_; }
  const uint8_t *boot_status() const { return boot_status_; }
  // Diagnostics: last packet sequence sent, and the reply most recently awaited.
  uint32_t last_sequence() const { return sequence_ - 1; }
  uint16_t awaited_opcode() const { return awaited_opcode_; }
  uint32_t awaited_sequence() const { return awaited_sequence_; }
  // Time from the request until the GPU flagged its reply available.
  uint32_t last_reply_wait_ms() const { return last_wait_ms_; }
  uint32_t max_reply_wait_ms() const { return max_wait_ms_; }
  // Bounded wait for one reply. Long commands legitimately delay it: at the
  // 6.3 MHz bus-test clock a full-screen copy takes well over 250 ms.
  static constexpr uint32_t kReplyDeadlineMs = 10000;

private:
  NanoGpuTransport bus_{};
  uint32_t sequence_ = 1, epoch_ = 0, front_ = 0, presents_ = 0;
  uint16_t awaited_opcode_ = 0;
  uint32_t awaited_sequence_ = 0, last_wait_ms_ = 0, max_wait_ms_ = 0;
  bool await_reply();
  bool online_ = false;
  const char *error_ = "not initialized";
  // Characters each colour buffer currently shows (metadata, not pixels).
  // A buffer is unknown until a frame has been drawn into it this session.
  char contents_[2][1200]{};
  bool known_[2]{};
  struct Pointer {
    int16_t x = -1, y = -1;
    bool visible() const { return x >= 0; }
    bool covers(unsigned cell) const;
    bool operator==(const Pointer &other) const { return x == other.x && y == other.y; }
  };
  Pointer pointer_, drawn_pointer_[2];
  bool pointer_uploaded_ = false;
  // Copy cost in glyph-copy equivalents when choosing how to fill the back.
  static constexpr unsigned kCopyCost = 16;
  // Large work buffers live in the object, never on the stack: each RP2350
  // core has only a 4 KiB stack bank, and Core 0's bank sits directly above
  // Core 1's, so a Core 0 overflow silently corrupts and halts Core 1.
  char next_[1200]{};
  uint8_t packet_[1024]{};
  uint8_t row_[512]{};
  uint8_t readback_[520]{};
  uint8_t boot_status_[64]{}; // Last startup snapshot, also retained on failure.
  bool fail(const char *message);
  bool read_bytes(uint8_t *out, uint32_t count);
  bool send(uint16_t opcode, const uint8_t *payload, uint32_t count, uint32_t &sequence);
  bool reply(uint16_t opcode, uint32_t sequence, uint32_t token,
             uint8_t *extra, uint32_t count);
  bool upload(uint32_t address, const uint8_t *data, uint32_t count);
  bool draw_pointer(uint32_t target);
  bool query(uint16_t opcode, uint8_t *out);
  bool send_copy(const uint32_t words[14]);
  bool copy(uint32_t target, uint32_t source_x, uint32_t source_y,
            uint32_t x, uint32_t y, uint32_t width, uint32_t height, bool font);
  bool wait_fence(uint32_t token);
};

#endif
