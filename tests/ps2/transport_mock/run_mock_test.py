"""Build and run the real PS/2 transport against mock hardware."""
from pathlib import Path
import argparse
import sys

TEST_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TEST_ROOT))
from test_paths import executable_path, find_cpp_compiler, find_project_root, run_checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    compiler = args.compiler or find_cpp_compiler()
    project = find_project_root(TEST_ROOT)
    build_dir = args.build_dir or TEST_ROOT / ".test-build"
    build_dir.mkdir(parents=True, exist_ok=True)
    executable = executable_path(build_dir, "transport_test")
    root = Path(__file__).resolve().parent
    run_checked([
        compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I", root / "include", "-I", project / "MPC-2350_Firmware",
        root / "transport_test.cpp", "-o", executable,
    ], compiler)
    run_checked([executable], compiler)


if __name__ == "__main__":
    main()
