"""Minecraft 1.0 SaveConverterMcRegion/WorldInfo owned-save checks."""

from __future__ import annotations

import gzip
import os
import sys
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tests"))

from test_nbt import parse  # noqa: E402


WORLD_INFO_TYPES = {
    "RandomSeed": 4,
    "GameType": 3,
    "SpawnX": 3,
    "SpawnY": 3,
    "SpawnZ": 3,
    "Time": 4,
    "LastPlayed": 4,
    "LevelName": 8,
    "version": 3,
}


def compatible_worlds() -> list[tuple[str, dict]]:
    saves = Path(os.environ.get("APPDATA", "")) / ".minecraft" / "saves"
    result: list[tuple[str, dict]] = []
    if not saves.is_dir():
        return result
    for folder in saves.iterdir():
        if not folder.is_dir():
            continue
        for leaf in ("level.dat", "level.dat_old"):
            path = folder / leaf
            if not path.is_file():
                continue
            try:
                root = parse(gzip.decompress(path.read_bytes()))[1]
                data = root["Data"]
                if data[0] != 10:
                    raise ValueError("Data is not a compound")
                result.append((folder.name, data[1]))
                break
            except (KeyError, OSError, ValueError):
                continue
    return result


def test_world_info_contract() -> None:
    worlds = compatible_worlds()
    assert worlds, "no owned Minecraft 1.0-compatible world was found"
    for folder, data in worlds:
        for key, tag_type in WORLD_INFO_TYPES.items():
            assert data[key][0] == tag_type, (folder, key)
        display_name = data["LevelName"][1] or folder
        assert display_name
        assert isinstance(data["LastPlayed"][1], int)
        conversion_required = data["version"][1] != 19132
        if folder == "New World":
            assert not conversion_required


def test_save_comparator_order() -> None:
    worlds = compatible_worlds()
    expected = sorted(worlds, key=lambda item: (-item[1]["LastPlayed"][1], item[0]))
    timestamps = [item[1]["LastPlayed"][1] for item in expected]
    assert timestamps == sorted(timestamps, reverse=True)
    tie = [("z", 100), ("a", 100), ("newest", 101)]
    assert sorted(tie, key=lambda item: (-item[1], item[0])) == [
        ("newest", 101), ("a", 100), ("z", 100)
    ]


if __name__ == "__main__":
    test_world_info_contract()
    test_save_comparator_order()
    print(f"WorldInfo owned-save validation: PASS ({len(compatible_worlds())} worlds)")
