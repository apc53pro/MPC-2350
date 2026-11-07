# PS/2 keyboard and mouse input

## Wiring (RP2350B GPIO numbers)

| Signal | GPIO | PS/2 mini-DIN contact |
| --- | ---: | ---: |
| Mouse clock | 1 | 5 |
| Mouse data | 26 | 1 |
| Mouse ground | GND | 3 |
| Mouse supply | board 5 V | 4 |
| Existing keyboard clock | 28 | 5 on keyboard connector |
| Existing keyboard data | 27 | 1 on keyboard connector |

GPIO1 and GPIO26 are dedicated to the mouse. GPU control and handshakes use
GPIO35–40; GPIO9–15 and GPIO29–46 are not a continuous free range.
GPIO1 is no longer available for UART0 RX,
and GPIO26 is no longer available for Wire1 SDA without remapping those buses.
Use the connector's numbered contacts; front/rear views are mirrored. Clock
and data are open-collector lines: use suitable pull-ups and the board's
bidirectional voltage interface. Connect a PS/2-compatible mouse with power off.

## Reception and coordinates

PIO1 uses two claimed state machines and one shared 32-instruction program.
Each state machine samples a complete 11-bit PS/2 frame independently. A
RX-FIFO-not-empty interrupt enabled only on Core 1 drains both FIFOs, validates
start/stop/odd parity and delivers bytes. Core 1 no longer services individual
PS/2 clock edges. Keyboard Set-2 decoding, normalized events and the existing
application keyboard API are retained.

The keyboard IRQ publishes bytes to a single-producer/single-consumer ring
with acquire/release ordering. Foreground parsing does not use Arduino's
noInterrupts()/interrupts() nesting stack. Each poll processes at most 31 bytes,
including when new input arrives during decoding. Overflow/error recovery
flushes uncertain queued input and releases latched keys on the next poll.

The mouse initializes with reset/defaults, then tries the IntelliMouse
200/100/80 sample-rate sequence and reads the device ID. ID 0 retains standard
three-byte reports; ID 3 enables four-byte wheel reports. ID 4, if reported,
uses wheel/extra-button packets. The firmware restores 100 Hz reporting before
stream mode and enable reporting. Optional setup failure triggers a reset and
basic-mode fallback, never an unconfirmed packet-length guess.
Its screen hotspot starts at
(320, 240); each completed report immediately updates coordinates, clamped to
x=0..639 and y=0..479. Mouse-up motion reduces screen y. Movement uses the
header's ninth sign bit, so a valid positive delta of 200 stays positive.
Zero axis bytes remain zero even with a retained sign bit; they must not
produce spurious -256 movements. PIO marks frames that start after about
3 ms of idle clock. At the configured 100 Hz rate, this lets the decoder
abandon a partial report at the next report boundary, even if the FIFO is
drained in a batch. After a framing error it skips the remaining bytes
until the next marked frame.

If a standard report indicates overflow, the affected axis uses its signed
limit. The other axis retains its decoded movement.
Button state and report count are available through mouse_runtime_get_state().
The retained NARM API 1.13 input infrastructure defines interface 0x0101
(mouse v1.0, capability bit 5) without
changing the existing context layout. Its state, event queue, raw motion,
cursor visibility and relative mode calls are declared in Compiler_For_NARM/narm.tch.
Input goes only to the focused process. Focus loss/exit restores cursor policy
and clears old input; buttons held during a focus change are suppressed until
released. Raw motion is captured before screen clipping, independently of the
event queue. Relative mode freezes desktop coordinates and hides the requested
cursor. NARM launching uses the GPU firmware and input services remain
available to the focused process.

Command transmission uses bounded host-to-device transactions with interrupts
enabled. An absent mouse gets three initialization attempts, then becomes
quiet until a new power-on BAT/ID sequence or mouse_runtime_reconnect().
No recurring 20 ms mouse probe runs indefinitely during normal rendering.

Bad/partial frames and FIFO stalls request recovery and a brief clock inhibit.
Before a flash erase/program parks Core 1, both PS/2 clocks are inhibited.
They are released only after external memory is usable again. Device-side
buffering is finite, so this does not promise to preserve unlimited input
generated during an arbitrarily long flash operation.

## Display and resources

The former CPU pixel cursor, saved background, timer and software presentation
paths have been removed. Mouse coordinates and events still update. A GPU mouse
cursor is deferred; the terminal underscore cursor is an ordinary glyph copy.

| Block | State machines used | Instruction slots used |
| --- | ---: | ---: |
| PIO0 | 0 | 0/32 |
| PIO1 PS/2 | 2 | 32/32 |
| PIO2 I2S | 1 | 8/32 |
| Nano GPU transport | 0 | GPIO only |

There is no video DMA or cursor hardware alarm. The GPU owns its scanout.

## Verification

Run `python tests/ps2/run_tests.py` for the nine retained host checks: PIO RX,
transport mock, flash handshakes, mouse decode/runtime, input policy, native
mouse boundary metadata, keyboard queue/parser, and concurrent mouse trace.
Physical PS/2 electrical timing still needs board verification. Applications
that used the earlier framebuffer API need to migrate to [NARM GPU services](NARM_GPU.md).
See README.md for the GPU terminal build settings and hardware test procedure.
