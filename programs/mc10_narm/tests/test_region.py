#!/usr/bin/env python3
"""Validate the Minecraft 1.0 McRegion spawn-chunk read path."""

from __future__ import annotations

import gzip
import os
import zlib
from pathlib import Path

from test_nbt import parse


def tagged(compound: dict, name: str, expected_type: int):
    tag_type, value = compound[name]
    assert tag_type == expected_type, (name, tag_type)
    return value


def load_level(path: Path) -> dict:
    _, root = parse(gzip.decompress(path.read_bytes()))
    return tagged(root, "Data", 10)


def read_chunk(region_path: Path, chunk_x: int, chunk_z: int):
    data = region_path.read_bytes()
    assert len(data) >= 8192 and len(data) % 4096 == 0
    local_x = chunk_x & 31
    local_z = chunk_z & 31
    entry = (local_x + local_z * 32) * 4
    location = int.from_bytes(data[entry : entry + 4], "big")
    sector_offset = location >> 8
    sector_count = location & 255
    assert sector_offset >= 2 and sector_count > 0
    assert sector_offset + sector_count <= len(data) // 4096
    offset = sector_offset * 4096
    length = int.from_bytes(data[offset : offset + 4], "big")
    compression = data[offset + 4]
    assert 1 <= length <= sector_count * 4096 - 4
    compressed = data[offset + 5 : offset + 4 + length]
    if compression == 1:
        raw = gzip.decompress(compressed)
    elif compression == 2:
        raw = zlib.decompress(compressed)
    else:
        raise AssertionError(f"unsupported compression {compression}")
    _, root = parse(raw)
    return compression, raw, tagged(root, "Level", 10)


def fnv1a(data: bytes) -> int:
    value = 0x811C9DC5
    for byte in data:
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return value


def owned_spawn_test() -> None:
    world = Path(os.environ.get("APPDATA", "")) / ".minecraft" / "saves" / "New World"
    level_path = world / "level.dat"
    if not level_path.is_file():
        print("McRegion owned spawn validation: SKIP (New World not found)")
        return
    info = load_level(level_path)
    spawn_x = tagged(info, "SpawnX", 3)
    spawn_y = tagged(info, "SpawnY", 3)
    spawn_z = tagged(info, "SpawnZ", 3)
    chunk_x = spawn_x // 16
    chunk_z = spawn_z // 16
    region_x = chunk_x // 32
    region_z = chunk_z // 32
    region_path = world / "region" / f"r.{region_x}.{region_z}.mcr"
    assert region_path.is_file(), region_path
    compression, raw, level = read_chunk(region_path, chunk_x, chunk_z)
    assert tagged(level, "xPos", 3) == chunk_x
    assert tagged(level, "zPos", 3) == chunk_z
    blocks = tagged(level, "Blocks", 7)
    metadata = tagged(level, "Data", 7)
    skylight = tagged(level, "SkyLight", 7)
    blocklight = tagged(level, "BlockLight", 7)
    heightmap = tagged(level, "HeightMap", 7)
    assert len(blocks) == 32768
    assert len(metadata) == len(skylight) == len(blocklight) == 16384
    assert len(heightmap) == 256
    local_x, local_z = spawn_x & 15, spawn_z & 15
    block_index = (local_x << 11) | (local_z << 7) | spawn_y
    column = (local_z << 4) | local_x
    entity_count = len(tagged(level, "Entities", 9)[1]) if "Entities" in level else 0
    tile_count = len(tagged(level, "TileEntities", 9)[1]) if "TileEntities" in level else 0
    print(
        "McRegion owned spawn validation: PASS "
        f"chunk=({chunk_x},{chunk_z}) compression={compression} raw={len(raw)} "
        f"block={blocks[block_index]} height={heightmap[column]} "
        f"nonair={sum(byte != 0 for byte in blocks)} fnv={fnv1a(blocks):08x} "
        f"entities={entity_count} tiles={tile_count}"
    )


if __name__ == "__main__":
    owned_spawn_test()
