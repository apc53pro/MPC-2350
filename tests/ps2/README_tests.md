# PS/2 host checks

Run from any working directory with Python 3.10+ and a C++17-capable `g++`:

```text
python tests/ps2/run_tests.py
```

In the staging tree, use `python run_tests.py` from the directory containing
this README. No Python packages or Arduino tools are required. The runner finds
`g++` on `PATH`, or uses `C:/msys64/mingw64/bin/g++.exe` on Windows. All C++ tests
compile with `-O2 -Wall -Wextra -Werror`. Executables go into `.test-build/` beside
the runner and can be deleted after the checks.

The scripts locate the project by walking upward until they find sibling
`MPC-2350_Firmware/` and `vendor-libraries/` directories. The same sources run
in staging or after installation under `tests/ps2/`.

| Check | What it verifies |
| --- | --- |
| PIO receive instruction model | Executes the actual assembled PIO words against all 256 byte values, successive frames, parity errors, truncated frames, and idle/noise input. |
| Transport hardware mock | Exercises the actual PIO transport, host bit timing, open-drain writes, immediate ACK reception, absent-device and missing-ACK timeouts, IRQ state, and flash pause/resume hooks. |
| Flash handshake model | Reproduces the old stale-acknowledgement race and explores timeout/retry interleavings of the ticket handshake, including ticket wrap. Checks that the source contains the modeled ticket operations. |
| Mouse packet decoder | Tests signed movement, Y direction, clipping, overflow flags, buttons, packet resynchronization, time wrap, and supported extended packet formats. |
| Mouse initialization/runtime | Exercises the actual runtime with transport/time stubs: reset/defaults/stream/enable commands, ACK and early-report handling, missing-device retries, streaming state updates, and interrupt masking boundaries. |
| Mouse input policy | Tests focus, quick clicks, coalescing, queue overflow/resync, raw deltas at screen edges, relative mode, wheel decoding, and saturation. |
| Native mouse API | Exercises the production service wrappers against writable-memory bounds, invalid pointers, missing capabilities, and background focus; invalid calls preserve queued input. |
| Production keyboard poll | Runs the actual parser/queue and keyboard runtime through 20 million idle polls, typing/modifiers, extended keys, the observed 153 RX/122 overrun/31 queued state, resync, focus routing, and a continuously replenished queue. Requires bounded consumption and zero Arduino IRQ-wrapper calls. |
| Mouse trace | Runs concurrent writer/snapshot readers, checks record consistency and signed values through ring wrap, and verifies reset. The production runtime test checks actual wire-byte/packet records and reading the trace without acquiring the input lock. |

Mouse runtime checks include exact wheel negotiation, three-/four-byte format
selection, queued early wheel reports, NARM wheel events/motion, optional-ID
timeout/rejection/wire-failure fallback, parameter RESEND, and reconnect. Decoder
checks include zero-axis sign retention and frame-loss recovery at every byte
position for IDs 0/3/4, including timestamp wrap. Transport checks verify that
on-demand hardware diagnostics neither drain FIFOs nor restart stopped SMs.

The former pixel-compositor and VGA presentation checks were removed with
the software video stack. GPU client checks live in tests/gpu/.

These are host checks of code, instruction behavior, and modeled concurrency.
They do not replace an RP2350 firmware build or a real keyboard/mouse test.

For source delivery, retain this README, the Python scripts, test C++ sources,
and stub headers. Exclude `.test-build/`, `__pycache__/`, and executables.
