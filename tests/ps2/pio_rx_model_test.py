"""Execute the assembled PIO words against PS/2 line waveforms at 1 MHz."""
from pathlib import Path
import re
from test_paths import find_project_root

HEADER = find_project_root() / 'MPC-2350_Firmware' / 'ps2_rx.pio.h'
instructions = [int(v, 16) for v in re.findall(r'^\s*(0x[0-9a-f]+),', HEADER.read_text(), re.M)]
assert len(instructions) == 32

def packet(byte, parity_flip=False):
    bits = [(byte >> i) & 1 for i in range(8)]
    return [0] + bits + [1 ^ (sum(bits) & 1) ^ parity_flip, 1]

def run(events, until, period=70, low=35):
    pc = x = y = isr = osr = time = 0
    output = []
    def pins(t):
        clock = data = 1
        for start, bits in events:
            for index, bit in enumerate(bits):
                falling = start + index * period
                if falling - 10 <= t < falling + low + 10:
                    data = bit
                if falling <= t < falling + low:
                    clock = 0
        return clock, data
    while time < until:
        op = instructions[pc]
        major, delay, a, b = op >> 13, (op >> 8) & 31, (op >> 5) & 7, op & 31
        nxt = (pc + 1) % len(instructions)
        clock, data = pins(time)
        if major == 0:  # JMP
            take = {0: True, 3: y == 0, 6: clock == 1}.get(a, False)
            if a == 2:
                take, x = x != 0, (x - 1) & 0xffffffff
            if a == 4:
                take, y = y != 0, (y - 1) & 0xffffffff
            if take:
                nxt = b
        elif major == 2:  # IN pins/OSR, 1
            assert a in (0, 7) and b == 1
            isr = (isr >> 1) | ((data if a == 0 else osr & 1) << 31)
        elif major == 4:  # PUSH block
            assert op & 0xe0 == 0x20
            output.append(isr)
            isr = 0
        elif major == 5:  # MOV ISR,NULL or Y,ISR
            if a == 6 and b == 3:
                isr = 0
            elif a == 2 and b == 6:
                y = isr
            elif a == 7 and b in (3, 11):
                osr = 0 if b == 3 else 0xffffffff
            else:
                raise AssertionError(hex(op))
        elif major == 7:  # SET X/Y
            if a == 1:
                x = b
            elif a == 2:
                y = b
            else:
                raise AssertionError(hex(op))
        else:
            raise AssertionError(hex(op))
        pc, time = nxt, time + 1 + delay
    return output

def decode(frame):
    frame = (frame >> 20) & 0x7ff
    byte = (frame >> 1) & 255
    parity = (frame >> 9) & 1
    valid = not (frame & 1) and bool(frame & 1024) and ((byte.bit_count() & 1) ^ parity) == 1
    return byte, valid

for byte in range(256):
    frames = run([(100, packet(byte))], 1200)
    assert [decode(frame) for frame in frames] == [(byte, True)], (byte, frames)

sequence = [0xe0, 0xf0, 0x75, 0x08, 0xff, 0x81]
events = [(100 + index * 1000, packet(byte)) for index, byte in enumerate(sequence)]
assert [decode(frame) for frame in run(events, 6500)] == [(byte, True) for byte in sequence]
assert decode(run([(100, packet(0x5a, True))], 1200)[0]) == (0x5a, False)

# A truncated frame causes the explicit invalid marker; a later frame recovers.
frames = run([(100, packet(0xab)[:4]), (2000, packet(0x1c))], 3100)
assert frames[0] == 0 and decode(frames[-1]) == (0x1c, True), frames

# Idle input, or a high DATA bit on an isolated clock pulse, publishes no byte.
assert run([], 3000) == []
assert run([(100, [1])], 1000) == []

# Check the wire marker under the clock extremes and the measured mouse clock.
# The quiet time between reports is independent of when the FIFO is read.
for period, low in [(60, 30), (93, 46), (100, 50)]:
    wire = [(3400, packet(0x38)), (3400 + 11 * period, packet(0xff)),
            (13400, packet(0x08)), (13400 + 11 * period, packet(1)),
            (13400 + 22 * period, packet(2))]
    words = run(wire, 17500, period, low)
    assert [decode(word) for word in words] == [
        (0x38, True), (0xff, True), (0x08, True), (1, True), (2, True)]
    assert [word >> 31 for word in words] == [1, 0, 1, 0, 0], words
print('PIO RX model: all 256 bytes, parity, timeout, and inter-report wire markers passed')
