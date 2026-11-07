#ifndef GPU_DISPLAY_H
#define GPU_DISPLAY_H

#include <stdint.h>

bool gpu_display_begin();
void gpu_display_present();
void gpu_display_diagnostics();
void gpu_display_probe();
void gpu_display_swaptest();
void gpu_display_latchtest();
void gpu_display_regs();
void gpu_display_drive(uint8_t byte);
void gpu_display_hold();
void gpu_display_release();

#endif
