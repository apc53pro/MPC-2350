"""Exercise compiled MMBasic frame scheduling with a delayed GPU service."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from unicorn import UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
                               UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "programs/mc10_narm/tests"))
from native_numeric_probe import invoke
from player_arm import execute

FIXTURE = """
#define main mmbasic_main
#include "mmbasic_v91_narm.tc"
#undef main

void add_line(const char *line) {
    int n = 0;
    while (line[n]) { inputLine[n] = line[n]; ++n; }
    append_program_line(0, n);
}

int main() {
    clear_program();
    clear_vars();
    add_line("10 GCLS 0");
    add_line("20 FOR I=0 TO 39");
    add_line("30 PIXEL 1,1,255");
    add_line("40 NEXT I");
    add_line("50 PRESENT");
    add_line("60 PIXEL 17,17,224");
    add_line("70 PRESENT");
    add_line("80 PIXEL 33,33,28");
    add_line("90 PRESENT");
    add_line("100 END");
    bc_run_program();
    if (errFlag || gdFault || !bcHasPresent || !mmSwapPending) return 1;

    // This edit is staged while the third frame is still in flight.
    mm_finish_present(0);
    mm_fill_rect(49,49,49,49,3);
    if (!mmSwapPending || gdCount != 1 || gdBack >= 0) return 2;
    mm_present();
    mm_wait_present();

    // An implicit frame still updates at PAUSE and can clear stale tiles.
    clear_program();
    add_line("10 GCLS 0");
    add_line("20 PIXEL 65,65,255");
    add_line("30 PAUSE 10");
    add_line("40 END");
    bc_run_program();
    if (errFlag || bcHasPresent || screenDirty) return 3;
    mm_wait_present();

    clear_program();
    initFontTable();
    add_line("PRINT 123");
    add_line("PRINT 123");
    fs_redraw_all();
    mm_present();
    mm_wait_present();
    fsCurLine = 1;
    fsTopLine = 1;
    fs_redraw_all();
    mm_present();
    mm_wait_present();
    return gdFault;
}
"""


class Gpu:
    def __init__(self):
        self.heap = bytearray(0x800000)
        self.back = 1
        self.render_polls = 0
        self.clock = 0
        self.passes = []
        self.copies = []
        self.swaps = []
        self.source_uploads = 0
        self.source_uploads_at_swap = []

    def setup(self, cpu, ctx):
        core, gpu = ctx + 0x100, ctx + 0x200
        service, submit, get_api = ctx + 0x1c00, ctx + 0x1c10, ctx + 0x1c20
        for address, value in ((ctx + 8, core), (ctx + 56, gpu),
                               (core + 16, service | 1), (core + 28, get_api | 1),
                               (gpu + 8, submit | 1)):
            cpu.mem_write(address, struct.pack("<I", value))

        def hook(uc, address, _size, _data):
            cmd, ptr, count = (uc.reg_read(reg) for reg in
                               (UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
            result = 0
            if address == service:
                if cmd == 1:
                    self.clock += 1
                    result = self.clock
                elif cmd == 9:
                    result = 1  # Focus.
                elif cmd == 0x12:
                    result = invoke(uc, ptr, count)
                else:
                    assert cmd == 12  # No system event.
            elif address == get_api:
                assert cmd == 0x101  # No attached mouse.
            else:
                args = struct.unpack(f"<{count}I", uc.mem_read(ptr, count * 4)) if count else ()
                if cmd == 0x107:
                    if self.render_polls:
                        self.render_polls -= 1
                    else:
                        result = 1
                else:
                    if cmd == 7:
                        assert args[0] == self.back
                        self.swaps.append(args)
                        self.source_uploads_at_swap.append(self.source_uploads)
                        self.back ^= 1
                        self.render_polls = 0
                    elif cmd == 0x100:
                        result = self.back
                    elif cmd == 3:
                        dest, src, length = args
                        self.heap[dest:dest + length] = uc.mem_read(src, length)
                        if 0x400000 <= dest < 0x7c0000:
                            self.source_uploads += 1
                    elif cmd == 4:
                        _, target, flags, _, _, table, tiles, _ = struct.unpack_from(
                            "<8I", self.heap, args[0])
                        assert target == self.back and tiles == 300
                        refs = [struct.unpack_from("<I", self.heap, table + i * 8 + 4)[0]
                                for i in range(tiles)]
                        self.passes.append((flags, {i: n for i, n in enumerate(refs) if n}))
                        self.render_polls = 2
                    else:
                        assert cmd == 5 and args[2] == self.back, (cmd, args)
                        if args[0] == 0:
                            assert args[1] == self.back ^ 1
                            self.copies.append(args[3:9])
                        else:
                            assert args[0] == 1 and 0x400000 <= args[9] < 0x7c0000
            uc.reg_write(UC_ARM_REG_R0, result)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

        for address in (service, submit, get_api):
            cpu.mem_write(address, b"\x70\x47")
            cpu.hook_add(UC_HOOK_CODE, hook, begin=address, end=address)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    compiler = parser.parse_args().compiler.resolve()
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build) as temp:
        source = Path(temp) / "frames.tc"
        binary = source.with_suffix(".narm")
        source.write_text(FIXTURE, encoding="utf-8")
        subprocess.run([str(compiler), str(source), "-I", str(compiler.parent),
                        "-I", str(ROOT / "sdk"), "-I", str(ROOT / "programs/mmbasic-v91-source"),
                        "-O2", "--stack-bytes", "16384", "-o", str(binary)], check=True)
        gpu = Gpu()
        result = execute(binary, gpu.setup)
        assert result == 0, result
        assert gpu.passes[:5] == [(7, {0: 40}), (6, {21: 1}), (6, {42: 1}), (6, {63: 1}),
                                  (15, {0: 1, 21: 1, 42: 1, 84: 1})]
        assert gpu.swaps == [(1, 1), (0, 2), (1, 3), (0, 4), (1, 5), (0, 6), (1, 7)]
        assert gpu.source_uploads_at_swap == [0, 0, 0, 0, 0, 14, 14]
        assert gpu.copies == [(0, 0, 0, 0, 640, 480),
                              (32, 32, 32, 32, 32, 32), (64, 64, 64, 64, 32, 32)]
    print("PASS MMBasic frame batching, CPU/GPU overlap, blocking swaps,"
          " dirty tiles and editor row residency")


if __name__ == "__main__":
    main()
