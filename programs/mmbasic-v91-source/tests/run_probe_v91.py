import collections
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R9,
    UC_ARM_REG_R10, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPEXC,
)

def event(kind, x, y, dx=0, dy=0, wheel=0, buttons=0, changed=0):
    return struct.pack("<IiiiiiIII", kind, 2*x, 2*y, dx, dy, wheel, buttons, changed, 0)

def click(x, y):
    return [event(2, x, y, buttons=1, changed=1), event(4, x, y, changed=1)]

def execute(path, scenario):
    image = path.read_bytes()
    u32 = lambda offset: struct.unpack_from("<I", image, offset)[0]
    align = lambda n, a: (n + a - 1) & ~(a - 1)
    text, arena, ctx, stack = 0x100000, 0x2000000, 0x3000000, 0x4000000
    stop, invoke, get, get_state, poll, motion, keys, video, exit_proc = range(ctx+0x1000, ctx+0x1048, 8)
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    cpu.mem_map(text, align(u32(0x20), 4096))
    cpu.mem_map(arena, 0x800000)
    cpu.mem_map(ctx, 0x2000)
    cpu.mem_map(stack, 0x10000)
    cpu.mem_write(text, image[u32(0x1c):u32(0x1c)+u32(0x20)])
    ro_size = u32(0x2c)
    data_off = align(ro_size, u32(0x3c))
    if ro_size:
        cpu.mem_write(arena, image[u32(0x28):u32(0x28)+ro_size])
    if u32(0x38):
        cpu.mem_write(arena+data_off, image[u32(0x34):u32(0x34)+u32(0x38)])

    def write_u32(address, value):
        cpu.mem_write(address, struct.pack("<I", value))

    for i in range(u32(0x54)):
        patch, target, kind, _ = struct.unpack_from("<IIHH", image, u32(0x50)+12*i)
        if not kind:
            continue
        if kind == 1:
            value = arena + target
        elif kind in (2, 3):
            value = (text + target) | (kind == 3)
        else:
            assert kind == 4
            value = ctx
        write_u32(arena+patch, value)

    api, mouse_api, key_api, video_api = (ctx+offset for offset in (0x100, 0x200, 0x300, 0x400))
    for address, value in (
        (ctx+8, api), (api+16, invoke|1), (api+24, exit_proc|1), (api+28, get|1),
        (ctx+52, key_api), (ctx+56, video_api), (key_api+8, keys|1),
        (video_api+28, video|1), (mouse_api+8, get_state|1),
        (mouse_api+12, poll|1), (mouse_api+16, motion|1),
    ):
        write_u32(address, value)
    for handler in range(stop, exit_proc+1, 8):
        cpu.mem_write(handler, b"\x70\x47")
    for register, value in ((UC_ARM_REG_R9, arena), (UC_ARM_REG_R10, ctx),
        (UC_ARM_REG_SP, stack+0xf000), (UC_ARM_REG_LR, stop|1), (UC_ARM_REG_FPEXC, 1<<30)):
        cpu.reg_write(register, value)

    input_events = [event(3, 80, 37, dx=-8, dy=5, buttons=1, changed=1),
                    event(4, 80, 37, changed=1)]
    phase_events = {
        2: [event(2, 19, 30, buttons=1, changed=1)],
        3: [event(1, 31, 46, buttons=1), event(4, 31, 46, changed=1)],
        4: [event(8, 32, 45, wheel=-2)],
        5: click(7, 22),
        6: [event(64, 19, 80, buttons=1), event(1, 90, 80, buttons=1)],
        7: [event(2, 7, 22, buttons=1, changed=1)],
        8: click(170, 235),
    }
    line_events = {
        2: [event(2, 31, 26, buttons=1, changed=1)],
        3: [event(1, 55, 26, buttons=1), event(4, 55, 26, changed=1)],
        4: click(20, 18),
    }
    queue = collections.deque(input_events if scenario in (1, 2) else ())
    frame = millis = phase = motion_reads = modal_stage = 0
    system_events = collections.deque()
    system_polls = 0
    closed = False

    def hook(uc, address, size, _):
        nonlocal frame, millis, phase, motion_reads, modal_stage, system_polls, closed
        if address == stop:
            uc.emu_stop()
            return
        r0, r1, r2, r3 = (uc.reg_read(reg) for reg in
                           (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
        assert r0 == ctx, (hex(address), hex(r0))
        result = 0
        if address == invoke:
            if r1 == 1:
                millis += 10
                result = millis
            elif r1 == 3:
                phase += 1
                result = 10 if scenario == 11 else scenario
                if scenario == 3:
                    queue.extend(phase_events.get(phase, ()))
                    if phase == 7:
                        system_events.extend([2, 1])
                if scenario == 6:
                    queue.extend(line_events.get(phase, ()))
            elif r1 == 12:
                system_polls += 1
                if scenario == 11:
                    result = 3  # NARM_EVENT_CLOSE_REQUESTED
                if system_events:
                    result = system_events.popleft()
            else:
                assert r1 == 0, r1
        elif address == get:
            assert (r1, r2, r3) == (0x0101, 1, 0)
            result = mouse_api
        elif address == get_state:
            flags = 2 if scenario == 5 else 11
            cpu.mem_write(r1, struct.pack("<iiII", 200, 100, 5 if scenario != 5 else 0, flags))
        elif address == poll:
            assert r1 % 4 == 0
            if scenario == 4:
                if frame == 1 and modal_stage == 0:
                    queue.extend(click(20, 234))
                    modal_stage = 1
                elif frame == 2 and modal_stage == 1:
                    # Let fs_wait_enter's bounded stale-input drain finish.
                    queue.clear()
                    modal_stage = 2
                elif frame == 2 and modal_stage == 2:
                    queue.extend(click(20, 99))
                    modal_stage = 3
                elif frame == 3 and modal_stage == 3:
                    queue.extend(click(258, 235))
                    modal_stage = 4
            if queue and (scenario != 2 or r1 < stack):
                cpu.mem_write(r1, queue.popleft())
                result = 1
        elif address == motion:
            cpu.mem_write(r1, struct.pack("<iii", *(-10, 7, -3) if motion_reads == 0 else (0, 0, 0)))
            motion_reads += 1
        elif address == video:
            if r1 == 0:
                frame += 1
        elif address == exit_proc:
            assert scenario == 11
            assert r1 == 0
            closed = True
            uc.emu_stop()
            return
        elif address == keys:
            pass
        else:
            raise AssertionError(("unexpected service", hex(address), r1))
        uc.reg_write(UC_ARM_REG_R0, result & 0xffffffff)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    cpu.hook_add(UC_HOOK_CODE, hook, begin=stop, end=exit_proc)
    cpu.emu_start((text+u32(0x18))|1, stop, count=24_000_000)
    if scenario == 11:
        assert closed and system_polls == 1 and frame == 0, (closed, system_polls, frame)
        print("Scenario 11 PASS (close handled during compute-only loop)")
        return
    if scenario == 10:
        assert 2 <= system_polls <= 8 and frame == 1, (system_polls, frame)
    pc = cpu.reg_read(UC_ARM_REG_PC)
    assert pc == stop, (scenario, "probe timed out", frame, modal_stage, millis, hex(pc))
    assert cpu.reg_read(UC_ARM_REG_R0) == 0, (scenario, cpu.reg_read(UC_ARM_REG_R0), frame)
    print(f"Scenario {scenario} PASS ({frame} presents)")

for scenario in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11):
    execute(Path("tests/probe_v91.narm"), scenario)

