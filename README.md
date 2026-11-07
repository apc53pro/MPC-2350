# MPC-2350 with the Nano GPU

The Tang Nano 20K GPU renders the **40×30 monochrome terminal** and runs
graphics commands from focused NARM applications. It outputs HDMI at
640×480/60, with the FPGA image's
50.4 MHz core/SDRAM clock and 25.2 MHz pixel clock. On 2026-11-07 the user
reported the terminal running on HDMI.
PS/2 terminal input, command history and scrolling remain. The terminal mouse
pointer uses GPU copies of opaque spans from a small RGB565 arrow asset.
Moving or hiding it redraws the glyphs under its old footprint.

VGA scanout, its three PIO programs and DMA channels, both RP pixel buffers,
half-resolution/depth/stamp buffers, CPU 2D/3D rasterizers and pixel cursor/overlay
compositors have been removed. The small terminal character grid is retained;
the general terminal allocation arena is 64 KiB and contains no graphics partitions.

`gpu_display.cpp` uses one PIO2 state machine with GPIO base 16 for byte
transfers in both directions. PIO clocks HCLK, drives the bus strobes and
data direction, and samples WREADY before a write edge and RVALID with the
read byte. Read and write retries take 12 state-machine cycles per HCLK
period. Set `kHostClockHz` in `gpu_display.cpp` to the desired burst rate
in Hz. It defaults to **250 kHz**. The driver computes the PIO divider
from the RP system clock and rounds to a rate at or below the target.
HCLK pauses between byte transactions. Core 0 encodes packets and
exchanges commands and completion words through the PIO FIFOs with a 250 ms
byte deadline. PS/2 remains on PIO1.
`nano_gpu.cpp` checks
the N20K-SYS-0.2 capabilities and replies, uploads the font once as a 64 KiB
RGB565 heap texture, and uses GPU `COPY_RECT`, `FENCE` and `PRESENT` commands.
Startup keeps GPU_RESET_N LOW and data released for **1000 ms** before clocking
the reset/release sequence and reading status, allowing for flash configuration.
The client remembers which characters each of the GPU's two colour buffers
shows. For a changed frame it picks the cheapest way to bring the back buffer
up to date. Either it draws only the glyphs that differ from what that buffer
already holds, or it first copies the front buffer, shifted up by whole text
rows for a scroll, and then draws the rest. It then fences and waits for the
vertical-blank presentation reply. With the pointer hidden, a one-line
scroll typically needs one copy plus the new line of glyphs. An unchanged
terminal emits no packets.

## NARM graphics

The existing SD and flash NARM launch paths run again. The focused application
can submit GPU commands while the terminal retains its text grid. Alt+F2
cycles focus. When the terminal regains focus it restores its frame using
the same GPU session and the current writable back buffer. Focus and mode
notifications appear as terminal messages. NARM apps control their own
graphics and can draw a pointer using PS/2 mouse input.

[NARM GPU interface 1.1](NARM_GPU.md) exposes the GPU through named NARMCC
built-ins for record encoding, drawing, readback, and recovery. Keep realtime
NARM loops running while the GPU works. Check `narm_gpu_frame_ready()` once
per iteration and queue a completed frame with `narm_gpu_try_present()`.
Application allocations start at `0x280000`. The firmware reserves the
lower heap area for terminal assets. Programs share the application heap
and must manage allocation and content across focus changes.

### Stack limit: keep large buffers off the stack

Arduino-Pico gives each RP2350 core a fixed **4 KiB stack** in its own scratch
bank, not the 512 KiB main RAM. Core 0's stack (SCRATCH_Y, 0x20081000–0x20082000)
sits directly above Core 1's (SCRATCH_X, 0x20080000–0x20081000), and nothing
guards the boundary. An earlier version of the frame code kept about 3.6 KiB of
frame buffers, plus a 1 KiB upload packet, as local arrays. When the GPU client
ran, Core 0 overflowed into Core 1's stack. Core 1 halted, so the PS/2 keyboard
stopped after the first frames, and Core 0 sometimes locked up.
`GPU_Isolation_Test` showed Core 1's loop counter freezing in the client stage.
The client's work buffers are now `NanoGpu` members, and its largest stack
frame is 208 bytes. On 2026-11-07 the user reported the keyboard and system
stable after this change. Do not add large local arrays to code that runs on
either core.

## Build settings

Use Arduino-Pico 5.6.1, **Generic RP2350 / RP2350B**, **16 MB flash (4 MB sketch,
12 MB filesystem)**, **8 MB PSRAM**, **PSRAM CS GPIO47**, PSRAM maximum 104 MHz,
CPU menu 300 MHz, and Optimize3. The sketch retains its existing startup clock
override (`RP2350_TEST_CPU_MHZ`, currently 375). The PIO divider is derived
from the applied system clock.
The existing project-specific FatFS/FatFSUSB libraries are still required.
Firmware compilation and flashing are left to the user for this milestone.
If you edit the PIO program, regenerate the checked-in Arduino header with
`pioasm -o c-sdk gpu_bus.pio gpu_bus.pio.h`.

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
other external bias fitment is unconfirmed. The earlier all-zero capability
reads were caused by the open-source FPGA build, not by wiring, E9 or this
firmware; see the result below. A2 E9 can still affect weak or floating inputs,
and the series parts alone do not provide pull-down bias.
Arduino `digitalRead()` does not remove the silicon leakage. See the GPU guide
and [RP2350 E9 notes](https://arduino-pico.readthedocs.io/en/stable/rp2350.html).

Reset, held-read and edge-free drive diagnostics use SIO. The driver restores
the bus to inactive strobes and releases the data pins on timeout.
`GPU DATA` shows the last received byte. The address echo is the PIO sample
of the data pads before the latch edge.
The driver checks the fixed sideband capability prefix before polling ready
status. Zero or otherwise incorrect capability bytes report `invalid capability
readback`; they are not treated as an SDRAM initialization timeout.

1. Install the full-board Nano GPU image and compile/install this RP firmware with
   signal links isolated during programming. Connect only while both boards are off.
2. Power normally, connect HDMI and press the RP reset once the FPGA has
   configured. Expect the 40×30 black-on-white terminal on HDMI; the banner is
   also mirrored to USB Serial.
3. USB Serial at 115200 mirrors terminal messages and accepts commands. `GPU_STATUS`
   prints link state, presentation count, the last and longest reply wait
   (`reply_wait_ms=last/max`), physical RESET_N/WREADY/RVALID levels
   and the last startup fault snapshot; `GPU_REGS` reads the live status
   registers twice, 500 ms apart, and decodes flags, epoch, completed sequence,
   fence/present tokens, cycle counter and fault codes (sideband reads only; it
   also runs automatically after a failed terminal frame); `GPU_RESET` performs a hard GPU reset,
   starts a fresh session, reuploads the font and redraws the terminal.
   `GPU_PROBE`, while offline, reads immutable capability registers 64..79
   without submitting packets. Its expected hexadecimal prefix is `0 2 8 20`.
   `GPU ADDRESS` prints the last address byte driven and sensed at the RP data
   pads; mismatches identify a local data-drive problem. Matching echoes do not
   prove continuity across the inter-board wires.
   The same probe runs automatically once after a failed startup, including
   when the serial command loop is disabled locally. The offline address-path
   diagnostics `GPU_LATCHTEST` and `GPU_DRIVE <hex>` are described below.

After a completed sixteen-byte capability sweep, `GPU_PROBE` runs the
offline-only **`GPU_SWAPTEST`**, also available as a USB command. A timeout
during the capability sweep skips the automatic test. It refreshes the status snapshot by
reading register `0x01`, then reads requested addresses **`02`, `21`, `23`** and
prints
`GPU SWAPTEST: <three HEX bytes>; flags=0x<raw flags>; edges=<4 decimal>; pre=<4 bits>`.
It does not decode or classify the result. After a fresh hard reset, epoch is 1;
status bytes `21` and `23` are the fault code's upper bytes, which the RTL
always reports as zero:

| Wiring hypothesis | Expected HEX bytes |
| --- | --- |
| Correct D5/D6 order | `1 0 0` |
| Pure D5/D6 interchange in both address and return data | `1 2 40` |

For the interchange, `02` is unchanged, `21` reaches capability register `41`
(`02`), and `23` reaches `43` (`20`), returned with bits 5/6 exchanged as `40`.
`1 2 40` strongly supports this hypothesis under those assumptions but is not
a unique fault diagnosis; `1 0 0` does not verify all bus lines. Under an
interchange the raw flags also arrive with bits 5/6 exchanged.

`edges` lists, for registers `01 02 21 23`, how many HCLK rising edges passed
after RD_N went LOW before RVALID was sampled HIGH. `pre` is RVALID sampled
once with RD_N LOW and HCLK LOW, before that first edge. A genuine GPU read
handshake gives **`edges=4 1 1 1; pre=0 0 0 0`**. A register-`01` read always
requests a new status snapshot; its acknowledgement crosses the core clock
domain and returns after the fourth edge in an Icarus simulation of
`nano_host_link`. Capability reads and later status reads answer on the first
edge. A register-`01` count of `1` means RVALID did not come from that
snapshot handshake: for example RVALID following HCLK, or the FPGA latching a
different address. Any `pre` value of `1` means RVALID was HIGH before any read
edge. The board drives the data pins under the same condition that drives
RVALID, so a genuine `4 1 1 1` with zero data points to the data wires or their
Tang endpoints rather than the handshake.

**Reported 2026-11-06, before edge counting:** `GPU CAPS` printed sixteen zeros
and `GPU SWAPTEST: 0 0 0; flags=0x0`, with no timeouts. This matches neither
row above. Register `02` uses neither D5 nor D6, so a pure interchange would
still return `1`. Every byte the RP read was `00`, including capability bytes
`02`, `08` and `20`. A D5/D6 interchange alone therefore does not explain the
fault. Startup can accept register 64's expected zero; register **65**,
expected `02`, is the first prefix mismatch.

**Reported 2026-11-06, with edge counting, after an RP reset (FPGA already
configured):** `GPU SWAPTEST: 0 0 0; flags=0x0; edges=1 1 1 1; pre=0 0 0 0`.
The `GPU_HOLD` idle check gave `0/0`. During a confirmed hold (`held=1`,
RVALID=1, WREADY=0 at the RP), measurements at the Tang pins were: 3V3 3.3 V,
pin 74 (RVALID) 3.3 V, pin 73 (WREADY) 0 V, pin 16 (RD_N) 0.23 V, and all eight
data pins 27, 28, 29, 30, 31, 42, 41, 48 at 0 V; RP GPIO19 was 0.05 V. So the
GPU answered the read and drove `00` where register `0x41` holds `02`. With
the one-edge register-`01` answer, this indicates that the FPGA latched a
different address, in the immediately answered capability range, from the one
the RP sent. That cause is inferred, not yet isolated. After a cold power-up the
first startup instead printed all `FF`, idle WREADY/RVALID HIGH and
`pre=1 1 1 1`: the FPGA had not finished configuration within the 1000 ms wait.

Address-write acceptance cannot reveal a missed latch: during an address write
the GPU's WREADY depends only on D7. Two offline diagnostics isolate the
address path without an FPGA change:

- **`GPU_LATCHTEST`** issues the same reset pulse as startup (32 HCLK edges with
  GPU_RESET_N LOW, release, 64 idle edges) without the 1000 ms configuration
  wait or client startup. It then reads capability registers **`41 42 43 41`**,
  never `40` first, and prints
  `GPU LATCHTEST: <4 HEX bytes>; edges=<4 decimal>; expected=2 8 20 2`.
  `2 8 20 2` means these address writes reached the GPU. `2 2 2 2` means only
  the first address after reset was latched. `0 0 0 0` means the FPGA saw
  addresses other than those sent. These are host-model signatures, not unique
  diagnoses. A timeout names the register and releases the bus.
- **`GPU_DRIVE <hex>`** (one or two hexadecimal digits, no `0x`) releases the
  bus, drives the byte on GPIO18..25, then holds an **address phase without
  its clock edge**: ALE_N and WR_N LOW, RD_N HIGH, HCLK LOW, reset HIGH. With no
  rising edge the GPU latches nothing, and with RD_N HIGH it does not drive the
  data pins. Measure the Tang data pins (D0..D7 = 27, 28, 29, 30, 31, 42, 41,
  48), WR_N pin 15 and ALE_N pin 17 (both LOW), RD_N pin 16 (HIGH), HCLK pin 77
  (LOW) and WREADY pin 73, which the GPU drives as the inverse of the D7 level
  it sees. Use `55` and then `2A` to place every address bit HIGH and LOW.
  `GPU BUS` reports `drive=1` and `GPU DATA` the RP pad readback.
  `GPU_RELEASE` raises WR_N/ALE_N/RD_N before releasing data, still without a
  rising edge; `GPU_RESET` also ends the hold. These are expected levels, not
  measured results.

Both diagnostics submit no packet and refuse while the client is online;
`GPU_PROBE`, `GPU_SWAPTEST` and `GPU_LATCHTEST` refuse while a `GPU_HOLD` or
`GPU_DRIVE` hold is active.

**Result, 2026-11-07:** with the open-flow `nano-gpu.fs`, `GPU_LATCHTEST` gave
`0 0 0 0; edges=4 1 1 1`. `GPU_DRIVE 55` and `2A` delivered every data bit,
HIGH and LOW, to its Tang pin. A pin-identical bus-echo image echoed every
address, and the unchanged GPU RTL built with Gowin EDA reached `READY` with
`GPU_LATCHTEST: 2 8 20 2` and correct capability and status reads. This
firmware, the wiring and the RTL are therefore working. The fault is in the
open-source FPGA implementation of the full GPU design; the Nano-20K-GPU control
guide records the evidence. With the Gowin build, the first terminal frame
completed, and the second then failed with `read timeout` while awaiting its
fence (sequence 1336). `GPU_REGS` showed the core busy on the frame's 640×480
front-to-back copy with no fault codes, the cycle counter running and
`completed` at 1332. A later dump showed `completed=1336` and a reply
available. The GPU was therefore slow, not stuck: at the 6.3 MHz bus-test clock
that copy takes well over the old 250 ms reply-byte wait. Reply waits now poll
status instead (below).

The map remains **D5: GPIO23 / Core P3.9 → Tang J5.5 / FPGA pin 42** and
**D6: GPIO24 / Core P3.12 → Tang J5.6 / FPGA pin 41**. The driver still indexes
data as `GPIO18 + bit_index`; the local RP address echo cannot verify the Tang
endpoints. `GPU_SWAPTEST` submits no packet and refuses while online or while
a read is held. It releases the bus when complete or on timeout; a timeout
identifies the register. It needs no FPGA reflash, constraint or clock change.

For a stable voltage test while offline, send **`GPU_HOLD`**. It first releases
the bus with RD_N/WR_N/ALE_N HIGH and data pins as inputs, then samples RVALID
after one HCLK phase at HCLK LOW and after one phase at HCLK HIGH. The `GPU IDLE` values
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

`GPU_STATUS` rereads held data without clock edges. `GPU_PROBE`,
`GPU_SWAPTEST` and `GPU_LATCHTEST` refuse while held. Send **`GPU_RELEASE`** to end the read and
release the bus, or **`GPU_RESET`**
to clear the hold and perform normal hard initialization. This diagnostic needs
no FPGA reflash or clock change.
The idle WREADY/RVALID values in a normal probe's final log are not its active
transfer levels: printing all sixteen bytes means software sampled both inputs
HIGH during those transfers.

A normal initialization, packet or probe byte transfer times out after 250 ms.
Before reading a reply, the client instead polls sideband register 1 until
bit 4 (reply available) is set. The deadline is
**10 s** on the CPU's `millis()`. It stops at once on bit 3 (core fault) or bit 7
(transport fault), because the RTL emits no reply after a fault. Errors are
`reply wait timeout` and `GPU fault while awaiting reply`. The reply bytes
themselves keep the 250 ms byte timeout.
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
The earlier GPIO firmware was confirmed on hardware. The 25 MHz setting
failed on the user's wired setup. `GPU_STATUS` prints the requested
setting as `hclk_target_hz=`.

## Host checks

`python tests/ps2/run_tests.py` runs nine input checks. For the actual firmware
packet/terminal client, first build the Nano GPU oracle objects with its
`make gpu-docs-test`, then run:

```text
python tests/gpu/run_tests.py --gpu <Nano-20K-GPU-checkout> --compiler <matching-g++>
```

The transport mock checks firmware FIFO and GPIO behavior. The GPU client
test uses the clocked C++ GPU software oracle. It reads each pixel back
through packets and checks full redraws, scrolling, pointer movement,
application commands, terminal restore and latched failures. The driver
and font compile as strict C++17 with warnings treated as errors in that check.
