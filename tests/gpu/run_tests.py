"""Run the firmware client against a sibling Nano GPU software oracle checkout."""
import argparse
import os
from pathlib import Path
import subprocess

firmware = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--gpu", type=Path, required=True, help="Nano-20K-GPU checkout")
parser.add_argument("--compiler", default="g++")
args = parser.parse_args()
gpu = args.gpu.resolve()
objects = ["setup", "renderer", "hw_mult18x18", "hw_bsram", "hw_tile_pipeline",
           "hw_perspective", "hw_texture", "hw_walker2d", "hw_blit", "hw_sdram",
           "hw_host_link", "hw_scanout", "hw_protocol", "hw_system_copy", "hw_system",
           "hw_reciprocal", "hw_fragment_pipeline"]
build = firmware / "build/gpu-client-tests"
build.mkdir(parents=True, exist_ok=True)
exe = build / ("gpu-client-tests.exe" if os.name == "nt" else "gpu-client-tests")
subprocess.run([args.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", str(firmware), "-I", str(gpu / "reference/include"),
                str(firmware / "tests/gpu/nano_gpu_test.cpp"),
                str(firmware / "nano_gpu.cpp"), str(firmware / "font.cpp"),
                *(str(gpu / "build" / (obj + ".o")) for obj in objects),
                "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
