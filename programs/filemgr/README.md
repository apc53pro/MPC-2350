# FILEMGR 640

Native NARM file manager using the firmware-owned 640x480, 4-bit VGA framebuffer.
Requires firmware API **1.11** or newer; updating only the `.narm` file on older
firmware is insufficient. Existing applications remain compatible with the new
firmware.

## Files and building

- `filemgr640.tc`: filesystem operations, normalized input, image decoding,
  child program launch, audio state, and application loop.
- `filemgr_graphics.tch`: drawing primitives and batched bitmap text.
- `filemgr_ui.tch`: browser, details, player, viewer, and dialogs.
- `narm.tch`: matching public runtime declarations/constants.

Application drawing helpers use names such as `fmFillRect`. Do not rename this
helper to `fillRect2D`: that name is a compiler-recognized legacy built-in that
draws to the separate 320x240 RGB332 surface. FILEMGR must clear and draw on the
same VGA4 surface. Redraw regressions compare incremental updates against clean
frames, and native graphics tests execute the generated Thumb code as well as
the host C++ model.

From this directory on Windows, run `./build_filemgr.ps1`. It uses the project's
`Compiler_For_NARM/narmcc.exe` and writes `../../data/filemgr.narm`. To use another
compiler or output location, pass `-Compiler` and `-Output`.

Equivalent command, including on Debian with a locally built NARMCC:

```sh
narmcc filemgr640.tc -I . -O3 --default-mode background --name "FILEMGR 640" --stack-bytes 16384 --heap-bytes 16384 -o filemgr.narm
```

Upload the updated firmware, enter `USB_STORAGE ON`, and update
`data/filemgr.narm` on the `RP2350FS` drive. Safely eject the drive and run
`USB_STORAGE OFF` before launching it.

## Controls

| Keys | Action |
| --- | --- |
| Up/Down or W/S | Move selection |
| Page Up/Page Down | Move one page |
| Enter | Open folder, launch NARM program, play WAV, or inspect file |
| Backspace/Escape | Parent directory or return from viewer; cancel name edit |
| T or Tab | Switch Flash/SD |
| R | Refresh/remount selected storage |
| F / D / E | Create file / create folder / rename |
| Delete or X, twice | Confirm deletion |
| Space | Pause/resume; restart a stopped or finished track |
| P | Stop playback and reset position to zero |
| Left/Right | Seek backward/forward 10 seconds |
| Home/End | Seek to start/last frame |
| Alt+F2 / Alt+F3 / Alt+F4 | Firmware focus / multitasking mode / close shortcuts |

Audio shortcuts are disabled while editing a filename. In the image viewer,
arrows/WASD pan and I/K zoom; Home/End still seek audio. Return to the browser for
10-second Left/Right seeking. In the file inspector Up/Down and Page Up/Down page
through the contents. The bottom help text changes with the active view.

## Launch and playback behavior

Launching a program uses `narm_spawn_program`, not the old destructive
`narm_run_program` chain-exec service. FILEMGR keeps its directory and selection
while the child receives focus. Closing a focused child restores FILEMGR if it is
still alive. Alt+F2 also allows manual switching among running applications.
Launch failures keep FILEMGR open and report an error.

FILEMGR defaults to background mode so its music can continue while another app
has focus. It does no screen drawing or keyboard processing while unfocused.
Selecting foreground-only mode with Alt+F3 uses the existing runtime policy:
execution and audio pause while FILEMGR is unfocused. Explicit user pause is
preserved across focus changes.

Only voice **0** is used. Another app's active voice is never forcibly stopped or
stolen. WAV playback streams from the **SD card** through existing firmware
functions; Flash-only WAV streaming and compressed audio decoding are not added.
Use PCM WAV files supported by the firmware. The progress display uses source
frame position/rate/length reported by the mixer, not elapsed wall time, and
clamps seeking to the track boundaries. Seeking a stopped track selects a new
restart position without beginning playback. A short hardware output-buffer
latency can separate the displayed mixer position from the audible output.

Existing filesystem operations and BMP/PPM/PGM image decoding are retained.
PNG and JPEG decode into the same packed image cache. GIF is recognized but
remains unsupported; the UI states that limitation.
Image row caches and text masks are small application buffers, not a private
full-screen framebuffer.


## Bounded image streaming

Image decoding and display share four retained 4 KiB blocks (16 KiB total).
This working set does not grow with the image dimensions. The temporary cache
on the selected storage contains row-aligned packed VGA4 palette indices,
with a 512-byte header followed by `ceil(width/2) * height` bytes. For example,
a 640x480 image uses 150 KiB plus the header on storage, rather than in RAM.

Pan and zoom invalidate transform-dependent display rows while retaining source
blocks. Sequential rendering consumes neighboring pixels and rows from each
block before fetching another. Two 640-byte display rows allow identical rows
to be merged into larger rectangle submissions. Each input renders once at full
resolution; there is no delayed duplicate preview redraw. Storage failures latch
until the viewer is reopened, avoiding one failed retry per pixel.

The implementation uses the existing filename-based file API. It keeps no file
handle open, adds no compiler builtins, and still requires only firmware API 1.11.
The 16-color palette and lack of dithering are unchanged.


## PNG and JPEG

`.png`, `.jpg`, and `.jpeg` (case-insensitive) open in the image viewer. Detection
uses the actual file signature, not the extension. Initial decoding creates the
same packed 4-bit cache, so subsequent fit, pan and zoom use the retained block
cache and merged-row renderer. No firmware or compiler update is required.

PNG supports grayscale, RGB, indexed color, grayscale+alpha and RGBA; standard
1/2/4/8/16-bit sample depths where defined by PNG; all five row filters; stored,
fixed and dynamic DEFLATE; split IDAT chunks; and Adam7 interlacing. Alpha and
tRNS transparency are composited onto white before palette conversion. PNG CRC
and zlib Adler-32 are checked. The default image of APNG is shown as a still.
Color-management metadata (gamma/ICC) is not applied. A decoded PNG scanline
must fit the fixed 16 KiB row buffer (up to 4096 pixels for RGBA8, 2048 for RGBA16).
Oversized scanlines report an unsupported variant rather than allocating more
memory. The DEFLATE history window is a fixed 32 KiB.

JPEG supports 8-bit sequential and progressive Huffman DCT coding, grayscale,
YCbCr/RGB, CMYK and Adobe YCCK; common and extended sampling ratios; restart
markers; separate-component scans; and 8/16-bit quantization tables. Chroma is
upsampled by nearest-neighbor replication. EXIF orientation and ICC transforms
are not applied. Arithmetic-coded, lossless and 12-bit-sample JPEG variants are
reported as unsupported. The fixed display palette remains 16 colors.

JPEG quantized coefficients are stored in `.fm_jpeg.tmp` in the image directory,
using 128 bytes per padded 8x8 component block. For a 640x480 4:2:0 image this is
900 KiB of temporary storage in addition to the 150 KiB packed image cache.
Only a bounded 16 KiB write-back cache and 16 decoded 8x8 blocks are kept in RAM.
The coefficient file is removed after conversion, cancellation or a handled
error. PNG and JPEG service work in bounded steps so Backspace/Escape can cancel
preparation. Corrupt data, checksum failures, unsupported variants and storage
failures are reported in the preparation panel.

The compiled app uses about 118 KiB of static data for all app/decoder state,
independent of image dimensions, plus its unchanged 16 KiB stack and 16 KiB heap
reservations. The image itself and JPEG coefficients stay on storage. Initial
decoding speed depends on compressed data and SD performance; tests verify file
I/O and pixels, not physical board timing.

Format references: [PNG specification](https://www.w3.org/TR/png-3/),
[DEFLATE RFC 1951](https://www.rfc-editor.org/rfc/rfc1951), and
[JPEG ITU-T T.81](https://www.w3.org/Graphics/JPEG/itu-t81.pdf).
