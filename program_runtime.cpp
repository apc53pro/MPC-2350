#include "program_runtime.h"

#include "native_runtime.h"
#include "terminal.h"

namespace
{
int8_t report_native_launch(NarmStatus status)
{
  if (status != NARM_OK)
  {
    echo_message("=>NARM ERROR " + String((int32_t)status) + ": " +
                 String(narm_status_string(status)));
    return -1;
  }

  echo_message("=>NARM PROGRAM STARTED AS PID " +
               String(native_focused_pid()) + ".");
  return 1;
}

int8_t launch_chain(const String &initial_path, uint32_t initial_storage)
{
  const NarmStatus status = initial_storage == 1u
                              ? native_run_sd(initial_path)
                              : native_run_fs(initial_path);
  return report_native_launch(status);
}
} // namespace

int8_t launch_program_sd(const String &program_path)
{
  return launch_chain(program_path, 1u);
}

int8_t launch_program_fs(const String &program_path)
{
  return launch_chain(program_path, 0u);
}
