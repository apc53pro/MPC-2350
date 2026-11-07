# Nano GPU services for NARM

Firmware API 1.18 exposes Nano GPU interface 1.0 at `0x0500` with capability
bit 6. NARMCC marks programs that call `narm_gpu_submit` with this requirement.
Include `narm.tch` with the updated compiler or include the standalone
[narm_gpu.tch](sdk/narm_gpu.tch) for GPU declarations.

`narm_gpu_submit(command, args, arg_words)` takes a four-byte-aligned array of
32-bit native arguments. Encode a program pointer as `(unsigned)pointer` in an
argument word. The runtime checks the argument array and each byte buffer
against the program's data or stack allocation. Calls require an active,
focused process with the GPU capability.

| Command | Argument words in order | On success |
| --- | --- | --- |
| `NARM_GPU_GET_CAPS` (1) | output pointer | Writes 64 capability bytes. |
| `NARM_GPU_GET_STATUS` (2) | output pointer | Writes a coherent 64-byte core status snapshot. |
| `NARM_GPU_WRITE_HEAP` (3) | GPU address, source pointer, byte count | Queues 1–956 bytes of decoded upload data. |
| `NARM_GPU_SUBMIT_PASS` (4) | GPU address of 32-byte pass descriptor | Queues a pass and its tile lists. |
| `NARM_GPU_COPY_RECT` (5) | All 14 words of the `COPY_RECT` payload | Queues a framebuffer or heap image copy to the writable back buffer. |
| `NARM_GPU_FENCE` (6) | caller token | Waits for earlier commands and their SDRAM writes. |
| `NARM_GPU_PRESENT` (7) | writable back buffer ID, caller token | Waits for the swap at vertical blank. |
| `NARM_GPU_READ_MEMORY` (8) | GPU address, byte count, output pointer | Reads 1–512 bytes after earlier commands. |
| `NARM_GPU_BACK_TARGET` (`0x0100`) | none | Returns the writable physical color buffer ID, 0 or 1. |

Other successful calls return 0. Errors are `-1` unknown command, `-2` invalid
arguments, `-3` missing capability, `-4` lack of display focus, and `-5` GPU
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
and checks reply headers, epochs, status and tokens. Programs explicitly
serialize heap data to the little-endian GPU format. Use
[GPU_CONTROL.md](https://github.com/apc53pro/Nano-20K-GPU/blob/main/GPU_DOCUMENTATION/GPU_CONTROL.md)
for capability and status layouts, pass and primitive records, tile lists,
textures, and the 14 copy words. Pack tile lists and primitive records on the
CPU and upload them with `WRITE_HEAP`. The GPU performs drawing, copies and
presentation. Use CLEAR to initialize a back buffer or LOAD to retain valid
contents. Preserve heap allocations referenced by an executing pass until its
fence completes.

[narm_gpu_smoke.tc](tests/gpu/narm_gpu_smoke.tc) exercises every command.
It displays a blue screen with a red square for three seconds before returning
to the terminal. Apply the [NARMCC source patch](sdk/narmcc-gpu.patch) once
to the sibling `Compiler_For_NARM` directory. These commands start in the
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
& ../Compiler_For_NARM/narmcc.exe tests/gpu/narm_gpu_smoke.tc `
  -I ../Compiler_For_NARM -I sdk -o build/narm_gpu_smoke.narm
```

Run the resulting image as a foreground NARM task. Exit status 0 confirms the
expected command results and readback. The firmware terminal redraws its own
contents after the task exits.
