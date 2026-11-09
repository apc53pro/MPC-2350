# NARM MMBASIC v91 colour modes

Use the rebuilt `mmbasic.narm` with the MPC-2350 Nano GPU firmware and the SKIP
FPGA image. Both BASIC display modes render into the GPU's RGB565 buffers
through NARM API 1.18.

| Command | Logical resolution | Colour depth | Console / editor |
| --- | --- | --- | --- |
| `MODE 3` | 640 × 480 | 4 bits per pixel, 16 fixed colours | 106 columns × 60 rows |
| `MODE 5` | 320 × 240 | 8 bits per pixel, 256 RGB332 colours | 53 columns × 30 rows |

The mode numbers follow the corresponding modes in the [PicoMite manual](https://geoffg.net/Downloads/picomite/PicoMite_User_Manual.pdf). These are the two supported NARM modes. Modes 1, 2 and 4 are not implemented. The colour API and fixed palette described below are specific to this interpreter and firmware.

The interpreter starts in `MODE 5`. Enter commands at the REPL, or put them in a stored BASIC program. Use F9 to reach the REPL from the editor.

```basic
MODE 3
GCLS 0
RECT 0,0,639,479,255
LINE 0,0,639,479,RGB(255,0,0)
GTEXT 400,300,"640 X 480",RGB(255,255,0)
PIXEL 639,479,255
PRESENT
```

Return to the original resolution with `MODE 5`. Mode changes clear the screen and console, reset the console cursor, and cancel pending mouse input. Program source and BASIC variables survive a mode change. The selected mode stays active after `RUN` finishes, including when returning to the editor. It is not saved across interpreter restarts.

## Resolution and input

These read-only numeric values work in expressions in the REPL and stored programs.

```basic
PRINT MM.HRES, MM.VRES
PRINT MM.BPP, MM.MODE
PIXEL MM.HRES-1,MM.VRES-1,255
```

Drawing commands, text, buttons, console layout, editor layout, and absolute mouse coordinates follow the current mode. The 5×7 font uses a 6×8 character cell in either mode, giving the larger editor more rows and columns. Click, drag, Shift-click, wheel scrolling, and toolbar actions work in both resolutions.

`MOUSE("X")`, `MOUSE("Y")`, `MOUSE("EVENTX")` and `MOUSE("EVENTY")` use logical pixels in the current mode. Raw motion and wheel values, including `MOUSE("DX")` and `MOUSE("DY")`, retain the firmware's original units.

## Colours

Existing colour arguments remain packed RGB332 values from 0 to 255 in both modes. Black is `0` and white is `255`. Values 0–15 are RGB332 colours, not palette slot numbers. This preserves existing programs and editor colour settings.

The new `RGB(red,green,blue)` function accepts numeric components from 0 to 255 and returns a packed RGB332 value. It accepts expressions and nested calls. For example, `RGB(255,0,0)` returns `224`, `RGB(0,255,0)` returns `28`, and `RGB(255,255,255)` returns `255`. This is an RGB332 result, unlike PicoMite's 24-bit RGB function. Named-colour arguments are not supported.

`MODE 5` expands RGB332 colours to RGB565. `MODE 3` maps them through the
interpreter's 16-colour palette. Both draw at 640×480. In `MODE 5` the GPU
draws at doubled logical coordinates, with doubled glyphs and rectangle extents.

The 16 exact RGB332 palette values are `255, 0, 2, 16, 224, 128, 97, 236, 252, 28, 9, 159, 7, 227, 73, 146`. Palette remapping, monochrome tile modes, and PicoMite layer buffers are not part of this change.

`GCLS`, `PIXEL`/`PSET`, `LINE`, `RECT`, `FILLRECT`, `TRI`, `FILLTRI`,
`GTEXT`, `GBUTTON` and `PRESENT` use the coordinates of the selected mode.

## Frame updates

If a stored program contains `PRESENT`, use one after drawing each complete
frame. The interpreter groups drawing across BASIC lines and can execute
subsequent statements while the GPU renders. It finishes the swap with
blocking PRESENT once rendering completes or the next frame needs GPU
access. The swap waits for vertical blank. `PAUSE` services the queued frame
during the delay.

Programs without `PRESENT` update automatically as the display becomes
available. Incremental frames copy the tiles that can differ between the
two buffers. A clear-and-redraw frame uses SKIP after both buffers have been
initialized to the same clear color. Glyphs remain resident in VRAM. The
editor paints each background once and caches rasterized source strips in
VRAM by their displayed text, glyph bits, colors and scale. Scrolling a
resident row to another screen position reuses its strip.

## Test demo

Copy `COLORMODES.BAS` to the SD card and run it.

```basic
LOAD "/COLORMODES.BAS"
RUN
```

Space switches between modes. Escape exits and returns to `MODE 5`. The demo draws all 256 RGB332 inputs. There are 256 distinct colours in `MODE 5` and 16 in `MODE 3`.

The earlier `GFXLAB.BAS` and `CORRIDOR.BAS` demos retain their 320×240 layouts. Use `MODE 5` before running them. Changing the display mode does not rescale coordinates in an existing BASIC program.

## Installation and rebuilding

Copy `data/mmbasic.narm` to the RP2350 flash filesystem, then launch
`MMBASIC` from the system prompt. Build from this directory with GPU-enabled
NARMCC.

The source directory contains the shared interpreter, NARM wrapper, runtime
header, demo, and historical regression probes. Build with the current project
compiler.

```powershell
narmcc.exe mmbasic_v91_narm.tc -I . -I ../../sdk -O2 --stack-bytes 16384 -o mmbasic.narm
```

The interpreter retains single-precision arithmetic. The GPU drawing helpers
encode tiled passes for lines, rectangles, triangles and text.

## Historical validation

The compiled ARM Thumb interpreter was run under Unicorn with native video and input services supplied by a test harness. Checks cover direct and stored-program mode switching, expression queries, RGB calculations, invalid modes, clipping, drawing at the final pixel, triangle and text routing, high-resolution editor selection, wheel scrolling, clickable modal screens, raw mouse deltas, and loading and switching the SD demo. The demo's rendered output was checked for 16 and 256 distinct colours.

The earlier interpreter regression suite, graphics showcase, and interactive raycaster were also checked. Screenshots are emulated rendering, not captures from an RP2350. Physical display output, PS/2 interaction and frame rate still need board testing.

## Historical regression probes

The bundled probes use the original video-service mock. They document the
earlier interpreter tests and require the matching pre-GPU build.

```powershell
narmcc.exe tests/modes_probe.tc -I . -O2 -o tests/modes_probe.narm
python tests/test_modes.py
narmcc.exe tests/probe_v91.tc -I . -O2 -o tests/probe_v91.narm
python tests/run_probe_v91.py
```
