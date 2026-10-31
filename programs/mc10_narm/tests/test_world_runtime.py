#!/usr/bin/env python3
"""Minecraft 1.0 player bootstrap and resident-chunk access checks."""

from __future__ import annotations

import math
import os
import struct
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tests"))

from test_region import load_level, read_chunk, tagged  # noqa: E402


def port_floor(value: float) -> int:
    bits = int.from_bytes(struct.pack(">d", value), "big")
    hi, lo = bits >> 32, bits & 0xFFFFFFFF
    sign = hi >> 31
    biased = (hi >> 20) & 0x7FF
    if biased == 0x7FF:
        if hi & 0xFFFFF or lo:
            return 0
        return -0x80000000 if sign else 0x7FFFFFFF
    if biased == 0:
        return -1 if sign and (hi & 0xFFFFF or lo) else 0
    exponent = biased - 1023
    if exponent < 0:
        return -1 if sign else 0
    if exponent > 30:
        return -0x80000000 if sign else 0x7FFFFFFF
    mantissa_hi = (hi & 0xFFFFF) | 0x100000
    shift = 52 - exponent
    if shift >= 32:
        magnitude = mantissa_hi >> (shift - 32)
        mask = 0 if shift == 32 else 0xFFFFFFFF >> (64 - shift)
        fractional = lo != 0 or mantissa_hi & mask != 0
    else:
        magnitude = (mantissa_hi << (32 - shift) | lo >> shift) & 0xFFFFFFFF
        mask = 0xFFFFFFFF >> (32 - shift)
        fractional = lo & mask != 0
    return -magnitude - int(fractional) if sign else magnitude


def test_floor_vectors() -> None:
    vectors = [0.0, -0.0, 0.25, -0.25, 15.999, -15.999, 16.0, -16.0,
               2147483647.0, 2147483648.0, -2147483648.0,
               float("inf"), float("-inf"), float("nan")]
    for value in vectors:
        if math.isnan(value):
            expected = 0
        elif value >= 2147483647.0:
            expected = 0x7FFFFFFF
        elif value <= -2147483648.0:
            expected = -0x80000000
        else:
            expected = math.floor(value)
        assert port_floor(value) == expected, value


def nibble(data: bytes, x: int, y: int, z: int) -> int:
    index = (x << 11) | (z << 7) | y
    value = data[index >> 1]
    return (value >> 4) & 15 if index & 1 else value & 15


def validate_world(world: Path, label: str) -> None:
    data = load_level(world / "level.dat")
    player = tagged(data, "Player", 10)
    pos_type, pos = tagged(player, "Pos", 9)
    motion_type, motion = tagged(player, "Motion", 9)
    rotation_type, rotation = tagged(player, "Rotation", 9)
    assert pos_type == motion_type == 6
    assert rotation_type == 5
    assert len(pos) >= 3 and len(motion) >= 3 and len(rotation) >= 2
    assert all(math.isfinite(value) for value in pos)
    block_x, block_y, block_z = (math.floor(value) for value in pos)
    center_x, center_z = block_x // 16, block_z // 16
    chunks: dict[tuple[int, int], dict] = {}
    raw_total = 0
    entities = tiles = ticks = 0
    for dz in range(-1, 2):
        for dx in range(-1, 2):
            chunk_x, chunk_z = center_x + dx, center_z + dz
            region = world / "region" / f"r.{chunk_x // 32}.{chunk_z // 32}.mcr"
            compression, raw, level = read_chunk(region, chunk_x, chunk_z)
            assert compression in (1, 2)
            assert tagged(level, "xPos", 3) == chunk_x
            assert tagged(level, "zPos", 3) == chunk_z
            blocks = tagged(level, "Blocks", 7)
            metadata = tagged(level, "Data", 7)
            skylight = tagged(level, "SkyLight", 7)
            blocklight = tagged(level, "BlockLight", 7)
            heightmap = tagged(level, "HeightMap", 7)
            assert len(blocks) == 32768 and len(heightmap) == 256
            assert len(metadata) == len(skylight) == len(blocklight) == 16384
            chunks[(chunk_x, chunk_z)] = {
                "blocks": blocks,
                "metadata": metadata,
                "skylight": skylight,
                "blocklight": blocklight,
                "heightmap": heightmap,
            }
            raw_total += len(raw)
            entities += len(tagged(level, "Entities", 9)[1])
            tiles += len(tagged(level, "TileEntities", 9)[1])
            ticks += len(tagged(level, "TileTicks", 9)[1]) if "TileTicks" in level else 0

    def resident(x: int, z: int) -> dict:
        return chunks[(x // 16, z // 16)]

    for (chunk_x, chunk_z), chunk in chunks.items():
        world_x, world_z = chunk_x * 16, chunk_z * 16
        assert resident(world_x, world_z)["blocks"][(0 << 11) | (0 << 7) | 64] == chunk["blocks"][64]
        assert nibble(resident(world_x + 15, world_z + 15)["metadata"], 15, 64, 15) == nibble(chunk["metadata"], 15, 64, 15)
        assert resident(world_x, world_z + 15)["heightmap"][15 << 4] == chunk["heightmap"][15 << 4]
        assert nibble(resident(world_x + 15, world_z)["skylight"], 15, 64, 0) == nibble(chunk["skylight"], 15, 64, 0)
        assert nibble(resident(world_x, world_z + 15)["blocklight"], 0, 64, 15) == nibble(chunk["blocklight"], 0, 64, 15)

    player_chunk = chunks[(center_x, center_z)]
    local_x, local_z = block_x & 15, block_z & 15
    block = player_chunk["blocks"][(local_x << 11) | (local_z << 7) | block_y]
    print(
        f"World runtime {label}: PASS player=({block_x},{block_y},{block_z}) "
        f"chunk=({center_x},{center_z}) resident={len(chunks)}/9 "
        f"raw={raw_total} block={block} entities={entities} tiles={tiles} ticks={ticks}"
    )


if __name__ == "__main__":
    test_floor_vectors()
    owned = Path(os.environ.get("APPDATA", "")) / ".minecraft" / "saves" / "New World"
    assert owned.is_dir(), "owned New World was not found"
    validate_world(owned, "owned")
    staged = PROJECT / "MC10" / "saves" / "New World"
    if staged.is_dir() and (staged / "region").is_dir():
        validate_world(staged, "staged")
