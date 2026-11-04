"""Run the thirteen host checks for the MPC-2350 PS/2 input/cursor implementation."""
from pathlib import Path
import subprocess
import sys

from test_paths import executable_path, find_cpp_compiler, find_project_root, run_checked


def main():
    tests_root = Path(__file__).resolve().parent
    firmware = find_project_root(tests_root) / "MPC-2350_Firmware"
    compiler = find_cpp_compiler()
    build_dir = tests_root / ".test-build"
    build_dir.mkdir(parents=True, exist_ok=True)
    flags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror"]
    cursor_tests = tests_root / "cursor-tests"
    runtime_tests = tests_root / "tests"

    def python_check(label, script, extra=()):
        print(label, flush=True)
        run_checked([sys.executable, tests_root / script, *extra], compiler)

    def cpp_check(label, name, sources, includes=(), extra=()):
        print(label, flush=True)
        executable = executable_path(build_dir, name)
        include_flags = []
        for include in (*includes, firmware):
            include_flags.extend(["-I", include])
        run_checked([compiler, *flags, *include_flags, *extra, *sources,
                     "-o", executable], compiler)
        run_checked([executable], compiler)

    python_check("[1/13] PIO receive instruction model", "pio_rx_model_test.py")
    python_check("[2/13] Actual transport with mock hardware",
                 "transport_mock/run_mock_test.py",
                 ["--compiler", compiler, "--build-dir", build_dir])
    python_check("[3/13] Flash park/resume interleaving model", "flash_handshake_model_test.py")
    cpp_check("[4/13] Mouse packet decoder", "mouse_protocol_tests",
              [runtime_tests / "mouse_protocol_tests.cpp"])
    cpp_check("[5/13] Actual mouse initialization/runtime", "mouse_runtime_tests",
              [runtime_tests / "mouse_runtime_tests.cpp", firmware / "mouse_runtime.cpp"],
              includes=[runtime_tests / "mouse_runtime_stubs"])
    cpp_check("[6/13] Cursor compositor and cadence", "mouse_cursor_tests",
              [cursor_tests / "mouse_cursor_tests.cpp"])
    cpp_check("[7/13] Actual half-resolution/VGA4 presentation paths", "mouse_cursor_present_tests",
              [cursor_tests / "mouse_cursor_present_tests.cpp"])
    cpp_check("[8/13] Actual renderer concurrency", "mouse_cursor_concurrency_tests",
              [cursor_tests / "mouse_cursor_concurrency_tests.cpp", firmware / "mouse_cursor.cpp"],
              includes=[cursor_tests / "stubs"],
              extra=["-pthread", "-include", cursor_tests / "cursor_host_globals.h"])
    cpp_check("[9/13] Focus, event queue and raw mouse input", "mouse_input_tests",
              [runtime_tests / "mouse_input_tests.cpp"])
    cpp_check("[10/13] Production NARM mouse service boundary", "native_mouse_api_tests",
              [runtime_tests / "native_mouse_api_tests.cpp"])
    cpp_check("[11/13] Stationary cursor performs no redraws", "mouse_cursor_idle_tests",
              [cursor_tests / "mouse_cursor_idle_tests.cpp"],
              includes=[cursor_tests / "stubs"],
              extra=["-include", cursor_tests / "cursor_host_globals.h"])
    cpp_check("[12/13] Production keyboard queue and parser", "keyboard_poll_tests",
              [runtime_tests / "keyboard_poll_tests.cpp", firmware / "keyboard_runtime.cpp"],
              includes=[runtime_tests / "mouse_runtime_stubs"])
    cpp_check("[13/13] Raw mouse trace snapshots", "mouse_trace_tests",
              [runtime_tests / "mouse_trace_tests.cpp"], extra=["-pthread"])
    print("All 13 PS/2 host checks passed.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        print(f"PS/2 host checks failed: {error}", file=sys.stderr)
        sys.exit(1)
