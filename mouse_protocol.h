#ifndef MOUSE_PROTOCOL_H
#define MOUSE_PROTOCOL_H

#include "mouse_runtime.h"
#include <limits.h>

// Independent of Arduino/PIO so framing and coordinate handling can be tested
// on the host. Only Core 1's receiver accesses a decoder during streaming.
namespace ps2mouse
{
static constexpr int32_t kScreenWidth = 640;
static constexpr int32_t kScreenHeight = 480;

static inline int32_t clamp_coordinate(int32_t value, int32_t limit)
{
  return value < 0 ? 0 : (value >= limit ? limit - 1 : value);
}

static inline int32_t decode_axis(uint8_t value, bool negative, bool overflow)
{
  // On overflow the exact distance is lost, but the signed bound preserves
  // movement in that direction instead of silently dropping the axis.
  if(overflow) return negative ? -256 : 255;
  // Some mice retain an old sign bit on an axis with zero movement.
  if(!value) return 0;
  return (int32_t)value - (negative ? 256 : 0);
}

struct Decoder
{
  uint8_t bytes[4] = {};
  uint8_t count = 0;
  uint8_t device_id = 0;
  bool recovering = false;

  void reset()
  {
    count = 0;
    recovering = false;
  }

  void invalidate()
  {
    count = 0;
    recovering = true;
  }

  void set_device_id(uint8_t id)
  {
    device_id = id;
    reset();
  }

  bool feed(uint8_t byte, bool after_idle, MouseState &state,
            NativeMouseMotionV1 *motion = nullptr, bool update_position = true)
  {
    // At 100 Hz, a wire idle interval separates reports even if their bytes
    // are delivered in a single FIFO drain. Never use a fresh header as data
    // for the previous report or look for headers inside a damaged report.
    if(after_idle) reset();
    if(recovering) return false;
    if(count == 0 && !(byte & 0x08u))
    {
      invalidate();
      return false;
    }

    bytes[count++] = byte;
    const uint8_t packet_length = (device_id == 3 || device_id == 4) ? 4 : 3;
    if(count != packet_length) return false;
    count = 0;

    const uint8_t flags = bytes[0];
    // Sign is the ninth bit in the header, not bit 7 of the movement byte.
    // A positive movement of 200 is valid and must not become -56.
    // IntelliMouse reserves the overflow bits. Only standard reports use them.
    const bool standard = packet_length == 3;
    const int32_t screen_dx = decode_axis(bytes[1], flags & 0x10u, standard && (flags & 0x40u));
    const int32_t screen_dy = -decode_axis(bytes[2], flags & 0x20u, standard && (flags & 0x80u));
    if(motion) *motion = {screen_dx, screen_dy, 0};
    if(update_position)
    {
      state.x = clamp_coordinate(state.x + screen_dx, kScreenWidth);
      state.y = clamp_coordinate(state.y + screen_dy, kScreenHeight);
    }
    state.buttons = flags & 7u;

    if(packet_length == 4)
    {
      int32_t dz;
      if(device_id == 4)
      {
        const uint8_t nibble = bytes[3] & 15u;
        dz = (int32_t)nibble - ((nibble & 8u) ? 16 : 0);
        state.buttons |= ((uint32_t)bytes[3] & 0x30u) >> 1;
      }
      else
      {
        dz = (int32_t)bytes[3] - ((bytes[3] & 0x80u) ? 256 : 0);
      }
      if(dz > 0 && state.wheel > INT32_MAX - dz) state.wheel = INT32_MAX;
      else if(dz < 0 && state.wheel < INT32_MIN - dz) state.wheel = INT32_MIN;
      else state.wheel += dz;
      if(motion) motion->wheel = dz;
    }

    ++state.reports;
    return true;
  }
};
}

#endif
