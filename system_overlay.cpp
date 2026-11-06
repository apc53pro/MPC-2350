#include "system_overlay.h"
#include "terminal.h"

// Process notifications now use terminal text, without saved pixel rows.
void system_overlay_show_focus(uint32_t pid, const char *name) {
  echo_message(pid ? "=>FOCUS " + String(pid) + " " + String(name ? name : "") : "=>SYSTEM TERMINAL");
}
void system_overlay_show_mode(uint32_t pid, const char *, NativeRunMode, NativeRunMode effective) {
  echo_message("=>PID " + String(pid) + (effective == NARM_RUN_MODE_BACKGROUND ? " BACKGROUND" : " FOREGROUND"));
}
