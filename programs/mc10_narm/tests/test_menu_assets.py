"""Orientation and source-composition checks for the Minecraft 1.0 menu."""

import io
import sys
from pathlib import Path
from zipfile import ZipFile

from PIL import Image


PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tools"))

from mc10_asset_pack import (  # noqa: E402
    compose_button,
    font_metrics,
    pack_image,
    select_world_labels,
)


JAR = Path.home() / "AppData/Roaming/.minecraft/versions/1.0/1.0.jar"


def mask_pixel(mask: bytes, width: int, x: int, y: int) -> bool:
    index = y * width + x
    word_offset = (index >> 5) * 4
    word = int.from_bytes(mask[word_offset:word_offset + 4], "little")
    return bool(word & (1 << (31 - (index & 31))))


def test_mask_bit_order_is_left_to_right():
    image = Image.new("RGBA", (8, 1), (0, 0, 0, 0))
    image.putpixel((0, 0), (255, 255, 255, 255))
    _, mask = pack_image(image)
    assert mask_pixel(mask, 8, 0, 0)
    assert not mask_pixel(mask, 8, 7, 0)


def test_font_glyphs_are_not_horizontally_mirrored():
    with ZipFile(JAR) as jar:
        font = Image.open(io.BytesIO(jar.read("font/default.png"))).convert("RGBA")
    _, mask = pack_image(font)
    glyph = ord("F")
    tile_x = (glyph % 16) * 8
    tile_y = (glyph // 16) * 8
    source_rows = []
    packed_rows = []
    for row in range(8):
        source_row = tuple(
            font.getpixel((tile_x + column, tile_y + row))[3] >= 128
            for column in range(8)
        )
        packed_row = tuple(
            mask_pixel(mask, 128, tile_x + column, tile_y + row)
            for column in range(8)
        )
        source_rows.append(source_row)
        packed_rows.append(packed_row)
    assert any(row != row[::-1] for row in source_rows)
    assert packed_rows == source_rows
    assert len(font_metrics(font)) == 256


def test_button_halves_follow_gui_button_source_coordinates():
    with ZipFile(JAR) as jar:
        gui = Image.open(io.BytesIO(jar.read("gui/gui.png"))).convert("RGBA")
    for width in (200, 150, 98, 70):
        half = width // 2
        for state in range(3):
            button = compose_button(gui, width, state)
            source_y = 46 + state * 20
            for x in range(width):
                source_x = x if x < half else 200 - half + x - half
                assert button.getpixel((x, 7)) == gui.getpixel((source_x, source_y + 7))


def test_select_world_labels_come_from_language_file():
    with ZipFile(JAR) as jar:
        payload = select_world_labels(jar.read("lang/en_US.lang"))
    assert payload.split(b"\0")[:-1] == [
        b"Select World",
        b"World",
        b"Must be converted!",
        b"Survival Mode",
        b"Creative Mode",
        b"Hardcore Mode!",
        b"Play Selected World",
        b"Rename",
        b"Delete",
        b"Create New World",
        b"Cancel",
    ]


def test_select_world_background_dimensions():
    with ZipFile(JAR) as jar:
        background = Image.open(io.BytesIO(jar.read("gui/background.png")))
    assert background.size == (16, 16)
    pixels, _ = pack_image(background)
    assert len(pixels) == 256


if __name__ == "__main__":
    test_mask_bit_order_is_left_to_right()
    test_font_glyphs_are_not_horizontally_mirrored()
    test_button_halves_follow_gui_button_source_coordinates()
    test_select_world_labels_come_from_language_file()
    test_select_world_background_dimensions()
    print("Menu asset orientation/composition: PASS")
