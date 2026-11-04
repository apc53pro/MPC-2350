#ifndef MOUSE_INPUT_STATE_H
#define MOUSE_INPUT_STATE_H

#include "native_abi.h"
#include <limits.h>

// Pure policy/queue state. The runtime holds its short cross-core lock for
// every access. Nothing here allocates memory or calls an application.
class MouseInputState
{
public:
  static constexpr uint32_t kCapacity = 32;

  void reset() { *this = MouseInputState(); }
  bool relative_mode() const { return relative_; }
  uint32_t owner() const { return owner_; }

  void focus(uint32_t pid)
  {
    if(owner_ == pid) return;
    owner_ = pid;
    relative_ = false;
    cursor_requested_ = true;
    suppressed_buttons_ = physical_buttons_;
    motion_ = {};
    count_ = 0;
    read_ = 0;
    dropped_ = 0;
  }

  void release(uint32_t pid) { if(pid && pid == owner_) focus(0); }

  bool set_cursor(uint32_t pid, bool visible)
  {
    if(!pid || pid != owner_) return false;
    cursor_requested_ = visible;
    return true;
  }

  bool set_relative(uint32_t pid, bool enabled)
  {
    if(!pid || pid != owner_) return false;
    if(relative_ != enabled)
    {
      relative_ = enabled;
      motion_ = {};
      count_ = read_ = 0;
    }
    return true;
  }

  void state(uint32_t pid, NativeMouseStateV1 &out) const
  {
    out = {0, 0, 0, connected_ ? (uint32_t)NARM_MOUSE_CONNECTED : 0u};
    if(!pid || pid != owner_) return;
    out.x = x_;
    out.y = y_;
    out.buttons = physical_buttons_ & ~suppressed_buttons_;
    out.flags |= NARM_MOUSE_FOCUSED;
    if(relative_) out.flags |= NARM_MOUSE_RELATIVE;
    if(cursor_visible()) out.flags |= NARM_MOUSE_CURSOR_VISIBLE;
  }

  void cursor(int32_t &x, int32_t &y, bool &visible) const
  { x = x_; y = y_; visible = cursor_visible(); }

  bool poll(uint32_t pid, NativeMouseEventV1 &out)
  {
    if(!pid || pid != owner_ || !count_) return false;
    out = events_[read_];
    read_ = (read_ + 1u) % kCapacity;
    --count_;
    return true;
  }

  void read_motion(uint32_t pid, NativeMouseMotionV1 &out)
  {
    out = {};
    if(!pid || pid != owner_) return;
    out = motion_;
    motion_ = {};
  }

  uint32_t dropped(uint32_t pid) const { return pid && pid == owner_ ? dropped_ : 0u; }

  void report(int32_t x, int32_t y, uint32_t buttons, bool connected,
              const NativeMouseMotionV1 &movement, uint32_t timestamp)
  {
    const uint32_t old_buttons = physical_buttons_ & ~suppressed_buttons_;
    const bool old_connected = connected_;
    x_ = x; y_ = y; connected_ = connected;
    physical_buttons_ = connected ? buttons : 0u;
    suppressed_buttons_ &= physical_buttons_;
    const uint32_t new_buttons = physical_buttons_ & ~suppressed_buttons_;
    if(!owner_) return;
    motion_.dx = add(motion_.dx, movement.dx);
    motion_.dy = add(motion_.dy, movement.dy);
    motion_.wheel = add(motion_.wheel, movement.wheel);
    NativeMouseEventV1 event = {0, x, y, movement.dx, movement.dy,
                                movement.wheel, new_buttons,
                                old_buttons ^ new_buttons, timestamp};
    if(movement.dx || movement.dy) event.type |= NARM_MOUSE_EVENT_MOVE;
    if(new_buttons & ~old_buttons) event.type |= NARM_MOUSE_EVENT_PRESS;
    if(old_buttons & ~new_buttons) event.type |= NARM_MOUSE_EVENT_RELEASE;
    if(movement.wheel) event.type |= NARM_MOUSE_EVENT_WHEEL;
    if(connected != old_connected)
      event.type |= connected ? NARM_MOUSE_EVENT_CONNECT : NARM_MOUSE_EVENT_DISCONNECT;
    if(event.type) enqueue(event);
  }

private:
  static int32_t add(int32_t a, int32_t b)
  {
    const int64_t sum = (int64_t)a + b;
    return sum > INT32_MAX ? INT32_MAX : (sum < INT32_MIN ? INT32_MIN : (int32_t)sum);
  }
  bool cursor_visible() const { return connected_ && cursor_requested_ && !relative_; }
  uint32_t index(uint32_t offset) const { return (read_ + offset) % kCapacity; }

  void enqueue(const NativeMouseEventV1 &event)
  {
    if(count_ && event.type == NARM_MOUSE_EVENT_MOVE &&
       events_[index(count_ - 1u)].type == NARM_MOUSE_EVENT_MOVE)
    {
      NativeMouseEventV1 &last = events_[index(count_ - 1u)];
      const int32_t dx = add(last.dx, event.dx), dy = add(last.dy, event.dy);
      last = event;
      last.dx = dx; last.dy = dy;
      return;
    }
    if(count_ == kCapacity)
    {
      uint32_t move = 0;
      while(move < count_ && events_[index(move)].type != NARM_MOUSE_EVENT_MOVE) ++move;
      ++dropped_;
      if(move < count_)
      {
        for(uint32_t i = move; i + 1u < count_; ++i) events_[index(i)] = events_[index(i + 1u)];
        --count_;
      }
      else if(event.type == NARM_MOUSE_EVENT_MOVE) return;
      else
      {
        // A finite queue cannot retain unlimited clicks. Explicitly notify
        // the client to cancel gestures and reconcile the current state.
        dropped_ += count_ - 1u; // All queued events are discarded, not just one.
        count_ = read_ = 0;
        NativeMouseEventV1 resync = event;
        resync.type = NARM_MOUSE_EVENT_RESYNC;
        resync.dx = resync.dy = resync.wheel = 0;
        resync.changed_buttons = 0;
        events_[count_++] = resync;
      }
    }
    events_[index(count_)] = event;
    ++count_;
  }

  NativeMouseEventV1 events_[kCapacity] = {};
  NativeMouseMotionV1 motion_ = {};
  uint32_t read_ = 0, count_ = 0, dropped_ = 0, owner_ = 0;
  uint32_t physical_buttons_ = 0, suppressed_buttons_ = 0;
  int32_t x_ = 320, y_ = 240;
  bool connected_ = false, relative_ = false, cursor_requested_ = true;
};

#endif
