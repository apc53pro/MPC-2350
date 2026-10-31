"""Minecraft 1.0 NoiseGeneratorPerlin constructor reference vectors."""

import hashlib
import struct

from test_java_random import ReferenceRandom


def double_bits(value: float) -> int:
    return struct.unpack(">Q", struct.pack(">d", value))[0]


def construct(seed: int):
    random = ReferenceRandom(seed)
    coords = []
    for _ in range(3):
        integer = (random.next_bits(26) << 27) + random.next_bits(27)
        coords.append(integer / float(1 << 53) * 256.0)
    permutations = list(range(256))
    for index in range(256):
        swap_index = random.next_int(256 - index) + index
        permutations[index], permutations[swap_index] = (
            permutations[swap_index], permutations[index]
        )
        permutations.append(permutations[index])
    return coords, permutations


def test_seed_zero_constructor_vectors():
    coords, permutations = construct(0)
    assert [double_bits(value) for value in coords] == [
        0x406764168EA6CA89,
        0x404EC9E5B3672E14,
        0x406465B93A78EF81,
    ]
    assert permutations[:16] == [
        140, 157, 53, 179, 117, 253, 229, 104,
        247, 35, 20, 221, 25, 51, 211, 18,
    ]
    assert permutations[256:] == permutations[:256]
    assert hashlib.sha256(bytes(permutations)).hexdigest() == (
        "ffaf23b5a65a3556a3018cb7c43f247fa857b948df8858cf97b9dd602ac8fcb3"
    )


if __name__ == "__main__":
    test_seed_zero_constructor_vectors()
    print("NoiseGeneratorPerlin constructor vectors: PASS")

