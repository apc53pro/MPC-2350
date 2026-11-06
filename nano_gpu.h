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
};

class NanoGpu {
public:
  bool begin(NanoGpuTransport transport, const uint16_t font[128][16]);
  bool terminal_present(const char grid[40][60], int scroll);
  bool read_memory(uint32_t address, uint32_t length, uint8_t *out);
  bool ready() const { return online_; }
  const char *error() const { return error_; }
  uint32_t presents() const { return presents_; }

private:
  NanoGpuTransport bus_{};
  uint32_t sequence_ = 1, epoch_ = 0, front_ = 0, presents_ = 0;
  bool online_ = false, first_frame_ = true;
  const char *error_ = "not initialized";
  char displayed_[1200]{}; // Character metadata, not pixels.
  bool fail(const char *message);
  bool read_bytes(uint8_t *out, uint32_t count);
  bool send(uint16_t opcode, const uint8_t *payload, uint32_t count, uint32_t &sequence);
  bool reply(uint16_t opcode, uint32_t sequence, uint32_t token,
             uint8_t *extra, uint32_t count);
  bool upload(uint32_t address, const uint8_t *data, uint32_t count);
  bool copy(uint32_t target, uint32_t source_x, uint32_t source_y,
            uint32_t x, uint32_t y, uint32_t width, uint32_t height, bool font);
  bool fence();
};

#endif
