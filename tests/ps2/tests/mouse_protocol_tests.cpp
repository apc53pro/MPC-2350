#include "mouse_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>

static bool packet(ps2mouse::Decoder &decoder, MouseState &state,
                   uint8_t flags, uint8_t x, uint8_t y)
{
  assert(!decoder.feed(flags, true, state));
  assert(!decoder.feed(x, false, state));
  return decoder.feed(y, false, state);
}

int main()
{
  ps2mouse::Decoder decoder;
  MouseState state = {320, 240, 0, 0, 0, true};
  assert(!decoder.feed(0, false, state));
  assert(!decoder.feed(0xfa & ~0x08, false, state));
  assert(state.reports == 0);

  assert(packet(decoder, state, 0x09, 200, 5));
  assert(state.x == 520 && state.y == 235);
  assert(state.buttons == MOUSE_BUTTON_LEFT && state.reports == 1);
  assert(packet(decoder, state, 0x3a, 56, 251));
  assert(state.x == 320 && state.y == 240);
  assert(state.buttons == MOUSE_BUTTON_RIGHT && state.reports == 2);

  assert(packet(decoder, state, 0x08, 255, 255));
  assert(state.x == 575 && state.y == 0);
  assert(packet(decoder, state, 0x08, 255, 0));
  assert(state.x == 639);
  assert(packet(decoder, state, 0x38, 1, 1));
  assert(state.x == 384 && state.y == 255);
  assert(packet(decoder, state, 0x38, 1, 1));
  assert(state.x == 129 && state.y == 479);
  assert(packet(decoder, state, 0x18, 1, 0));
  assert(state.x == 0 && state.y == 479);

  // Overflow preserves direction at the signed limit. The other axis
  // continues to report its full movement and button changes are retained.
  state.x = 20; state.y = 30;
  assert(packet(decoder, state, 0x4d, 100, 5));
  assert(state.x == 275 && state.y == 25 && state.buttons == 5);
  assert(packet(decoder, state, 0xa8, 5, 100));
  assert(state.x == 280 && state.y == 281 && state.buttons == 0);

  // A missing Y byte used to make the next header produce a 248-pixel jump.
  assert(!decoder.feed(0x38, true, state));
  assert(!decoder.feed(0xff, false, state));
  assert(packet(decoder, state, 0x08, 1, 2));
  assert(state.x == 281 && state.y == 279);

  // Four-byte formats follow the negotiated device ID.
  decoder.set_device_id(3);
  assert(!packet(decoder, state, 0x08, 0, 0));
  assert(decoder.feed(0xff, false, state));
  assert(state.wheel == -1);
  decoder.set_device_id(4);
  assert(!packet(decoder, state, 0x08, 0, 0));
  assert(decoder.feed(0x3f, false, state));
  assert(state.wheel == -2 && state.buttons == (MOUSE_BUTTON_BACK | MOUSE_BUTTON_FORWARD));
  state.wheel = INT32_MIN;
  assert(!packet(decoder, state, 0x08, 0, 0));
  assert(decoder.feed(0xf, false, state));
  assert(state.wheel == INT32_MIN);

  // Zero movement with retained sign bits must never become a 256-pixel jump.
  for(uint8_t id : {0, 3, 4})
  {
    decoder.set_device_id(id);
    state = {320, 240, 0, 0, 0, true};
    NativeMouseMotionV1 motion = {};
    assert(!decoder.feed(0x38, true, state, &motion));
    assert(!decoder.feed(0, false, state, &motion));
    bool complete = decoder.feed(255, false, state, &motion);
    if(id) { assert(!complete); complete = decoder.feed(0, false, state, &motion); }
    assert(complete && state.x == 320 && state.y == 241 && motion.dx == 0 && motion.dy == 1);
    assert(!decoder.feed(0x38, true, state, &motion));
    assert(!decoder.feed(255, false, state, &motion));
    complete = decoder.feed(0, false, state, &motion);
    if(id) { assert(!complete); complete = decoder.feed(0, false, state, &motion); }
    assert(complete && state.x == 319 && state.y == 241 && motion.dx == -1 && motion.dy == 0);
  }

  // Loss of any frame in either packet format discards the remaining bytes.
  for(uint8_t id : {0, 3, 4})
    for(unsigned lost = 0; lost < (id ? 4u : 3u); ++lost)
    {
      decoder.set_device_id(id);
      state = {320, 240, 0, 0, 0, true};
      const uint8_t bad[] = {0x08, 0xff, 0x08, 0xff};
      for(unsigned i = 0; i < lost; ++i) assert(!decoder.feed(bad[i], i == 0, state));
      decoder.invalidate();
      for(unsigned i = lost + 1; i < (id ? 4u : 3u); ++i)
        assert(!decoder.feed(bad[i], false, state));
      assert(state.reports == 0 && state.x == 320 && state.y == 240);
      bool complete = packet(decoder, state, 0x08, 3, 4);
      if(id) { assert(!complete); complete = decoder.feed(0xff, false, state); }
      assert(complete && state.reports == 1 && state.x == 323 && state.y == 236);
      assert(state.wheel == (id ? -1 : 0));
    }

  puts("mouse_protocol_tests: all tests passed");
  return 0;
}
