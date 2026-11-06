# MPC-2350: Nano GPU terminal bring-up

This first GPU firmware displays a **40x30 monochrome terminal** over the Tang
Nano 20K HDMI output. PS/2 terminal input, command history and scrolling remain.
NARM program launching is disabled until its GPU API is implemented. PS/2 mouse
input is retained; the graphical mouse cursor is deferred.

VGA scanout, its three PIO programs and DMA channels, both RP pixel buffers,
half-resolution/depth/stamp buffers, CPU 2D/3D rasterizers and pixel cursor/overlay
compositors have been removed. The small terminal character grid is retained;
the general terminal allocation arena is 64 KiB and contains no graphics partitions.

`gpu_display.cpp` drives the current 16-wire bus with GPIO, using at least
1 us per clock phase. It claims no PIO or DMA resources. `nano_gpu.cpp` checks
the N20K-SYS-0.2 capabilities and replies, uploads the font once as a 64 KiB
RGB565 heap texture, and uses GPU `COPY_RECT`, `FENCE` and `PRESENT` commands.
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

1. Install the full-board Nano GPU image and compile/install this RP firmware with
   signal links isolated during programming. Connect only while both boards are off.
2. Power normally and connect HDMI. Expect the MPC-2350 banner, black text on white,
   a prompt and an underscore cursor. Type a command such as `SD_INFO` or `SYS_MEM`.
3. USB Serial at 115200 mirrors terminal messages and accepts commands. `GPU_STATUS`
   prints link state, presentation count, physical RESET_N/WREADY/RVALID levels
   and the last startup fault snapshot; `GPU_RESET` performs a hard GPU reset,
   starts a fresh session, reuploads the font and redraws the terminal.

A byte transfer times out after 250 ms. Any failed transfer or invalid reply
latches the client offline and releases data, with WR_N/RD_N/ALE_N HIGH and
GPU_RESET_N **HIGH**. Keeping reset released lets video timing continue while
the failed host session stays offline. The previous failure path asserted reset
LOW, which also stopped HDMI video; that behavior was removed.
No partial packet is retried. Correct the connection/image and use `GPU_RESET`
to hard-reset and retry. `GPU BOOT` reports the last captured startup flags and
core/link fault codes; it is a saved snapshot, not a new live status read.
Electrical margins, clock rate and physical display operation remain unverified.
The slow first full redraw is intentional; this version prioritizes bring-up.

## Host checks

`python tests/ps2/run_tests.py` runs nine input checks. For the actual firmware
packet/terminal client, first build the Nano GPU oracle objects with its
`make gpu-docs-test`, then run:

```text
python tests/gpu/run_tests.py --gpu <Nano-20K-GPU-checkout> --compiler <matching-g++>
```

The GPIO host mock compiles the actual board glue and checks that write/read
timeouts, startup faults and malformed replies leave reset HIGH, strobes
inactive and data released. It also checks reset pulse polarity and retained
startup diagnostics. GPIO/time are stubbed; electrical behavior is not tested.

The GPU client test reads every pixel back through packets and checks font upload, full
redraw, idle behavior, edits across swaps, scrolling, invalid glyphs and latched
transport/reply failures. It uses the clocked C++ GPU software oracle, not FPGA
RTL or physical hardware. The portable driver and font compile as strict C++17
with warnings treated as errors in that check.
