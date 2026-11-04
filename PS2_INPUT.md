# PS/2 keyboard, mouse and firmware cursor

## Wiring (RP2350B GPIO numbers)

| Signal | GPIO | PS/2 mini-DIN contact |
| --- | ---: | ---: |
| Mouse clock | 1 | 5 |
| Mouse data | 26 | 1 |
| Mouse ground | GND | 3 |
| Mouse supply | board 5 V | 4 |
| Existing keyboard clock | 28 | 5 on keyboard connector |
| Existing keyboard data | 27 | 1 on keyboard connector |

GPIO1 and GPIO26 were the two isolated unused GPIOs. This keeps GPIO9–15
and GPIO29–46 continuously unused. GPIO1 is no longer available for UART0 RX,
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
NARM API 1.13 adds interface 0x0101 (mouse v1.0, capability bit 5) without
changing the existing context layout. Its state, event queue, raw motion,
cursor visibility and relative mode calls are declared in Compiler_For_NARM/narm.tch.
Input goes only to the focused process. Focus loss/exit restores cursor policy
and clears old input; buttons held during a focus change are suppressed until
released. Raw motion is captured before screen clipping, independently of the
event queue. Relative mode freezes desktop coordinates and hides the cursor.

Command transmission uses bounded host-to-device transactions with interrupts
enabled. An absent mouse gets three initialization attempts, then becomes
quiet until a new power-on BAT/ID sequence or mouse_runtime_reconnect().
No recurring 20 ms mouse probe runs indefinitely during normal rendering.

Bad/partial frames and FIFO stalls request recovery and a brief clock inhibit.
Before a flash erase/program parks Core 1, both PS/2 clocks are inhibited.
They are released only after external memory is usable again. Device-side
buffering is finite, so this does not promise to preserve unlimited input
generated during an arbitrarily long flash operation.

## Cursor

The firmware embeds the updated MPC-2350_mouse.png at its exact 6×9 size. Its
22 black outline pixels and 12 enclosed white fill pixels are drawn. The 20
exterior white pixels are transparent. The mask excludes only white pixels
connected to the image edge by four-neighbor steps; diagonal outline edges
therefore preserve the enclosed white fill. The hotspot is its top-left pixel.

A Core 1 hardware alarm schedules cursor refreshes no closer than 16,667 us
apart (at most 60 per second). Coordinate updates and cursor targets are not
limited to this rate. Delayed refreshes do not run catch-up bursts. A deferred
refresh can also run at the end of a presentation using the same deadline.
An unchanged target does not redraw the cursor. Each presentation queries the
latched rectangle once; VGA4 retains flat bulk conversion spans above and below
the cursor, and only its at most nine scanout rows take the cursor-aware path.
Half-resolution presentation similarly handles only intersecting row pairs.

Only the 34 opaque cursor pixels and their saved background are touched when the
cursor moves. Full-screen presentations preserve the displayed cursor and
update the underlying saved pixels, so they do not repaint the cursor at the
application's frame rate. Terminal/VGA4, native half-resolution and the system
overlay use the same compositor. Application backing buffers contain no cursor.

## Resources after startup

| Block | State machines used | Instruction slots used |
| --- | ---: | ---: |
| PIO0 VGA | 3 | 32/32 |
| PIO1 PS/2 | 2 | 32/32 |
| PIO2 I2S | 1 | 8/32 |

Six state machines remain physically unused; three have room for additional
programs without rearranging VGA, subject to each block's instruction budget.
Cursor timing claims one unused hardware alarm. No extra DMA channel or full
framebuffer is allocated for the cursor. VGA's fixed state machines are now
marked claimed in the SDK allocator.

## Hardware verification

For Arduino IDE, the installed `Arduino/libraries/FatFS/src/FatFSFlash.cpp`
must match the project's `vendor-libraries/FatFS/src/FatFSFlash.cpp`. Both copies
were updated together for this change; the IDE selects the installed copy.

1. Build in Arduino IDE with the existing RP2350B board settings, or use the
   project's existing build_firmware.ps1 script.
2. Confirm normal keyboard typing, extended keys, make/break handling and Alt+F2/F3/F4.
3. Connect the mouse to GPIO1/26; verify motion direction, all screen edges and buttons.
4. Move over static terminal text, animated VGA4/half-resolution scenes and overlays;
   check that no trails remain and the original background is restored.
5. Exercise audio/rendering while moving the mouse, then flash file writes while typing.

Host tests cover PIO instruction execution, wire-level command handshakes,
mouse decoding and initialization, exact cursor pixels, clipping, compositor
presentation paths, refresh cadence, timer wrap and concurrent cursor/presenter
access. Physical PS/2 electrical timing still needs verification on the board.

From the MPC-2350_Firmware directory, run `python tests/ps2/run_tests.py`.
The thirteen checks need Python 3 and a host C++17 compiler (g++); they require no
connected hardware. Test executables are written to tests/ps2/.test-build.

The compiler example examples/narmcc_mouse_test.tc exercises clicks, wheel
resizing and relative motion (R toggles, Escape exits). Its companion .narm file is in the firmware's
data directory. Mouse API details are in Compiler_For_NARM/NARMCC_README.md and
NATIVE_ARM_RUNTIME_SPEC.md. Wheel deltas are available through both mouse events
and accumulated raw motion. Basic mice continue to supply movement and
left/right/middle buttons with zero wheel delta.

For a keyboard freeze, send NARM DIAG twice through USB Serial Monitor. Compare
NARM-CORE1 LOOPS/AGE_MS/STAGE and NARM-KEY POLLS/PARSED/QUEUED/POLL_STAGE.
POLL_STAGE is 0 idle/returned, 1 transport poll, 2 mouse poll, 3 resync handling,
4 queue read, or 5 scan-code decode. These supplement the coarse Core 1 stage
(2 means its keyboard service). IRQ RX can continue while foreground parsing
is stalled, so a moving mouse does not by itself prove the Core 1 loop is alive.

NARM DIAG also prints NARM-PS2 and per-port KEY/MOUSE lines with PIO enable
bits, instruction address, FIFO level, divider/configuration, flash-pause state,
GPIO functions and wire levels. CLK and DATA display pin/level/host-output
(for example 28/1/0 means GPIO28 high and released). These are read-only
snapshots taken only on request, with no periodic reset or extra idle polling.

For incorrect mouse movement, NARM DIAG also prints NARM-MOUSE counters, the
last 16 NARM-MOUSE-RX byte records, and four NARM-MOUSE-PACKET records. RX shows
the byte and validity, time, decoder position, device ID, wire idle marker,
streaming/recovery and
initialization state. PACKET shows the original bytes (least significant byte
first), selected format, decoded dx/dy/wheel and resulting screen coordinates.
Move horizontally while the fault is visible and capture these lines; compare
with a capture while movement is normal if the fault is intermittent.

The trace uses fixed SRAM rings (16 bytes and 4 packets), without allocation,
serial output in an interrupt, changes to pointer sensitivity, or per-pixel
work. Core 0's snapshot makes at most three attempts and returns TRACE_BUSY if
Core 1 is mid-write, so a stopped writer cannot hang the diagnostic command.
