"""Compile every GPU builtin and check its firmware call on emulated Thumb."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
                               UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                               UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_SP)


def check(image):
    text, data, context, stack = 0x100000, 0x200000, 0x300000, 0x400000
    api, submit, stop = context + 0x100, context + 0x200, context + 0x204
    gpu_heap = 0x280000

    def u32(offset):
        return struct.unpack_from("<I", image, offset)[0]

    def align(value, alignment):
        return (value + alignment - 1) & -alignment

    import_offset, import_count, major, minor, caps = struct.unpack_from("<IIHHI", image, 0x58)
    assert (major, minor, caps & 64) == (1, 19, 64)
    assert (0x500, 1, 0) in [
        struct.unpack_from("<IHH", image, import_offset + 8*i) for i in range(import_count)
    ]
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    for address in (text, data, context, stack):
        cpu.mem_map(address, 0x10000)
    for start, size, address in ((0x1c, 0x20, text), (0x28, 0x2c, data),
                                 (0x34, 0x38, data + align(u32(0x2c), u32(0x3c)))):
        if u32(size):
            cpu.mem_write(address, image[u32(start):u32(start) + u32(size)])

    def put(address, value):
        cpu.mem_write(address, struct.pack("<I", value))

    for i in range(u32(0x54)):
        offset, target, kind, _ = struct.unpack_from("<IIHH", image, u32(0x50) + 12*i)
        if kind:
            put(data + offset, {1: data + target, 2: text + target,
                                3: (text + target) | 1, 4: context}[kind])
    put(context + 56, api)
    put(api + 8, submit | 1)
    cpu.mem_write(submit, b"\x70\x47")
    for register, value in ((UC_ARM_REG_R9, data), (UC_ARM_REG_R10, context),
                            (UC_ARM_REG_SP, stack + 0xf000), (UC_ARM_REG_LR, stop | 1)):
        cpu.reg_write(register, value)

    commands = [1, 2, 3, 4, 5, 6, 7, 8, *range(0x100, 0x109), 7]
    commands.insert(-2, 0x107)  # First readiness check reports busy.
    observed = []

    def hook(uc, _address, _size, _data):
        ctx, cmd, pointer, count = [uc.reg_read(r) for r in
                                  (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
        assert ctx == context
        args = struct.unpack(f"<{count}I", uc.mem_read(pointer, count*4)) if count else ()
        if not observed:
            assert cmd == 1 and len(args) == 1 and data <= args[0] < data + 0x10000
        buf = observed[0][1][0] if observed else args[0]
        expected = {
            1: (buf,), 2: (buf,), 3: (gpu_heap, buf, 24), 4: (gpu_heap,),
            5: (0, 0, 1, 0xfffffff3, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0),
            6: (21,), 7: (1, 24) if len(observed) == len(commands) - 1 else (1, 22),
            8: (0, 32, buf), 0x100: (),
            0x101: (buf, 1, 7, 0x1234, 0xffffff, gpu_heap + 32),
            0x102: (buf, gpu_heap + 2432, 5), 0x104: (buf,),
            0x105: (), 0x106: (), 0x107: (), 0x108: (0, 23),
        }
        if cmd == 0x103:
            assert len(args) == 2 and args[0] == buf
            primitive = uc.mem_read(args[1], 336)
            assert struct.unpack_from("<I", primitive, 132)[0] == 1
            assert struct.unpack_from("<I", primitive, 332)[0] == 0xdeadbeef
        else:
            assert args == expected[cmd], (hex(cmd), args)
        uc.reg_write(UC_ARM_REG_R0, int(cmd == 0x108 or
                                       (cmd == 0x107 and (0x107, ()) in observed)))
        observed.append((cmd, args))

    cpu.hook_add(UC_HOOK_CODE, hook, begin=submit, end=submit)
    cpu.emu_start((text + u32(0x18)) | 1, stop, count=1_000_000)
    assert cpu.reg_read(UC_ARM_REG_PC) == stop
    assert cpu.reg_read(UC_ARM_REG_R0) == 0
    assert [cmd for cmd, _ in observed] == commands


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    compiler = args.compiler.resolve()
    build = root / "build"
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build) as temp:
        binary = Path(temp) / "builtins.narm"
        for optimization in range(4):
            subprocess.run([str(compiler), str(root / "tests/gpu/narm_gpu_builtins.tc"),
                            "-I", str(compiler.parent), "-I", str(root / "sdk"),
                            f"-O{optimization}", "-o", str(binary)], check=True)
            check(binary.read_bytes())
    print("PASS NARMCC GPU built-ins and firmware arguments at -O0 through -O3")
