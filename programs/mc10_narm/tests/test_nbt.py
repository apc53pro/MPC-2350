#!/usr/bin/env python3
"""Minecraft 1.0 NBT compatibility vectors and owned-save validation."""

from __future__ import annotations

import gzip
import os
import struct
from pathlib import Path


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def take(self, size: int) -> bytes:
        if size < 0 or self.pos + size > len(self.data):
            raise ValueError("truncated NBT")
        result = self.data[self.pos : self.pos + size]
        self.pos += size
        return result

    def u8(self) -> int:
        return self.take(1)[0]

    def i32(self) -> int:
        return struct.unpack(">i", self.take(4))[0]

    def utf(self) -> str:
        size = struct.unpack(">H", self.take(2))[0]
        raw = self.take(size)
        units: list[int] = []
        i = 0
        while i < len(raw):
            first = raw[i]
            i += 1
            if first & 0x80 == 0:
                units.append(first)
            elif first & 0xE0 == 0xC0:
                if i >= len(raw) or raw[i] & 0xC0 != 0x80:
                    raise ValueError("bad modified UTF-8")
                units.append(((first & 0x1F) << 6) | (raw[i] & 0x3F))
                i += 1
            elif first & 0xF0 == 0xE0:
                if i + 1 >= len(raw) or raw[i] & 0xC0 != 0x80 or raw[i + 1] & 0xC0 != 0x80:
                    raise ValueError("bad modified UTF-8")
                units.append(((first & 15) << 12) | ((raw[i] & 0x3F) << 6) | (raw[i + 1] & 0x3F))
                i += 2
            else:
                raise ValueError("bad modified UTF-8")
        encoded = b"".join(unit.to_bytes(2, "big") for unit in units)
        return encoded.decode("utf-16-be", "surrogatepass")


def payload(reader: Reader, tag_type: int):
    if tag_type == 0:
        return None
    if tag_type == 1:
        return struct.unpack(">b", reader.take(1))[0]
    if tag_type == 2:
        return struct.unpack(">h", reader.take(2))[0]
    if tag_type == 3:
        return reader.i32()
    if tag_type == 4:
        return struct.unpack(">q", reader.take(8))[0]
    if tag_type == 5:
        return struct.unpack(">f", reader.take(4))[0]
    if tag_type == 6:
        return struct.unpack(">d", reader.take(8))[0]
    if tag_type == 7:
        return reader.take(reader.i32())
    if tag_type == 8:
        return reader.utf()
    if tag_type == 9:
        item_type = reader.u8()
        count = reader.i32()
        if not 0 <= item_type <= 10 or count < 0:
            raise ValueError("bad list header")
        return (item_type, [payload(reader, item_type) for _ in range(count)])
    if tag_type == 10:
        result = {}
        while True:
            child_type = reader.u8()
            if child_type == 0:
                return result
            if not 1 <= child_type <= 10:
                raise ValueError(f"post-1.0 tag id {child_type}")
            name = reader.utf()
            result[name] = (child_type, payload(reader, child_type))
    raise ValueError(f"unsupported tag id {tag_type}")


def parse(data: bytes):
    reader = Reader(data)
    tag_type = reader.u8()
    if tag_type != 10:
        raise ValueError("root tag must be a named compound tag")
    name = reader.utf()
    value = payload(reader, tag_type)
    return name, value


def vector_test() -> None:
    raw = (
        b"\x0a\x00\x00"
        b"\x01\x00\x01b\xfe"
        b"\x02\x00\x01s\xff\xfe"
        b"\x03\x00\x01i\xff\xff\xff\xfd"
        b"\x04\x00\x01l\x01\x23\x45\x67\x89\xab\xcd\xef"
        b"\x05\x00\x01f\x3f\xc0\x00\x00"
        b"\x06\x00\x01d\xc0\x04\x00\x00\x00\x00\x00\x00"
        b"\x07\x00\x01a\x00\x00\x00\x03\x00\x80\xff"
        b"\x08\x00\x01t\x00\x04A\xc0\x80B"
        b"\x09\x00\x01q\x03\x00\x00\x00\x02\x00\x00\x00\x07\xff\xff\xff\xf8"
        b"\x0a\x00\x01c\x01\x00\x01x\x01\x00"
        b"\x00"
    )
    name, root = parse(raw)
    assert name == ""
    assert root["b"] == (1, -2)
    assert root["s"] == (2, -2)
    assert root["i"] == (3, -3)
    assert root["l"] == (4, 0x0123456789ABCDEF)
    assert root["f"] == (5, 1.5)
    assert root["d"] == (6, -2.5)
    assert root["a"] == (7, b"\x00\x80\xff")
    assert root["t"] == (8, "A\x00B")
    assert root["q"] == (9, (3, [7, -8]))
    assert root["c"][1]["x"] == (1, 1)
    assert parse(gzip.decompress(gzip.compress(raw)))[1] == root


def owned_save_test() -> None:
    appdata = Path(os.environ.get("APPDATA", ""))
    saves = appdata / ".minecraft" / "saves"
    level_files = sorted(saves.glob("*/level.dat")) if saves.is_dir() else []
    if not level_files:
        print("NBT owned-save validation: SKIP (no level.dat files)")
        return
    checked = 0
    for path in level_files:
        try:
            raw = gzip.decompress(path.read_bytes())
            _, root = parse(raw)
        except ValueError as exc:
            if "post-1.0 tag id" in str(exc):
                continue
            raise
        assert "Data" in root and root["Data"][0] == 10, path
        data = root["Data"][1]
        assert "RandomSeed" in data and data["RandomSeed"][0] == 4, path
        assert all(key in data for key in ("SpawnX", "SpawnY", "SpawnZ")), path
        checked += 1
    assert checked, "no Minecraft 1.0-compatible owned saves were found"
    print(f"NBT owned-save validation: PASS ({checked} level.dat files)")


if __name__ == "__main__":
    vector_test()
    owned_save_test()
    print("NBT tag/gzip vectors: PASS")
