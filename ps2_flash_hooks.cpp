#include "ps2_pio_transport.h"

// The vendor flash library calls these before acknowledging its Core 1 park
// and after QMI has been restored. Device-side buffering covers long erases.
extern "C" void fatfs_flash_before_core1_park()
{
  ps2_pio_flash_pause();
}

extern "C" void fatfs_flash_after_core1_park()
{
  ps2_pio_flash_resume();
}
