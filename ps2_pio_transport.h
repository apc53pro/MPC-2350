#ifndef PS2_PIO_TRANSPORT_H
#define PS2_PIO_TRANSPORT_H

#include <stdint.h>

enum class Ps2Port : uint8_t { Keyboard = 0, Mouse = 1 };
// Called on Core 1. In normal reception this runs in the PIO interrupt.
// valid=false means that a partial frame, parity error or FIFO loss occurred.
// after_idle is measured on the wire before the start bit of a valid byte.
using Ps2ReceiveCallback = void (*)(uint8_t byte, bool valid, bool after_idle);

bool ps2_pio_begin(uint8_t keyboard_clock, uint8_t keyboard_data,
                   uint8_t mouse_clock, uint8_t mouse_data,
                   Ps2ReceiveCallback keyboard_callback,
                   Ps2ReceiveCallback mouse_callback);
void ps2_pio_poll();

struct Ps2PioPortDiagnostics {
  uint32_t sm, pc, rx_level, clkdiv, execctrl, pinctrl;
  uint32_t clock_pin, data_pin, clock_high, data_high;
  uint32_t clock_output, data_output, clock_function, data_function;
  uint32_t transmitting, recovering;
};
struct Ps2PioDiagnostics {
  uint32_t ready, flash_paused, ctrl, fdebug, irq_enable, irq_pending, program_offset;
  Ps2PioPortDiagnostics port[2];
};
// Read-only, on-demand hardware snapshot from either core. Does not drain
// FIFOs, reset a receiver, release clocks, or add work to the idle poll.
void ps2_pio_get_diagnostics(Ps2PioDiagnostics &out);

// Sends one host command, including its wire-level ACK. Interrupts remain
// enabled while waiting, and all waits share a 20 ms deadline. The separate
// protocol ACK byte (FA) is received normally through the callback.
bool ps2_pio_send_byte(Ps2Port port, uint8_t byte);

// Core-1 hooks around a flash operation that will park the CPU with IRQs off.
// Pause must run before parking; resume must run after flash is readable again.
void ps2_pio_flash_pause();
void ps2_pio_flash_resume();

#endif
