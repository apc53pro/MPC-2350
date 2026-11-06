#include "program_runtime.h"
#include "terminal.h"

// NARM GPU integration is a later milestone. No software video fallback.
int8_t launch_program_sd(const String &) {
  echo_message("=>NARM IS DISABLED IN THE GPU TERMINAL BUILD.");
  return -1;
}
int8_t launch_program_fs(const String &) {
  echo_message("=>NARM IS DISABLED IN THE GPU TERMINAL BUILD.");
  return -1;
}
