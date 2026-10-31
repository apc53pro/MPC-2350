#!/usr/bin/env python3
"""Minecraft 1.0 options hierarchy and persistence compatibility checks."""

from __future__ import annotations

import re
import subprocess
import tempfile
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]

EXPECTED_KEYS = {
    0: -100,
    1: -99,
    2: 17,
    3: 30,
    4: 31,
    5: 32,
    6: 57,
    7: 42,
    8: 16,
    9: 18,
    10: 20,
    11: 15,
    12: -98,
}

EXPECTED_OPTION_LINES = (
    "music", "sound", "invertYMouse", "mouseSensitivity", "fov", "gamma",
    "viewDistance", "guiScale", "particles", "bobView", "anaglyph3d",
    "advancedOpengl", "fpsLimit", "difficulty", "fancyGraphics", "ao",
    "clouds",
    "cursorSpeed", "occlusionCulling",
)


def main() -> None:
    source = (PROJECT / "src/mc10_options.tc").read_text(encoding="utf-8")
    main = (PROJECT / "src/mc10_main.tc").read_text(encoding="utf-8")
    packer = (PROJECT / "tools/mc10_asset_pack.py").read_text(encoding="utf-8")

    defaults = {
        int(index): int(value)
        for index, value in re.findall(
            r"mc_options\.key_codes\[(\d+)\]\s*=\s*(-?\d+);", source
        )
    }
    assert defaults == EXPECTED_KEYS
    for key in EXPECTED_OPTION_LINES:
        assert f'MC_SAVE_' in source and f'("{key}",' in source
    assert 'mc_options_append("skin:Default\\nlastServer:\\n")' in source
    assert 'mc_options_append("key_")' in source

    assert "int options_main[6]" in source
    assert "int options_video[11]" in source
    assert "MC_OPTIONS_KEYS 13" in source
    assert "mc_options_build_screen(MC_OPTIONS_VIDEO)" in source
    assert "mc_options_build_screen(MC_OPTIONS_CONTROLS)" in source
    assert "mc_options_key_duplicate" in source
    assert "mc_options_narm_to_lwjgl" in source
    assert "mc_options_save()" in source
    assert "mc_options_fallback_label" in source
    assert "mc_menu_button_200[MC_MENU_BUTTON_200_WORDS]" in source
    assert "mc_options_run();" in main

    assert '"mc10/options/labels"' in packer
    assert '"mc10/options/slider_knob_8x20"' in packer
    assert '"options.farWarning1", "options.farWarning2"' in packer
    assert 'gui.crop((0, 66, 4, 86))' in packer
    assert 'gui.crop((196, 66, 200, 86))' in packer

    assert 'MC_OPT_CURSOR_SPEED 17' in source and 'MC_OPT_OCCLUSION_CULLING 18' in source
    assert 'mc_menu_set_cursor_speed' in source
    with tempfile.TemporaryDirectory(prefix='mc10-options-') as temporary:
        executable=Path(temporary)/'options.exe'
        built=subprocess.run(['gcc','-O2',str(PROJECT/'tests/options_fixture.c'),'-I',str(PROJECT),'-o',str(executable)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        result=subprocess.run([str(executable)],capture_output=True,text=True)
        assert result.returncode==0,result.stdout+result.stderr
        print(result.stdout.strip())
    print("Options compatibility: PASS screens=3 bindings=13 persisted=21; original options plus explicit cursor/occlusion extensions")


if __name__ == "__main__":
    main()
