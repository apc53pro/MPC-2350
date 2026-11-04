#pragma once
#include <atomic>
#include <stdint.h>
using uint = unsigned int;
inline std::atomic<uint32_t> cursor_test_now{0u};
inline void (*cursor_test_alarm_callback)(uint) = nullptr;
inline uint32_t time_us_32() { return cursor_test_now.load(std::memory_order_relaxed); }
inline uint64_t get_absolute_time() { return time_us_32(); }
inline uint64_t delayed_by_us(uint64_t base, uint32_t delay) { return base + delay; }
inline int hardware_alarm_claim_unused(bool) { return 0; }
inline void hardware_alarm_set_callback(uint, void (*callback)(uint)) { cursor_test_alarm_callback = callback; }
inline bool hardware_alarm_set_target(uint, uint64_t) { return false; }
