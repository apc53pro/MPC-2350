#!/usr/bin/env python3
"""Resident Block registry and first static terrain renderer checks."""

from __future__ import annotations

import math
import re
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tests"))

from test_region import load_level, read_chunk, tagged  # noqa: E402


EXPECTED = {
    1: (4, 1, 0, 1, 1, 255, 0),
    2: (1, 3, 0, 1, 1, 255, 0),
    3: (2, 2, 0, 1, 1, 255, 0),
    4: (4, 16, 0, 1, 1, 255, 0),
    5: (3, 4, 0, 1, 1, 255, 0),
    7: (4, 17, 0, 1, 1, 255, 0),
    9: (6, 205, 4, 0, 0, 3, 0),
    10: (7, 237, 4, 0, 0, 255, 15),
    11: (7, 237, 4, 0, 0, 255, 15),
    13: (14, 19, 0, 1, 1, 255, 0),
    14: (4, 32, 0, 1, 1, 255, 0),
    15: (4, 33, 0, 1, 1, 255, 0),
    16: (4, 34, 0, 1, 1, 255, 0),
    17: (3, 20, 0, 1, 1, 255, 0),
    21: (4, 160, 0, 1, 1, 255, 0),
    31: (9, 39, 1, 0, 0, 0, 0),
    35: (12, 64, 0, 1, 1, 255, 0),
    37: (9, 13, 1, 0, 0, 0, 0),
    43: (4, 6, 0, 1, 1, 255, 0),
    50: (15, 80, 2, 0, 0, 0, 14),
    53: (3, 4, 10, 0, 0, 255, 0),
    56: (4, 50, 0, 1, 1, 255, 0),
    58: (3, 59, 0, 1, 1, 255, 0),
    59: (9, 88, 6, 0, 0, 0, 0),
    60: (2, 87, 0, 0, 0, 255, 0),
    64: (3, 97, 7, 0, 0, 0, 0),
    65: (15, 83, 8, 0, 0, 0, 0),
    67: (4, 16, 10, 0, 0, 255, 0),
    72: (3, 4, 0, 0, 0, 0, 0),
    73: (4, 51, 0, 1, 1, 255, 0),
    85: (3, 4, 11, 0, 0, 0, 0),
    102: (16, 49, 18, 0, 0, 0, 0),
}


def parsed_registry() -> dict[int, tuple[int, ...]]:
    text = (PROJECT / "src/mc10_blocks.tc").read_text(encoding="utf-8")
    material_ids = {
        name: int(value)
        for name, value in re.findall(r"#define (MC_MATERIAL_[A-Z_]+) (\d+)", text)
    }
    records: dict[int, tuple[int, ...]] = {}
    pattern = re.compile(
        r"mc_block_register\((\d+),\s*(MC_MATERIAL_[A-Z_]+),\s*"
        r"(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\);"
    )
    for match in pattern.finditer(text):
        block_id = int(match.group(1))
        records[block_id] = (material_ids[match.group(2)],) + tuple(
            int(value) for value in match.groups()[2:]
        )
    return records


def resident_ids() -> set[int]:
    world = PROJECT / "MC10/saves/New World"
    data = load_level(world / "level.dat")
    player = tagged(data, "Player", 10)
    position = tagged(player, "Pos", 9)[1]
    block_x, _, block_z = (math.floor(value) for value in position)
    center_x, center_z = block_x // 16, block_z // 16
    result: set[int] = set()
    for dz in range(-1, 2):
        for dx in range(-1, 2):
            x, z = center_x + dx, center_z + dz
            region = world / "region" / f"r.{x // 32}.{z // 32}.mcr"
            _, _, level = read_chunk(region, x, z)
            result.update(tagged(level, "Blocks", 7))
    result.discard(0)
    return result


def main() -> None:
    registry = parsed_registry()
    assert registry == EXPECTED
    block_source = (PROJECT / "src/mc10_blocks.tc").read_text(encoding="utf-8")
    assert "int liquid; int solid; int blocks_movement;" in block_source
    assert "mc_materials[MC_MATERIAL_WEB].blocks_movement = 0;" in block_source
    ids = resident_ids()
    assert ids <= registry.keys(), sorted(ids - registry.keys())
    renderer = (PROJECT / "src/mc10_render.tc").read_text(encoding="utf-8")
    assert 'mc_asset_load_image("terrain.png")' in renderer
    assert 'mc_asset_load_image("misc/grasscolor.png")' in renderer
    assert "mc_render_grass_color" in renderer
    assert "[127*256+127]" in renderer
    assert "mc_render_texture_override=38" in renderer
    assert "px>=96 && px<128" in renderer
    assert "px>=128 && px<144" in renderer
    assert "mc_options.fancy_graphics" in renderer
    for decoder_symbol in ("stbi_load", "png_decode", "inflate_png", "lodepng"):
        assert decoder_symbol not in renderer.lower()
    for render_type in (0, 1, 2, 4, 6, 7, 8, 10, 11, 18):
        assert f"render_type == {render_type}" in renderer
    assert "mc_render_apply_grass_tint();" in renderer
    assert "&mc_render_light_atlas[mc_render_atlas_variant*16384], 256, 256, 0" in renderer
    assert "mc_render_isolation_mode" not in renderer
    assert "mc_render_door(id,metadata,x,y,z)" in renderer
    assert "mc_render_torch(metadata,x,y,z)" in renderer
    assert "mc_render_fluid(id,metadata,x,y,z)" in renderer
    assert "mc_render_ladder(metadata,x,y,z)" in renderer
    assert "mc_render_pane(id,metadata,x,y,z)" in renderer
    # Device-confirmed projection regression guard. Do not substitute the
    # TinyCraft projection or make the static frame's far plane option-driven.
    assert "mc_render_camera[12] = 171.375f" in renderer
    assert "mc_render_camera[13] = 171.375f" in renderer
    assert "mc_render_camera[17] = 48.0f" in renderer
    assert "mc_render_camera[12] = focal" not in renderer
    assert "256 >> mc_options.render_distance" not in renderer
    menu = (PROJECT / "src/mc10_menu.tc").read_text(encoding="utf-8")
    assert "mc_menu_pointer_interval_ms = 8" in menu
    assert "mc_menu_pointer_pending_ms/mc_menu_pointer_interval_ms" in menu
    assert "mc_menu_pointer_pending_ms%mc_menu_pointer_interval_ms" in menu
    assert "delta_x = -2" in menu and "delta_y = -2" in menu
    print(
        f"Block/renderer compatibility: PASS registry={len(registry)} "
        f"resident_ids={len(ids)} rendered_types=0,1,2,4,6,7,8,10,11,18"
    )


if __name__ == "__main__":
    main()
