#include "gpu_ui.h"

#include "mouse_runtime.h"
#include "nano_gpu.h"

void gpu_ui_prepare_terminal(NanoGpu &gpu) {
  MouseState mouse;
  mouse_runtime_get_state(mouse);
  gpu.terminal_pointer(mouse.x, mouse.y, mouse.connected);
}
