# MPC-2350: Nano GPU terminal bring-up

This first GPU firmware implements a **40x30 monochrome terminal** for the Tang
Nano 20K. The current fixed bus-test FPGA image runs the GPU core/SDRAM at
**6.3 MHz** and intentionally outputs an independent **black 640×480/60 HDMI
raster** at 25.2 MHz pixels / 126 MHz serialization. GPU-rendered pixels are
disconnected, so this image cannot display or qualify the terminal. Restoring
visible GPU output requires a later board clock/video change and new validation.
PS/2 terminal input, command history and scrolling remain.
NARM program launching is disabled until its GPU API is implemented. PS/2 mouse
input is retained; the graphical mouse cursor is deferred.

VGA scanout, its three PIO programs and DMA channels, both RP pixel buffers,
half-resolution/depth/stamp buffers, CPU 2D/3D rasterizers and pixel cursor/overlay
compositors have been removed. The small terminal character grid is retained;
the general terminal allocation arena is 64 KiB and contains no graphics partitions.

`gpu_display.cpp` drives the current 16-wire bus with GPIO, using at least
50 us per clock phase/setup wait (a phase-only ceiling of 10 kHz before API
overhead). It claims no PIO or DMA resources. `nano_gpu.cpp` checks
the N20K-SYS-0.2 capabilities and replies, uploads the font once as a 64 KiB
RGB565 heap texture, and uses GPU `COPY_RECT`, `FENCE` and `PRESENT` commands.
Startup keeps GPU_RESET_N LOW and data released for **1000 ms** before clocking
the reset/release sequence and reading status, allowing for flash configuration.
The first frame copies all characters. Later frames copy front to back, update
changed characters, fence, then wait for the vertical-blank presentation reply.
An unchanged terminal emits no packets. The CPU never rasterizes a screen buffer.

## Build settings

Use Arduino-Pico 5.6.1, **Generic RP2350 / RP2350B**, **16 MB flash (4 MB sketch,
12 MB filesystem)**, **8 MB PSRAM**, **PSRAM CS GPIO47**, PSRAM maximum 104 MHz,
CPU menu 300 MHz, and Optimize3. The sketch retains its existing startup clock
override (`RP2350_TEST_CPU_MHZ`, currently 375); GPIO bus waits are in microseconds.
The existing project-specific FatFS/FatFSUSB libraries are still required.
Firmware compilation and flashing are left to the user for this milestone.

## Wiring and first test

Use the current [GPU control guide](https://github.com/apc53pro/Nano-20K-GPU/blob/main/GPU_DOCUMENTATION/GPU_CONTROL.md#fixed-wiring-layout-core2350b-to-tang-nano-20k)
and its matched full-board FPGA image. GPIO numbers below are RP2350 numbers:

| Signal | RP GPIO |
| --- | ---: |
| HCLK / WR_N | 16 / 17 |
| D0..D7 | 18..25 |
| ABORT / GPU_RESET_N | 35 / 36 |
| ALE_N / RD_N | 37 / 38 |
| WREADY / RVALID | 39 / 40 |

GP39/40 are always RP inputs. GP39 shares the Core LED; no RP LED code drives it.
The three LED-loaded Tang control nets use 12 mA RP drive. Keep the VGA DAC and
Tang LCD disconnected and follow the guide's pulls, series resistors and grounding.
Remove any local GPU-reset release jumper before the RP drives GPU_RESET_N.

The user reports A2 silicon and **one 100 Ω series resistor per signal wire**
(16 total). The added **4.7 kΩ pull-down from GPIO39/WREADY to common GND and
4.7 kΩ pull-down from GPIO40/RVALID to common GND** are now confirmed fitted;
capability reads still return all zero. Other external bias fitment is
unconfirmed. The user subsequently tested the 6.3 MHz / 50 us build and again
reported sixteen zero capability bytes with matching address echoes and reset
HIGH. Slowing the clocks did not change the readback or identify its cause.
A2 E9 can affect weak or floating inputs; the series parts alone
do not provide pull-down bias. E9 is not established as the root cause.
Arduino `digitalRead()` does not remove the silicon leakage. See the GPU guide
and [RP2350 E9 notes](https://arduino-pico.readthedocs.io/en/stable/rp2350.html).

The transport uses Arduino `digitalRead()`, `digitalWrite()`, `pinMode()`,
`delayMicroseconds()`, `delay()` and `micros()`. Controls use Arduino-Pico's
`OUTPUT_12MA`; data uses `OUTPUT` for writes and `INPUT` for reads/release.
GP39/40 stay `INPUT`. Read bytes are assembled from eight `digitalRead()` calls
with RD_N LOW and HCLK HIGH throughout. `GPU DATA` shows the last received byte.
The address echo also uses `digitalRead()`.
The driver checks the fixed sideband capability prefix before polling ready
status. Zero or otherwise incorrect capability bytes report `invalid capability
readback`; they are not treated as an SDRAM initialization timeout.

1. Install the full-board Nano GPU image and compile/install this RP firmware with
   signal links isolated during programming. Connect only while both boards are off.
2. Power normally and connect HDMI. With the fixed 6.3 MHz bus-test FPGA image,
   expect a detected video signal and constant black. The terminal banner is
   mirrored to USB Serial; no visible GPU terminal is expected for this image.
   A successful bus/capability read is the first test, not a display result.
3. USB Serial at 115200 mirrors terminal messages and accepts commands. `GPU_STATUS`
   prints link state, presentation count, physical RESET_N/WREADY/RVALID levels
   and the last startup fault snapshot; `GPU_RESET` performs a hard GPU reset,
   starts a fresh session, reuploads the font and redraws the terminal.
   `GPU_PROBE`, while offline, reads immutable capability registers 64..79
   without submitting packets. Its expected hexadecimal prefix is `0 2 8 20`.
   `GPU ADDRESS` prints the last address byte driven and sensed at the RP data
   pads; mismatches identify a local data-drive problem. Matching echoes do not
   prove continuity across the inter-board wires.
   The same probe runs automatically once after a failed startup, including
   when the serial command loop is disabled locally.

After a completed sixteen-byte capability sweep, `GPU_PROBE` runs the
offline-only **`GPU_SWAPTEST`**, also available as a USB command. A timeout
during the capability sweep skips the automatic test. It refreshes the status snapshot by
reading register `0x01`, then reads requested addresses **`02`, `21`, `23`** and
prints `GPU SWAPTEST: <three HEX bytes>; flags=0x<raw flags>`. It does not decode
or classify the result. After a fresh hard reset with epoch 1 and zero upper
bytes of the fault code:

| Wiring hypothesis | Expected HEX bytes |
| --- | --- |
| Correct D5/D6 order | `1 0 0` |
| Pure D5/D6 interchange in both address and return data | `1 2 40` |

For the interchange, `02` is unchanged, `21` reaches capability register `41`
(`02`), and `23` reaches `43` (`20`), returned with bits 5/6 exchanged as `40`.
`1 2 40` strongly supports this hypothesis under those assumptions but is not
a unique fault diagnosis; `1 0 0` does not verify all bus lines. No swap-test
result has yet been reported. The sixteen zero capability bytes neither rule
out firmware nor prove this interchange. Startup can accept register 64's
expected zero; register **65**, expected `02`, is the first prefix mismatch.
All zeros or timeouts keep the held-read voltage test below relevant.

The map remains **D5: GPIO23 / Core P3.9 → Tang J5.5 / FPGA pin 42** and
**D6: GPIO24 / Core P3.12 → Tang J5.6 / FPGA pin 41**. The driver still indexes
data as `GPIO18 + bit_index`; the local RP address echo cannot verify the Tang
endpoints. `GPU_SWAPTEST` submits no packet and refuses while online or while
a read is held. It releases the bus when complete or on timeout; a timeout
identifies the register. It needs no FPGA reflash, constraint or clock change.

For a stable voltage test while offline, send **`GPU_HOLD`**. It first releases
the bus with RD_N/WR_N/ALE_N HIGH and data pins as inputs, then samples RVALID
after 50 us at HCLK LOW and after 50 us at HCLK HIGH. The `GPU IDLE` values
`RVALID_LOWCLK` / `RVALID_HIGHCLK` should both be **0** while RD_N is inactive.
An incorrect result could indicate a clock-linked RVALID miswire; that cause
has not been confirmed. The diagnostic continues even if this idle check fails.
The single idle rising edge sends no packet.

It then freshly selects
register `0x41`, whose expected byte is **`0x02`**, and holds **HCLK HIGH / RD_N
LOW**, with WR_N/ALE_N HIGH, reset HIGH and all RP data pins as inputs. The
read remains held even if the byte is wrong or RVALID times out after 250 ms;
an address-write/WREADY timeout releases the bus. `GPU HOLD` reports the result,
and `GPU BUS` prints RP-side control levels. The client stays offline and no
command packet is submitted or retried.

Measure **D1 at Tang J6.9 / FPGA pin 28 and RP GPIO19 / Core P3.5**, on both sides
of the existing 100 Ω resistor, relative to common GND. A correct held read has
D1 HIGH (about 3.3 V) and the other seven data bits LOW. At the Tang end,
RESET_N pin71, HCLK pin77, WR_N pin15 and ALE_N pin17 should be HIGH; RD_N pin16
and ABORT pin72 LOW; RVALID pin74 HIGH and WREADY pin73 LOW. These are expected
levels, not measured results. Keep the two fitted 4.7 kΩ handshake pull-downs.

`GPU_STATUS` rereads held data without clock edges. `GPU_PROBE` and
`GPU_SWAPTEST` refuse while held. Send **`GPU_RELEASE`** to end the read and
release the bus, or **`GPU_RESET`**
to clear the hold and perform normal hard initialization. This diagnostic needs
no FPGA reflash or clock change; use the existing 6.3 MHz bus-test image.
The idle WREADY/RVALID values in a normal probe's final log are not its active
transfer levels: printing all sixteen bytes means software sampled both inputs
HIGH during those transfers.

A normal initialization, packet or probe byte transfer times out after 250 ms.
A failed transfer or invalid reply latches the client offline and releases data,
with WR_N/RD_N/ALE_N HIGH and
GPU_RESET_N **HIGH**. Keeping reset released lets video timing continue while
the failed host session stays offline. The previous failure path asserted reset
LOW, which also stopped HDMI video; that behavior was removed.
No partial packet is retried. Correct the connection/image and use `GPU_RESET`
to hard-reset and retry. `GPU BOOT` reports the last captured startup flags and
core/link fault codes; it is a saved snapshot, not a new live status read.
If the capability check fails first, the snapshot is still zero-initialized
and unread; zero `GPU BOOT` fields then do not establish that the GPU has no fault.
The one-second startup allowance is not a configuration-DONE detector. If the
Tang is powered later or reprogrammed while the RP is running, wait for its
configuration to finish and issue `GPU_RESET` (or restart the RP).
An entirely `FF` startup snapshot is reported as `all-ones startup readback`,
not a decoded GPU fault. Use `GPU_PROBE` and check the data wiring before
interpreting fault fields from that invalid snapshot.
Electrical margins, clock rate and physical display operation remain unverified.
The slow first full redraw is intentional; this version prioritizes bring-up.

## Host checks

`python tests/ps2/run_tests.py` runs nine input checks. For the actual firmware
packet/terminal client, first build the Nano GPU oracle objects with its
`make gpu-docs-test`, then run:

```text
python tests/gpu/run_tests.py --gpu <Nano-20K-GPU-checkout> --compiler <matching-g++>
```

The Arduino API host mock compiles the actual board glue and checks all 256
write/read byte patterns, data direction changes, and that write/read
timeouts, startup faults and malformed replies leave reset HIGH, strobes
inactive and data released. It also checks reset pulse polarity and retained
startup diagnostics. A simulated 800 ms FPGA configuration delay checks that
host transfers begin after the one-second guard. GPIO/time are stubbed;
electrical behavior is not tested. The mock models Arduino-Pico clearing the
output latch in `pinMode(OUTPUT)`, so each data bit is written after setting
its output mode while the bus strobes are inactive.

The GPU client test reads every pixel back through packets and checks font upload, full
redraw, idle behavior, edits across swaps, scrolling, invalid glyphs and latched
transport/reply failures. It uses the clocked C++ GPU software oracle, not FPGA
RTL or physical hardware. The portable driver and font compile as strict C++17
with warnings treated as errors in that check.
