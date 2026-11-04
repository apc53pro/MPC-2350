#pragma once
#include <thread>
#define __not_in_flash_func(name) name
inline unsigned int get_core_num() { return 1u; }
inline void tight_loop_contents() { std::this_thread::yield(); }
