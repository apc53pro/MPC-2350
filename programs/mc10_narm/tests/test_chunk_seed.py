"""Minecraft 1.0 Chunk.getRandomWithSeed reference vectors."""

from test_java_random import ReferenceRandom


MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1


def java_int(value: int) -> int:
    value &= MASK32
    return value - (1 << 32) if value & (1 << 31) else value


def chunk_random_seed(
    world_seed: int, x_position: int, z_position: int, salt: int
) -> int:
    x_squared = java_int(x_position * x_position)
    x_quadratic = java_int(x_squared * 4_987_142)
    x_linear = java_int(x_position * 5_947_611)
    z_squared = java_int(z_position * z_position)
    z_quadratic = z_squared * 4_392_871
    z_linear = java_int(z_position * 389_711)
    summed = (
        world_seed
        + x_quadratic
        + x_linear
        + z_quadratic
        + z_linear
    ) & MASK64
    return summed ^ (salt & MASK64)


def test_chunk_get_random_with_seed_vectors():
    vectors = (
        (0, 0, 0, 0, 0x0000000000000000, 0xBB20B460),
        (
            1_234_567_890_123_456_789,
            5,
            -7,
            0x1122334455667788,
            0x000023B0C6AEFA48,
            0x25F9C18B,
        ),
        (
            -1,
            50_000,
            -70_000,
            -(1 << 63),
            0x800971480197C0DF,
            0xC780D44B,
        ),
    )
    for world_seed, x, z, salt, expected_seed, expected_first in vectors:
        seed = chunk_random_seed(world_seed, x, z, salt)
        assert seed == expected_seed
        assert ReferenceRandom(seed).next_bits(32) == expected_first


if __name__ == "__main__":
    test_chunk_get_random_with_seed_vectors()
    print("Chunk.getRandomWithSeed vectors: PASS")
