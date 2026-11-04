# Sinclair ZX80 NARM — scanline/M1 raster edition (v5)

An experimental 4 KB Sinclair ZX80 emulator for the GP-PICO-OS NARM environment on RP2350. Built from the **working v4** emulator with the v5 authentic-video/3.25 MHz pacing changes retained. This release replaces the *default* screen renderer with an actual emulated Z80 opcode-fetch-driven raster, rather than reconstructing what BASIC *should* show by parsing the display file after execution.

## Install

Put these two files on the SD card (or compatible NARM storage):

- `zx80.narm` (in this ZIP)
- `zx80.rom` (your existing, separately supplied 4,096-byte Sinclair ZX80 ROM)

From GP-PICO-OS:

```
SELECT_PROG_SD "zx80.narm"
RUN
```

The ROM remains external and is **not embedded or redistributed**. The existing loader searches SD and flash root and `/roms`. An 8 KB ZX81 ROM cannot replace a ZX80 ROM without a different emulator implementation.

## How the new video system works

The ZX80 makes its display by executing a display-file mirror from the upper 32 KB address space. Its discrete video logic watches Z80 **M1 opcode fetches**. When A15=1, HALT is not active, and D6 of the byte read is 0, the machine forces a NOP onto the CPU bus but uses the real byte's lower six bits to select a glyph. D7 selects inverted video. The 8-pixel pattern comes from a ROM/refresh lookup using the Z80 **I** register, and an external 3-bit character-line counter that advances on scanline interrupt acknowledgements. HALT (`0x76`) terminates each row. I/O `IN` and `OUT` control vertical synchronization.

This implementation observes these events **while the Z80 is executing**, not after the fact:

1. Records emulated T-states at each Z80 instruction step.
2. Captures real M1 opcode fetches in the display mirror, including M1 prefix fetches, with the correct NOP-injection gate.
3. Reads the exact character bitmap row indexed by I, the intercepted opcode and the external scanline counter. A RAM refresh address uses the I:R refresh bus path.
4. Places each 8-pixel group into the active 256 x 192 raster based on its T-state offset from the preceding interrupt acknowledge (stock-ROM timing calibration: +75 T-states for first character, +4 for each next character).
5. Tracks VSYNC start on even-port `IN`, the end of VSYNC on `OUT`, external character-line counter reset on `OUT`, and horizontal line count on interrupt acknowledgement.
6. Builds a packed RGB332 256 x 192 scanline image, **double-buffered** so the currently-building field cannot tear the displayed field.
7. Presents each completed field in a single `narm_half_blit`, surrounded by a white border; if regular complete video fields stop arriving, the host display goes black.

The original 50 Hz PAL ZX80 ROM produces approximately 310 lines per field, with the first 56 lines forming the top border and the next 192 forming the active image. The rest are border/VSync. M1/INT time positions are derived from an execution trace of the 4 KB ZX80 ROM, not from host keyboard state. Original-RAM character glyphs and inverse characters display at the measured positions; the screen is **not** rendered by scanning the D-FILE in raster mode.

The `STEADY` fallback still uses the old D-FILE renderer and is intentionally not cycle-aware.

### Fidelity limits

This is **cycle-aware scanline capture, not a bus-exact analog television simulator**. The Z80 engine is primarily instruction-level, with approximate timings for some uncommon instructions, not a separate bus-operation event at every clock edge. The software raster maps each video M1 to an 8-pixel group; it does not emulate an analog RF waveform, sub-character glitches, nonstandard field dimensions, color/snow, or the precise analog effects of lost sync.

Horizontal alignment uses the timing measured from the original 4 KB ROM, and vertical placement assumes the PAL 56-line top border. Unusual custom display routines that change these parameters, issue multiple synchronization transitions within a line, deliberately write into unsupported expansion hardware, or depend on exact Z80 memory contention may still render incorrectly. Some flicker-free or machine-code games are now more plausible compatibility targets, **not guaranteed compatible**. The stock 4 KB ROM and BASIC behavior are regression-tested; individual games have not been tested.

## Z80 clock limiting

Emulated CPU clock is capped at **3.25 MHz**. Each 20 ms host window executes a budget of 65,000 emulated Z80 T-states, carrying instruction-overshoot forward, then waits until the 20 ms deadline according to NARM's `narm_micros()` clock. If real hardware cannot process all execution/rendering in time, emulation will be **slower** than real ZX80, never deliberately faster.

## Keyboard and controls

| Key | Action |
|---|---|
| F1 | Controls/help overlay |
| F2 | Emulator status and display diagnostics |
| F3 | ROM glyph grid |
| F4 | Toggle *old steady-mode* pixel/blitter renderer (no effect in raster mode) |
| F5 | Reset the ZX80 |
| F6 | Pause/resume |
| F7 | Change 1 KB / 16 KB RAM mode and reset |
| F8 | CPU and RAM self-test (destructive; resets BASIC) |
| F9 | Toggle **M1 SCAN VIDEO** (default) / **STEADY VIDEO** (old renderer) |
| Esc | Exit |

F2 shows ROM CRC, display-file bytes, PC, IRQ, field count, and intercepted M1 fetch count. `SYNC` indicates that a recent completed video field exists. An idle screen should show an inverse K near the bottom-left of the text area. In raster mode, the old F2 `CHARS` counter is still from the fallback D-FILE parser and may be zero or stale; `FETCH` and `FIELDS` are the important new counters.

## Building

Compiler: `narmcc` from the supplied `Compiler_For_NARM.7z` toolchain.

```
cd source
narmcc zx80_narm.tc -I . -O1 --stack-bytes 12288 -o ../zx80.narm
```

Or run `build_windows.bat`, or `NARMCC=/path/to/narmcc bash build_linux.sh` on Linux.

Compiled `.narm`: about **49 KB**. Runtime BSS: about **128 KB**, dominated by the two 48 KB packed raster buffers and the 16 KB emulated Z80 RAM.

## Host-side tests

These tests use the *same C source* the NARM application compiles:

```
gcc -O2 tests/cpu_smoke.c -o cpu_smoke
./cpu_smoke

gcc -O2 tests/raster_custom.c -o raster_custom
./raster_custom

gcc -O2 tests/raster_integration.c -o raster_integration
./raster_integration /path/to/your/zx80.rom
```

The integration test writes `raster_start.ppm` and `raster_A.ppm` in the current directory. It tests original BASIC boot, field capture, correct inverse K at the bottom-left, insertion of `LIST` after the A key, and video loss under a synthetic non-video program.

Additional regression tests: `display_smoke.c` and `rom_integration.c`. For these, compile with `-ffunction-sections -fdata-sections -Wl,--gc-sections` to discard the embedded firmware entry point in the host executable.

**Result:** CPU tests, synthetic raster tests, old display/keyboard tests, original 4 KB ZX80 ROM integration, source-level raster integration, and the NARM compiler all pass **on Linux**. No physical RP2350 run is available here. Keep your known-working v4/v5 binary as a fallback and use F9 if the new raster appears blank on real hardware.

## Hardware references

- [Tom Harte: ZX80/ZX81 hardware and scanline video](https://github.com/TomHarte/CLK/wiki/The-ZX80-and-ZX81)
- [Tynemouth: How the ZX80 Generates Video](https://blog.tynemouthsoftware.co.uk/2023/10/how-the-zx80-generates-video.html)
- [ZX80 Flicker-Free Display Mechanism](https://homepages.plus.net/fruitcake/Sinclair/ZX80/FlickerFree/ZX80_DisplayMechanism.htm)
