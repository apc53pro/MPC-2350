#!/usr/bin/env python3
"""Build the MC10 runtime asset package from an owned Minecraft 1.0 install.

The output contains no PNG decoder requirement: images are converted on the
host to NARM's padded-row RGB332 texture format plus optional one-bit masks.
The adjacent JSON manifest records each original resource digest and every
derived presentation adaptation.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from zipfile import ZipFile

from PIL import Image


MAGIC = b"MCA1"
VERSION = 1
HEADER = struct.Struct("<4sIIIIIII")
RECORD = struct.Struct("<IIIIIIIIII")
KIND_IMAGE = 1
KIND_TEXT = 2
FLAG_MASK = 1


class PackError(RuntimeError):
    pass


def fnv1a32(text: str) -> int:
    value = 0x811C9DC5
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 0x01000193) & 0xFFFFFFFF
    return value


def rgb332(r: int, g: int, b: int) -> int:
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)


def pack_image(image: Image.Image) -> tuple[bytes, bytes]:
    rgba = image.convert("RGBA")
    width, height = rgba.size
    row_bytes = ((width + 3) // 4) * 4
    pixels = bytearray(row_bytes * height)
    opaque: list[bool] = []
    source = rgba.load()
    for y in range(height):
        for x in range(width):
            r, g, b, a = source[x, y]
            pixels[y * row_bytes + x] = rgb332(r, g, b)
            opaque.append(a >= 128)

    if all(opaque):
        return bytes(pixels), b""

    words = [0] * ((width * height + 31) // 32)
    for index, is_opaque in enumerate(opaque):
        if is_opaque:
            words[index >> 5] |= 1 << (31 - (index & 31))
    mask = b"".join(struct.pack("<I", word) for word in words)
    return bytes(pixels), mask


@dataclass
class Asset:
    name: str
    kind: int
    width: int
    height: int
    flags: int
    data: bytes
    mask: bytes
    source_name: str
    source_sha256: str
    source_crc32: int
    derived: bool = False


class ResourceSource:
    def __init__(self, jar: Path | None, resources: Path | None):
        self.jar_path = jar
        self.resources = resources
        self.zip = ZipFile(jar) if jar else None

    def close(self) -> None:
        if self.zip:
            self.zip.close()

    def names(self) -> list[str]:
        names: set[str] = set()
        if self.zip:
            for entry in self.zip.infolist():
                name = entry.filename.replace("\\", "/").lstrip("/")
                if not entry.is_dir() and Path(name).suffix.lower() in {".png", ".txt", ".lang"}:
                    names.add(name)
        if self.resources and self.resources.is_dir():
            for path in self.resources.rglob("*"):
                if path.is_file() and path.suffix.lower() in {".png", ".txt", ".lang"}:
                    names.add(path.relative_to(self.resources).as_posix())
        return sorted(names)

    def read(self, name: str) -> bytes:
        if self.resources:
            loose = self.resources.joinpath(*name.split("/"))
            if loose.is_file():
                return loose.read_bytes()
        if self.zip:
            try:
                return self.zip.read(name)
            except KeyError as exc:
                raise PackError(f"resource is missing from JAR: {name}") from exc
        raise PackError(f"resource is missing: {name}")


def image_asset(name: str, data: bytes, *, output_name: str | None = None,
                image: Image.Image | None = None, derived: bool = False) -> Asset:
    decoded = image if image is not None else Image.open(io.BytesIO(data))
    decoded.load()
    pixels, mask = pack_image(decoded)
    return Asset(
        name=output_name or name,
        kind=KIND_IMAGE,
        width=decoded.width,
        height=decoded.height,
        flags=FLAG_MASK if mask else 0,
        data=pixels,
        mask=mask,
        source_name=name,
        source_sha256=hashlib.sha256(data).hexdigest(),
        source_crc32=zlib.crc32(data) & 0xFFFFFFFF,
        derived=derived,
    )


def text_asset(name: str, data: bytes) -> Asset:
    return Asset(
        name=name,
        kind=KIND_TEXT,
        width=0,
        height=0,
        flags=0,
        data=data,
        mask=b"",
        source_name=name,
        source_sha256=hashlib.sha256(data).hexdigest(),
        source_crc32=zlib.crc32(data) & 0xFFFFFFFF,
    )


def derived_binary_asset(name: str, data: bytes, source_name: str,
                         source_data: bytes) -> Asset:
    return Asset(
        name=name,
        kind=KIND_TEXT,
        width=0,
        height=0,
        flags=0,
        data=data,
        mask=b"",
        source_name=source_name,
        source_sha256=hashlib.sha256(source_data).hexdigest(),
        source_crc32=zlib.crc32(source_data) & 0xFFFFFFFF,
        derived=True,
    )


def java_string_hash(text: str) -> int:
    value = 0
    encoded = text.encode("utf-16-be", errors="surrogatepass")
    for offset in range(0, len(encoded), 2):
        unit = (encoded[offset] << 8) | encoded[offset + 1]
        value = (value * 31 + unit) & 0xFFFFFFFF
    return value


def java_trim(text: str) -> str:
    start = 0
    end = len(text)
    while start < end and ord(text[start]) <= 0x20:
        start += 1
    while end > start and ord(text[end - 1]) <= 0x20:
        end -= 1
    return text[start:end]


def font_allowed_characters(data: bytes) -> str:
    result = ""
    for line in data.decode("utf-8", errors="replace").splitlines():
        if not line.startswith("#"):
            result += line
    return result


def font_metrics(image: Image.Image) -> bytes:
    rgba = image.convert("RGBA")
    source = rgba.load()
    widths = bytearray(256)
    for glyph in range(256):
        tile_x = glyph % 16
        tile_y = glyph // 16
        last_column = -1
        for column in range(7, -1, -1):
            if any(source[tile_x * 8 + column, tile_y * 8 + row][2] > 0
                   for row in range(8)):
                last_column = column
                break
        if glyph == 32:
            last_column = 2
        widths[glyph] = last_column + 2
    return bytes(widths)


def menu_labels(language_data: bytes) -> bytes:
    properties: dict[str, str] = {}
    for raw_line in language_data.decode("latin-1").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        properties[key] = value
    keys = (
        "menu.singleplayer",
        "menu.multiplayer",
        "menu.mods",
        "menu.options",
        "menu.quit",
    )
    return b"".join(properties.get(key, key).encode("latin-1") + b"\0"
                    for key in keys)


def select_world_labels(language_data: bytes) -> bytes:
    properties: dict[str, str] = {}
    for raw_line in language_data.decode("latin-1").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        properties[key] = value
    keys = (
        "selectWorld.title",
        "selectWorld.world",
        "selectWorld.conversion",
        "gameMode.survival",
        "gameMode.creative",
        "gameMode.hardcore",
        "selectWorld.select",
        "selectWorld.rename",
        "selectWorld.delete",
        "selectWorld.create",
        "gui.cancel",
    )
    return b"".join(properties.get(key, key).encode("latin-1") + b"\0"
                    for key in keys)


def options_labels(language_data: bytes) -> bytes:
    properties: dict[str, str] = {}
    for raw_line in language_data.decode("latin-1").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        properties[key] = value
    keys = (
        "options.title", "options.music", "options.sound",
        "options.invertMouse", "options.sensitivity", "options.fov",
        "options.difficulty", "options.video", "options.controls", "gui.done",
        "options.videoTitle", "options.graphics", "options.renderDistance",
        "options.ao", "options.framerateLimit", "options.anaglyph",
        "options.viewBobbing", "options.guiScale", "options.advancedOpengl",
        "options.gamma", "options.renderClouds", "options.particles",
        "options.on", "options.off", "options.sensitivity.min",
        "options.sensitivity.max", "options.fov.min", "options.fov.max",
        "options.gamma.min", "options.gamma.max",
        "options.renderDistance.far", "options.renderDistance.normal",
        "options.renderDistance.short", "options.renderDistance.tiny",
        "options.difficulty.peaceful", "options.difficulty.easy",
        "options.difficulty.normal", "options.difficulty.hard",
        "options.guiScale.auto", "options.guiScale.small",
        "options.guiScale.normal", "options.guiScale.large",
        "options.particles.all", "options.particles.decreased",
        "options.particles.minimal", "performance.max",
        "performance.balanced", "performance.powersaver",
        "options.graphics.fancy", "options.graphics.fast", "controls.title",
        "key.attack", "key.use", "key.forward", "key.left", "key.back",
        "key.right", "key.jump", "key.sneak", "key.drop", "key.inventory",
        "key.chat", "key.playerlist", "key.pickItem", "key.mouseButton",
        "options.farWarning1", "options.farWarning2",
    )
    return b"".join(properties.get(key, key).encode("latin-1") + b"\0"
                    for key in keys)


def menu_splashes(splash_data: bytes, allowed: str) -> bytes:
    lines = [java_trim(line) for line in
             splash_data.decode("utf-8", errors="replace").splitlines()]
    lines = [line for line in lines if line]
    payload = bytearray(struct.pack("<I", len(lines)))
    for line in lines:
        glyphs = bytearray()
        for character in line:
            index = allowed.find(character)
            if index >= 0:
                glyphs.append(index + 32)
        if len(glyphs) > 255:
            raise PackError("menu splash is too long for the NARM table")
        payload.extend(struct.pack("<IB", java_string_hash(line), len(glyphs)))
        payload.extend(glyphs)
    return bytes(payload)


def compose_button(gui: Image.Image, width: int, state: int) -> Image.Image:
    height = 20
    half = width // 2
    source_y = 46 + state * 20
    result = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    result.alpha_composite(gui.crop((0, source_y, half, source_y + height)),
                           (0, 0))
    result.alpha_composite(
        gui.crop((200 - half, source_y, 200, source_y + height)),
        (half, 0),
    )
    return result


def derived_assets(source: ResourceSource) -> list[Asset]:
    logo_bytes = source.read("title/mclogo.png")
    logo = Image.open(io.BytesIO(logo_bytes)).convert("RGBA")
    if logo.width < 155 or logo.height < 89:
        raise PackError("title/mclogo.png is smaller than the 1.0 GUI layout expects")
    composed = Image.new("RGBA", (310, 44), (0, 0, 0, 0))
    composed.alpha_composite(logo.crop((0, 0, 155, 44)), (0, 0))
    composed.alpha_composite(logo.crop((0, 45, 155, 89)), (155, 0))

    rare = Image.new("RGBA", (310, 44), (0, 0, 0, 0))
    rare.alpha_composite(logo.crop((0, 0, 99, 44)), (0, 0))
    rare.alpha_composite(logo.crop((129, 0, 156, 44)), (99, 0))
    rare.alpha_composite(logo.crop((126, 0, 129, 44)), (125, 0))
    rare.alpha_composite(logo.crop((99, 0, 125, 44)), (128, 0))
    rare.alpha_composite(logo.crop((0, 45, 155, 89)), (155, 0))

    gui_bytes = source.read("gui/gui.png")
    gui = Image.open(io.BytesIO(gui_bytes)).convert("RGBA")
    font_bytes = source.read("font/default.png")
    font = Image.open(io.BytesIO(font_bytes)).convert("RGBA")
    font_text = source.read("font.txt")
    allowed = font_allowed_characters(font_text)
    splash_bytes = source.read("title/splashes.txt")
    language_bytes = source.read("lang/en_US.lang")

    result = [
        image_asset(
            "title/mclogo.png",
            logo_bytes,
            output_name="mc10/title/logo_310x44",
            image=composed,
            derived=True,
        ),
        image_asset(
            "title/mclogo.png",
            logo_bytes,
            output_name="mc10/title/logo_rare_310x44",
            image=rare,
            derived=True,
        ),
        derived_binary_asset(
            "mc10/font/default_metrics",
            font_metrics(font),
            "font/default.png",
            font_bytes,
        ),
        derived_binary_asset(
            "mc10/menu/labels",
            menu_labels(language_bytes),
            "lang/en_US.lang",
            language_bytes,
        ),
        derived_binary_asset(
            "mc10/select_world/labels",
            select_world_labels(language_bytes),
            "lang/en_US.lang",
            language_bytes,
        ),
        derived_binary_asset(
            "mc10/options/labels",
            options_labels(language_bytes),
            "lang/en_US.lang",
            language_bytes,
        ),
        derived_binary_asset(
            "mc10/menu/splashes",
            menu_splashes(splash_bytes, allowed),
            "title/splashes.txt + font.txt",
            splash_bytes + font_text,
        ),
    ]
    for width in (200, 150, 98, 70):
        for state, state_name in enumerate(("disabled", "normal", "hover")):
            result.append(image_asset(
                "gui/gui.png",
                gui_bytes,
                output_name=f"mc10/menu/button_{width}x20_{state_name}",
                image=compose_button(gui, width, state),
                derived=True,
            ))
    knob = Image.new("RGBA", (8, 20), (0, 0, 0, 0))
    knob.alpha_composite(gui.crop((0, 66, 4, 86)), (0, 0))
    knob.alpha_composite(gui.crop((196, 66, 200, 86)), (4, 0))
    result.append(image_asset(
        "gui/gui.png", gui_bytes,
        output_name="mc10/options/slider_knob_8x20",
        image=knob, derived=True,
    ))
    return result


def align4(value: int) -> int:
    return (value + 3) & ~3


def build_pack(jar: Path | None, resources: Path | None, output: Path,
               manifest_path: Path) -> None:
    source = ResourceSource(jar, resources)
    try:
        names = source.names()
        if "terrain.png" not in names:
            raise PackError("terrain.png was not found in the selected Minecraft 1.0 resources")
        if "title/mclogo.png" not in names:
            raise PackError("complete resources are required; title/mclogo.png is missing")

        assets: list[Asset] = []
        for name in names:
            data = source.read(name)
            if name.lower().endswith(".png"):
                assets.append(image_asset(name, data))
            else:
                assets.append(text_asset(name, data))
        assets.extend(derived_assets(source))
        assets.sort(key=lambda asset: asset.name)

        seen_hashes: dict[int, str] = {}
        for asset in assets:
            value = fnv1a32(asset.name)
            previous = seen_hashes.get(value)
            if previous is not None:
                raise PackError(f"FNV-1a collision: {previous!r} and {asset.name!r}")
            seen_hashes[value] = asset.name

        directory_offset = HEADER.size
        payload_offset = align4(directory_offset + len(assets) * RECORD.size)
        payload = bytearray()
        records: list[bytes] = []
        manifest_assets: list[dict[str, object]] = []

        for asset in assets:
            while (payload_offset + len(payload)) & 3:
                payload.append(0)
            data_offset = payload_offset + len(payload)
            payload.extend(asset.data)
            while (payload_offset + len(payload)) & 3:
                payload.append(0)
            mask_offset = payload_offset + len(payload) if asset.mask else 0
            payload.extend(asset.mask)

            records.append(RECORD.pack(
                fnv1a32(asset.name),
                asset.kind,
                asset.width,
                asset.height,
                asset.flags,
                data_offset,
                len(asset.data),
                mask_offset,
                len(asset.mask),
                asset.source_crc32,
            ))
            manifest_assets.append({
                "name": asset.name,
                "name_hash": f"0x{fnv1a32(asset.name):08x}",
                "kind": "image" if asset.kind == KIND_IMAGE else "text",
                "width": asset.width,
                "height": asset.height,
                "rgb332_bytes": len(asset.data) if asset.kind == KIND_IMAGE else 0,
                "mask_bytes": len(asset.mask),
                "source": asset.source_name,
                "source_sha256": asset.source_sha256,
                "derived": asset.derived,
            })

        source_bytes = jar.read_bytes() if jar else b""
        header = HEADER.pack(
            MAGIC,
            VERSION,
            len(assets),
            RECORD.size,
            directory_offset,
            payload_offset,
            zlib.crc32(source_bytes) & 0xFFFFFFFF,
            0,
        )
        blob = bytearray(header)
        blob.extend(b"".join(records))
        blob.extend(b"\0" * (payload_offset - len(blob)))
        blob.extend(payload)

        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(blob)
        manifest_path.write_text(json.dumps({
            "format": "MC10 asset pack v1",
            "source_jar": str(jar) if jar else None,
            "source_jar_sha256": hashlib.sha256(source_bytes).hexdigest() if source_bytes else None,
            "resource_root": str(resources) if resources else None,
            "record_count": len(assets),
            "package_bytes": len(blob),
            "package_sha256": hashlib.sha256(blob).hexdigest(),
            "assets": manifest_assets,
        }, indent=2) + "\n", encoding="utf-8")
        print(f"packed {len(assets)} Minecraft 1.0 resources -> {output} ({len(blob)} bytes)")
    finally:
        source.close()


def inspect_pack(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise PackError("file is shorter than the asset header")
    magic, version, count, record_size, directory_offset, payload_offset, source_crc, flags = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION or record_size != RECORD.size:
        raise PackError("unsupported or corrupt MC10 asset pack")
    if directory_offset + count * record_size > len(data) or payload_offset > len(data):
        raise PackError("asset directory is out of bounds")
    images = 0
    texts = 0
    masked = 0
    for index in range(count):
        fields = RECORD.unpack_from(data, directory_offset + index * record_size)
        _, kind, width, height, record_flags, data_offset, data_bytes, mask_offset, mask_bytes, _ = fields
        if data_offset + data_bytes > len(data):
            raise PackError(f"record {index} data is out of bounds")
        if mask_bytes and (mask_offset + mask_bytes > len(data)):
            raise PackError(f"record {index} mask is out of bounds")
        if kind == KIND_IMAGE:
            images += 1
            expected = ((width + 3) // 4) * 4 * height
            if data_bytes != expected:
                raise PackError(f"record {index} has invalid padded image length")
            if record_flags & FLAG_MASK:
                masked += 1
                if mask_bytes != ((width * height + 31) // 32) * 4:
                    raise PackError(f"record {index} has invalid mask length")
        elif kind == KIND_TEXT:
            texts += 1
        else:
            raise PackError(f"record {index} has unknown kind {kind}")
    print(json.dumps({
        "path": str(path),
        "bytes": len(data),
        "version": version,
        "records": count,
        "images": images,
        "masked_images": masked,
        "text_records": texts,
        "source_crc32": f"0x{source_crc:08x}",
        "flags": flags,
        "sha256": hashlib.sha256(data).hexdigest(),
    }, indent=2))


def default_jar() -> Path:
    return Path.home() / "AppData/Roaming/.minecraft/versions/1.0/1.0.jar"


def default_resources() -> Path:
    return Path.home() / "AppData/Roaming/.minecraft/versions/1.0/minecraft/src"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    pack = sub.add_parser("pack")
    pack.add_argument("--jar", type=Path, default=default_jar())
    pack.add_argument("--resources", type=Path, default=default_resources())
    pack.add_argument("--output", type=Path, required=True)
    pack.add_argument("--manifest", type=Path, required=True)
    inspect = sub.add_parser("inspect")
    inspect.add_argument("path", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "pack":
            jar = args.jar if args.jar.is_file() else None
            resources = args.resources if args.resources.is_dir() else None
            if jar is None and resources is None:
                raise PackError("neither the Minecraft 1.0 JAR nor resource directory exists")
            build_pack(jar, resources, args.output, args.manifest)
        else:
            inspect_pack(args.path)
        return 0
    except (PackError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

