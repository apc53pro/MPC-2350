"""Shared standard-library-only paths and compiler discovery for PS/2 tests."""
from pathlib import Path
import os
import shutil
import subprocess


def find_project_root(start=None):
    """Work in either staging or an installed project/tests/ps2 tree."""
    location = Path(start or __file__).resolve()
    if location.is_file():
        location = location.parent
    for candidate in (location, *location.parents):
        if ((candidate / "MPC-2350_Firmware").is_dir()
                and (candidate / "vendor-libraries").is_dir()):
            return candidate
    raise FileNotFoundError(
        "Cannot find the project: expected MPC-2350_Firmware and "
        "vendor-libraries in a common ancestor directory."
    )


def find_cpp_compiler():
    compiler = shutil.which("g++")
    if compiler:
        return Path(compiler)
    fallback = Path("C:/msys64/mingw64/bin/g++.exe")
    if fallback.is_file():
        return fallback
    raise FileNotFoundError(
        "The host tests require g++ with C++17 support. Add g++ to PATH "
        "or install MSYS2 MinGW64 at C:/msys64/mingw64."
    )


def executable_path(build_dir, name):
    return Path(build_dir) / (name + (".exe" if os.name == "nt" else ""))


def run_checked(command, compiler=None):
    environment = os.environ.copy()
    if compiler:
        # MinGW executables also need the compiler's runtime DLL directory.
        environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment.get("PATH", "")
    subprocess.run([str(argument) for argument in command], check=True, env=environment)
