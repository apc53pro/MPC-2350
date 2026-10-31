"""Reference vectors for the independent NARM Java Random implementation."""

MULTIPLIER = 0x5DEECE66D
ADDEND = 0xB
MASK = (1 << 48) - 1


class ReferenceRandom:
    def __init__(self, seed: int):
        self.state = (seed ^ MULTIPLIER) & MASK

    def next_bits(self, bits: int) -> int:
        self.state = (self.state * MULTIPLIER + ADDEND) & MASK
        return self.state >> (48 - bits)

    def next_int(self, bound: int) -> int:
        if bound <= 0:
            raise ValueError("bound must be positive")
        if bound & (bound - 1) == 0:
            return (bound * self.next_bits(31)) >> 31
        while True:
            bits = self.next_bits(31)
            value = bits % bound
            signed_sum = (bits - value + (bound - 1)) & 0xFFFFFFFF
            if signed_sum < 0x80000000:
                return value


def split_step(digits: list[int]) -> list[int]:
    a0, a1, a2, a3 = digits
    carry = 0
    total = a0 * 0x66D + ADDEND
    o0, carry = total & 0xFFF, total >> 12
    total = carry + a0 * 0xECE + a1 * 0x66D
    o1, carry = total & 0xFFF, total >> 12
    total = carry + a0 * 0x5DE + a1 * 0xECE + a2 * 0x66D
    o2, carry = total & 0xFFF, total >> 12
    total = carry + a1 * 0x5DE + a2 * 0xECE + a3 * 0x66D
    o3 = total & 0xFFF
    return [o0, o1, o2, o3]


def split_state(seed: int) -> list[int]:
    state = (seed ^ MULTIPLIER) & MASK
    return [(state >> shift) & 0xFFF for shift in (0, 12, 24, 36)]


def join_state(digits: list[int]) -> int:
    return sum(value << shift for value, shift in zip(digits, (0, 12, 24, 36)))


def test_known_java_seed_zero_vectors():
    random = ReferenceRandom(0)
    values = [random.next_bits(32) for _ in range(8)]
    assert values == [
        0xBB20B460,
        0xD4D95138,
        0x3D93CB7A,
        0x9B3970BE,
        0xA32DC9F6,
        0x4F1DF03A,
        0x8CE970B7,
        0x1DF42503,
    ]


def test_bounded_vectors():
    random = ReferenceRandom(0)
    assert [random.next_int(100) for _ in range(10)] == [
        60, 48, 29, 47, 15, 53, 91, 61, 19, 54
    ]


def test_split_limb_transition_matches_48_bit_reference():
    for seed in (0, 1, 0x12345678, -1, -123456789):
        reference = ReferenceRandom(seed)
        digits = split_state(seed)
        for _ in range(1000):
            reference.next_bits(1)
            digits = split_step(digits)
            assert join_state(digits) == reference.state


if __name__ == "__main__":
    test_known_java_seed_zero_vectors()
    test_bounded_vectors()
    test_split_limb_transition_matches_48_bit_reference()
    print("Java Random compatibility vectors: PASS")

