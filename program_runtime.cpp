#include "program_runtime.h"
#include "native_runtime.h"
#include "terminal.h"

namespace {
int8_t report_native_launch(NarmStatus status) {
  if (status != NARM_OK) {
    echo_message("=>NARM ERROR " + String((int32_t)status) + ": " +
                 String(narm_status_string(status)));
    return -1;
  }
  echo_message("=>NARM PROGRAM STARTED AS PID " + String(native_focused_pid()) + ".");
  return 1;
}
}

int8_t launch_program_sd(const String &path) {
  return report_native_launch(native_run_sd(path));
}

int8_t launch_program_fs(const String &path) {
  return report_native_launch(native_run_fs(path));
}
