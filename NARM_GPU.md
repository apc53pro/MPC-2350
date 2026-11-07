# Nano GPU services for NARM

Firmware API 1.19 exposes Nano GPU interface 1.1 at `0x0500` with capability
bit 6. NARMCC records the firmware and capability requirements automatically.
Include `narm.tch` with the updated compiler for the [GPU declarations](sdk/narm_gpu.tch).

`narm_gpu_submit(command, args, arg_words)` takes a four-byte-aligned array of
32-bit native arguments. Encode a program pointer as `(unsigned)pointer` in an
argument word. The runtime checks the argument array and each byte buffer
against the program's data or stack allocation. Calls require an active,
focused process with the GPU capability.

| NARMCC built-in | Arguments in order | On success |
| --- | --- | --- |
| `narm_gpu_get_caps` | output pointer | Writes 64 capability bytes. |
| `narm_gpu_get_status` | output pointer | Writes an ordered, coherent 64-byte core status snapshot. |
| `narm_gpu_write_heap` | GPU address, source pointer, byte count | Queues 1–956 bytes of decoded upload data. |
| `narm_gpu_submit_pass` | GPU address of 32-byte pass descriptor | Queues a pass and its tile lists. |
| `narm_gpu_copy_rect` | pointer to `NarmGpuCopy` | Queues a framebuffer or heap image copy to the writable back buffer. |
| `narm_gpu_fence` | caller token | Waits for earlier commands and their SDRAM writes. |
| `narm_gpu_present` | writable back buffer ID, caller token | Waits for the swap at vertical blank. |
| `narm_gpu_read_memory` | GPU address, byte count, output pointer | Reads 1–512 bytes after earlier commands. |
| `narm_gpu_back_target` | none | Returns the writable physical color buffer ID, 0 or 1. |
| `narm_gpu_encode_pass` | output pointer, target, flags, clear RGB565, clear D24, GPU table address | Encodes a 32-byte descriptor in CPU memory. |
| `narm_gpu_encode_tile` | output pointer, GPU list address, reference count | Encodes an 8-byte tile entry in CPU memory. Use 0, 0 for an empty tile. |
| `narm_gpu_encode_primitive` | output pointer, pointer to `NarmGpuPrimitive` | Encodes a 320-byte primitive in a separate CPU buffer. |
| `narm_gpu_link_status` | output pointer | Reads the 64-byte sideband bank, including link flags and fault codes, even after a packet fault. |
| `narm_gpu_abort` | none | Quiesces the link and resumes with a new epoch while preserving scanout. |
| `narm_gpu_reset` | none | Hard-resets GPU state and restores firmware terminal assets. |
| `narm_gpu_frame_ready` | none | Returns 1 when earlier commands and any async swap have completed, or 0 while busy. |
| `narm_gpu_try_present` | writable back buffer ID, caller token | Returns 1 when queued or 0 while busy. Completion is asynchronous. |

Other successful calls return 0. Errors are `-1` unknown command, `-2` invalid
arguments, `-3` missing capability, `-4` lack of display focus or pending
asynchronous presentation, and `-5` GPU
offline or transport/protocol failure. A queued pass can fault later while
the GPU reads its records. Use a fence to establish that those operations
completed, and stop submitting if the fence reports a device failure.

Allocate app graphics data in `0x280000..0x7fffff`. Firmware reserves the
lower GPU heap for the terminal font and UI. Readback can inspect valid
GPU SDRAM ranges, including color and depth attachments. The graphics heap
and display attachments are shared among trusted foreground programs. On
focus regain, query the current back target and restore any graphics data and
attachment contents your program needs.

The driver generates packet headers, sequence numbers and upload encoding,
and checks reply headers, epochs, status and tokens. Use
[GPU_CONTROL.md](https://github.com/apc53pro/Nano-20K-GPU/blob/main/GPU_DOCUMENTATION/GPU_CONTROL.md)
for capability and status layouts, pass and primitive records, tile lists,
textures, and copy arguments. The firmware record builders serialize the pass,
tile, and primitive fields in little-endian order, including reserved bytes.
Use the SDK constants for triangle, rectangle, line and sprite kinds, color
and depth enables, keying, all five shade modes, LESS/LEQUAL/ALWAYS, independent
U/V CLAMP/REPEAT, and color/depth LOAD/CLEAR and depth DISCARD. Set a texture base
to enable its RGB565 descriptor. For prepared triangles, supply Q4 vertices,
three edges, and the seven signed 64-bit planes as low/high pairs. The
application prepares geometry and bins it into ordered little-endian lists of
GPU record addresses, then uploads its batches.

Use CLEAR to initialize a back buffer or LOAD to retain valid
contents. Passes that never test depth should clear and DISCARD it (load
flags bit2): the GPU then moves no depth through SDRAM, and a LOAD-color pass
skips every tile without draws. Depth LOAD after a DISCARD requires a pass
that clears and stores depth first. DISCARD requires the current FPGA image.
Keep active heap resources immutable until execution completes. After abort,
reupload interrupted resources and clear invalid attachments before use.
After hard reset, rebuild all application resources.

## Keep the NARM frame loop running

**Strong recommendation for all NARM programs. Keep processing input and
simulation while the GPU is busy. Poll once per application-loop iteration and
submit a new presentation only when the GPU is available.** Avoid
`narm_gpu_fence`, synchronous `narm_gpu_present`, and readiness spin loops in
the realtime frame path.

1. Update input, simulation and CPU-side frame preparation each iteration.
2. Call `narm_gpu_frame_ready()`. On 0, continue the application loop. On a
   negative return, stop GPU submissions and handle the fault. On 1, the
   previous work has retired and any pending swap has been acknowledged.
3. Get the current back target before submitting render work. Retain the
   target and frame state until it is ready to present. While that work is
   running, keep updating input and simulation.
4. Call `narm_gpu_try_present(target, token)` for the completed frame. On 1,
   remember that it is queued. Poll `narm_gpu_frame_ready()` on a later
   iteration before getting the next back target. On 0, retain the completed
   frame and retry on a later iteration.

Each readiness poll samples sideband state and drains a completed swap
reply if present. It does not wait for a draw or for vertical blank. Large
uploads can still wait for host FIFO space. For latency-sensitive streaming,
schedule one packet per ready poll and use up to
`NARM_GPU_FRAME_UPLOAD_BYTES` (216) per upload so even its worst-case encoding
fits the empty RX FIFO.

[narm_gpu_smoke.tc](tests/gpu/narm_gpu_smoke.tc) exercises the raw packet commands.
It displays a blue screen with a red square for three seconds before returning
to the terminal. Apply the [NARMCC source patch](sdk/narmcc-gpu.patch) once
to a clean pre-GPU copy of `Compiler_For_NARM`. These commands start in the
firmware directory and use the MSYS2 UCRT64 C++ compiler:

```powershell
$env:PATH = 'C:/msys64/ucrt64/bin;' + $env:PATH
$compilerPatch = (Resolve-Path sdk/narmcc-gpu.patch).Path
Push-Location ../Compiler_For_NARM
git apply --check --ignore-space-change $compilerPatch
if ($LASTEXITCODE -ne 0) { throw "Compiler patch check failed" }
git apply --ignore-space-change $compilerPatch
if ($LASTEXITCODE -ne 0) { throw "Compiler patch failed" }
.\build_narmcc.ps1
Pop-Location
Copy-Item sdk/narm_gpu.tch ../Compiler_For_NARM/narm_gpu.tch
& ../Compiler_For_NARM/narmcc.exe tests/gpu/narm_gpu_smoke.tc `
  -I ../Compiler_For_NARM -I sdk -o build/narm_gpu_smoke.narm
```

Run the resulting image as a foreground NARM task. Exit status 0 confirms the
expected command results and readback. The firmware terminal redraws its own
contents after the task exits.
