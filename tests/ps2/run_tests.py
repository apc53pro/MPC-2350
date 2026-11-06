"""Run the nine host checks for the MPC-2350 PS/2 input implementation."""
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

    python_check("[1/9] PIO receive instruction model", "pio_rx_model_test.py")
    python_check("[2/9] Actual transport with mock hardware",
                 "transport_mock/run_mock_test.py",
                 ["--compiler", compiler, "--build-dir", build_dir])
    python_check("[3/9] Flash park/resume interleaving model", "flash_handshake_model_test.py")
    cpp_check("[4/9] Mouse packet decoder", "mouse_protocol_tests",
              [runtime_tests / "mouse_protocol_tests.cpp"])
    cpp_check("[5/9] Actual mouse initialization/runtime", "mouse_runtime_tests",
              [runtime_tests / "mouse_runtime_tests.cpp", firmware / "mouse_runtime.cpp"],
              includes=[runtime_tests / "mouse_runtime_stubs"])
    cpp_check("[6/9] Focus, event queue and raw mouse input", "mouse_input_tests",
              [runtime_tests / "mouse_input_tests.cpp"])
    cpp_check("[7/9] Production NARM mouse service boundary", "native_mouse_api_tests",
              [runtime_tests / "native_mouse_api_tests.cpp"])
    cpp_check("[8/9] Production keyboard queue and parser", "keyboard_poll_tests",
              [runtime_tests / "keyboard_poll_tests.cpp", firmware / "keyboard_runtime.cpp"],
              includes=[runtime_tests / "mouse_runtime_stubs"])
    cpp_check("[9/9] Raw mouse trace snapshots", "mouse_trace_tests",
              [runtime_tests / "mouse_trace_tests.cpp"], extra=["-pthread"])
    print("All 9 PS/2 host checks passed.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        print(f"PS/2 host checks failed: {error}", file=sys.stderr)
        sys.exit(1)
