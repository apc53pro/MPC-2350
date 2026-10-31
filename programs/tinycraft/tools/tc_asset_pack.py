#!/usr/bin/env python3
"""Pack TinyCraft editable packs into the deployable Tinycraft/assets.tca.

This tool targets the TCAC v8 format loaded by TinyCraft v58. It accepts PNG
and raw RGB332/RG332 texture files, converts them into one-byte-per-pixel
RGB332 payloads, packs optional baked custom item meshes, can derive flat item
meshes and 1-bit alpha masks from item PNGs, and writes the binary cache
expected by the VM runtime.
"""

import argparse
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

from tc_pack_loader import PackLayoutError, load_pack_pair


MAGIC = b"TCAC"
VERSION = 8
HEADER_BYTES = 16
RECORD_BYTES = 32
MAX_RECORDS = 64
ALPHA_NONE = 0
ALPHA_MASK = 1
RECORD_KIND_TEXTURE = 0
RECORD_KIND_ITEM_MESH = 1
RECORD_KIND_BLOCK_DEF = 2
RECORD_KIND_BLOCK_MODEL = 3
RECORD_KIND_ENTITY_DEF = 4
RECORD_KIND_ENTITY_MODEL = 5
RECORD_KIND_DESTROY_STAGES = 6
RECORD_KIND_SHIFT = 24
RECORD_KIND_ITEM_MESH_FLAG = RECORD_KIND_ITEM_MESH << RECORD_KIND_SHIFT
RECORD_KIND_BLOCK_DEF_FLAG = RECORD_KIND_BLOCK_DEF << RECORD_KIND_SHIFT
RECORD_KIND_BLOCK_MODEL_FLAG = RECORD_KIND_BLOCK_MODEL << RECORD_KIND_SHIFT
RECORD_KIND_ENTITY_DEF_FLAG = RECORD_KIND_ENTITY_DEF << RECORD_KIND_SHIFT
RECORD_KIND_ENTITY_MODEL_FLAG = RECORD_KIND_ENTITY_MODEL << RECORD_KIND_SHIFT
RECORD_KIND_DESTROY_STAGES_FLAG = RECORD_KIND_DESTROY_STAGES << RECORD_KIND_SHIFT
BLOCK_DEF_BYTES = 32
ENTITY_DEF_BYTES = 32
ENTITY_MODEL_PART_BYTES = 32
ITEM_MESH_FACE_BYTES = 20
BLOCK_MODEL_FACE_BYTES = 20
ITEM_MESH_RENDER_KIND = 4
BLOCK_MODEL_RENDER_KIND = 2
ENTITY_MODEL_RENDER_KIND = 2
TEXTURE_MASK_BYTES = 32
MAX_ITEM_MESH_FACES_PER_RECORD = 128
MAX_ITEM_MESH_FACES_TOTAL = 640
MAX_BLOCK_MODEL_FACES_PER_RECORD = 128
MAX_BLOCK_MODEL_FACES_TOTAL = 384
MAX_ENTITY_MODEL_PARTS_PER_RECORD = 16
MAX_ENTITY_MODEL_PARTS_TOTAL = 64
DESTROY_STAGE_COUNT = 10
DESTROY_STAGE_WIDTH = 16
DESTROY_STAGE_HEIGHT = 16
DESTROY_STAGE_TEXTURE_BYTES = 256
DESTROY_STAGE_MASK_BYTES = 32
DESTROY_STAGE_BYTES = DESTROY_STAGE_TEXTURE_BYTES + DESTROY_STAGE_MASK_BYTES

DEFAULT_ENTITY_IDS = {
    "none": 0,
    "pig": 1,
    "entity/pig": 1,
    "zombie": 2,
    "entity/zombie": 2,
    "cow": 3,
    "entity/cow": 3,
    "skeleton": 4,
    "entity/skeleton": 4,
    "creeper": 5,
    "entity/creeper": 5,
    "steve": 6,
    "entity/steve": 6,
}

ENTITY_RENDER_KINDS = {
    "none": 0,
    "cuboids": 1,
    "entity_cuboids": 1,
    "pig_cuboids": 1,
    "pig": 1,
}

ENTITY_BEHAVIOR_KINDS = {
    "none": 0,
    "controller_passive_00": 1,
    "passive_wander": 1,
    "passive_00": 1,
    "pig": 1,
    "controller_aggressive_00": 2,
    "aggressive_00": 2,
    "hostile_00": 2,
    "zombie": 2,
}

ENTITY_MODEL_KINDS = {
    "none": 0,
    "pig": 1,
    "entity/pig": 1,
    "zombie": 2,
    "entity/zombie": 2,
    "cow": 3,
    "entity/cow": 3,
    "skeleton": 4,
    "entity/skeleton": 4,
    "creeper": 5,
    "entity/creeper": 5,
    "steve": 6,
    "entity/steve": 6,
}

ENTITY_PART_KINDS = {
    "none": 0,
    "body": 1,
    "box": 1,
    "cuboid": 1,
    "head": 2,
    "leg": 3,
    "body_rot_x": 4,
    "body_rotate_x": 4,
}

ENTITY_PART_ANIMS = {
    "none": 0,
    "leg_pos": 1,
    "leg_positive": 1,
    "leg_neg": 2,
    "leg_negative": 2,
}

DEFAULT_TEXTURE_IDS = {
    "missing": 0,
    "grass_top": 1,
    "grass_side": 2,
    "dirt": 3,
    "stone": 4,
    "log_side": 5,
    "log_top": 6,
    "leaves": 7,
    "oak_planks": 8,
    "crafting_table_top": 9,
    "crafting_table_side": 10,
    "entity/pig": 11,
    "pig": 11,
    "item/stick": 12,
    "item_stick": 12,
    "stick": 12,
    "item/wood_pickaxe": 13,
    "wood_pickaxe": 13,
    "wooden_pickaxe": 13,
    "item/wood_sword": 14,
    "wood_sword": 14,
    "wooden_sword": 14,
    "item/wood_shovel": 15,
    "wood_shovel": 15,
    "wooden_shovel": 15,
    "item/wood_axe": 16,
    "wood_axe": 16,
    "wooden_axe": 16,
    "entity/zombie": 17,
    "zombie": 17,
    "entity/cow": 18,
    "cow": 18,
    "entity/skeleton": 19,
    "skeleton": 19,
    "cobblestone": 20,
    "item/raw_porkchop": 21,
    "raw_porkchop": 21,
    "porkchop_raw": 21,
    "entity/creeper": 25,
    "creeper": 25,
    "entity/steve": 26,
    "steve": 26,
}

ENTITY_TEXTURE_SIZES = {
    "entity/pig": (64, 32),
    "pig": (64, 32),
    "entity/zombie": (64, 32),
    "zombie": (64, 32),
    "entity/cow": (64, 32),
    "cow": (64, 32),
    "entity/skeleton": (64, 32),
    "skeleton": (64, 32),
    "entity/creeper": (64, 32),
    "creeper": (64, 32),
    "entity/steve": (64, 32),
    "steve": (64, 32),
}

ENTITY_TEXTURE_ID_VALUES = {11, 17, 18, 19, 25, 26}

DEFAULT_ITEM_IDS = {
    "air": 0,
    "grass": 1,
    "grass_block": 1,
    "dirt": 2,
    "stone": 3,
    "wood": 4,
    "oak_wood": 4,
    "log": 4,
    "oak_log": 4,
    "leaves": 5,
    "oak_leaves": 5,
    "oak_planks": 6,
    "planks": 6,
    "crafting_table": 7,
    "cobblestone": 8,
    "cobble": 8,
    "stick": 20,
    "wooden_pickaxe": 21,
    "pickaxe": 21,
    "wooden_sword": 22,
    "sword": 22,
    "wooden_shovel": 23,
    "shovel": 23,
    "wooden_axe": 24,
    "axe": 24,
    "raw_porkchop": 25,
    "porkchop_raw": 25,
    "porkchop": 25,
}

DEFAULT_BLOCK_IDS = {
    "air": 0,
    "grass": 1,
    "grass_block": 1,
    "dirt": 2,
    "stone": 3,
    "wood": 4,
    "oak_wood": 4,
    "log": 4,
    "oak_log": 4,
    "leaves": 5,
    "oak_leaves": 5,
    "oak_planks": 6,
    "planks": 6,
    "crafting_table": 7,
    "cobblestone": 8,
    "cobble": 8,
}

DEFAULT_NAME_LABELS = {
    "none": 0,
    "grass": 1,
    "grass_block": 1,
    "dirt": 2,
    "stone": 3,
    "wood": 4,
    "oak_wood": 4,
    "log": 4,
    "oak_log": 4,
    "leaves": 5,
    "oak_leaves": 5,
    "oak_planks": 6,
    "planks": 6,
    "crafting_table": 7,
    "cobblestone": 8,
    "cobble": 8,
    "stick": 20,
    "wooden_pickaxe": 21,
    "wooden_sword": 22,
    "wooden_shovel": 23,
    "wooden_axe": 24,
    "raw_porkchop": 25,
    "porkchop_raw": 25,
    "porkchop": 25,
}

TOOL_KINDS = {
    "none": 0,
    "pickaxe": 1,
    "axe": 2,
    "shovel": 3,
    "sword": 4,
}

BLOCK_RENDER_CLASSES = {
    "none": 0,
    "fast_cube": 1,
    "cube": 1,
    "custom_model": 2,
}

BLOCK_FLAGS = {
    "solid": 1,
    "placeable": 2,
    "stops_sky": 4,
    "opens_crafting": 8,
}

DEFAULT_FLAT_ITEM_TEXTURES = {
    "stick": "item/stick",
    "wooden_pickaxe": "item/wood_pickaxe",
    "wooden_sword": "item/wood_sword",
    "wooden_shovel": "item/wood_shovel",
    "wooden_axe": "item/wood_axe",
    "raw_porkchop": "item/raw_porkchop",
}

DEFAULT_FLAT_ITEM_SOURCES = {
    "stick": "textures/items/png/stick.png",
    "wooden_pickaxe": "textures/items/png/wood_pickaxe.png",
    "wooden_sword": "textures/items/png/wood_sword.png",
    "wooden_shovel": "textures/items/png/wood_shovel.png",
    "wooden_axe": "textures/items/png/wood_axe.png",
    "raw_porkchop": "textures/items/png/porkchop_raw.png",
}


@dataclass
class TextureRecord:
    name: str
    source: Path
    texture_id: int
    width: int
    height: int
    payload: bytes
    alpha_kind: int = ALPHA_NONE
    flags: int = 0

    @property
    def word_count(self):
        return len(self.payload) // 4


@dataclass
class ItemMeshRecord:
    name: str
    item_id: int
    icon_texture_id: int
    payload: bytes
    face_count: int
    flags: int = 0

    @property
    def word_count(self):
        return len(self.payload) // 4


@dataclass
class BlockRecord:
    name: str
    block_id: int
    payload: bytes
    flags: int = 0

    @property
    def word_count(self):
        return len(self.payload) // 4


@dataclass
class BlockModelRecord:
    name: str
    block_id: int
    payload: bytes
    face_count: int
    flags: int = 0

    @property
    def word_count(self):
        return len(self.payload) // 4


@dataclass
class EntityRecord:
    name: str
    entity_id: int
    render_kind: int
    texture_id: int
    payload: bytes
    flags: int = 0

    @property
    def word_count(self):
        return len(self.payload) // 4


@dataclass
class EntityModelRecord:
    name: str
    model_id: int
    texture_id: int
    payload: bytes
    part_count: int
    flags: int = 0

    @property
    def word_count(self):
        return len(self.payload) // 4


@dataclass
class DestroyStagesRecord:
    name: str
    sources: list
    payload: bytes
    opaque_counts: list
    flags: int = 0

    @property
    def word_count(self):
        return len(self.payload) // 4


class PackError(Exception):
    pass


def normalize_key(value):
    key = str(value).strip().replace("\\", "/").replace("-", "_").replace(" ", "_").lower()
    if key.startswith("tc_tex_"):
        key = key[7:]
    return key


def load_texture_ids(path):
    ids = dict(DEFAULT_TEXTURE_IDS)
    if path is None:
        default_path = Path(__file__).with_name("texture_ids.json")
        path = default_path if default_path.exists() else None
    if path is None:
        return ids

    data = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    for key, value in data.items():
        if isinstance(value, dict):
            value = value.get("id")
        if not isinstance(value, int):
            raise PackError(f"texture id mapping for {key!r} must be an integer")
        ids[normalize_key(key)] = value
    return ids


def resolve_texture_id(value, texture_ids):
    if isinstance(value, int):
        tex_id = value
    else:
        text = str(value).strip()
        if text.isdigit():
            tex_id = int(text)
        else:
            key = normalize_key(text)
            if key not in texture_ids:
                names = ", ".join(sorted(texture_ids.keys()))
                raise PackError(f"unknown texture id/name {value!r}; known names: {names}")
            tex_id = texture_ids[key]

    if tex_id < 0 or tex_id > 31:
        raise PackError(f"texture id {tex_id} is outside the v53 runtime range 0..31")
    return tex_id


def resolve_item_id(value):
    if isinstance(value, int):
        item_id = value
    else:
        text = str(value).strip()
        if text.isdigit():
            item_id = int(text)
        else:
            key = normalize_key(text)
            if key.startswith("tinycraft:"):
                key = key.split(":", 1)[1]
            if key not in DEFAULT_ITEM_IDS:
                names = ", ".join(sorted(DEFAULT_ITEM_IDS.keys()))
                raise PackError(f"unknown item id/name {value!r}; known names: {names}")
            item_id = DEFAULT_ITEM_IDS[key]

    if item_id < 0 or item_id > 31:
        raise PackError(f"item id {item_id} is outside the v53 runtime range 0..31")
    return item_id


def resolve_block_id(value):
    if isinstance(value, int):
        block_id = value
    else:
        text = str(value).strip()
        if text.isdigit():
            block_id = int(text)
        else:
            key = normalize_key(text)
            if key.startswith("tinycraft:"):
                key = key.split(":", 1)[1]
            if key not in DEFAULT_BLOCK_IDS:
                names = ", ".join(sorted(DEFAULT_BLOCK_IDS.keys()))
                raise PackError(f"unknown block id/name {value!r}; known names: {names}")
            block_id = DEFAULT_BLOCK_IDS[key]

    if block_id < 0 or block_id > 15:
        raise PackError(f"block id {block_id} is outside the v53 runtime range 0..15")
    return block_id


def resolve_entity_id(value):
    if isinstance(value, int):
        entity_id = value
    else:
        text = str(value).strip()
        if text.isdigit():
            entity_id = int(text)
        else:
            key = normalize_key(text)
            if key.startswith("tinycraft:"):
                key = key.split(":", 1)[1]
            if key not in DEFAULT_ENTITY_IDS:
                names = ", ".join(sorted(DEFAULT_ENTITY_IDS.keys()))
                raise PackError(f"unknown entity id/name {value!r}; known names: {names}")
            entity_id = DEFAULT_ENTITY_IDS[key]

    if entity_id < 0 or entity_id > 7:
        raise PackError(f"entity id {entity_id} is outside the v53 runtime range 0..7")
    return entity_id


def resolve_entity_model_id(value):
    if isinstance(value, int):
        model_id = value
    else:
        text = str(value).strip()
        if text.isdigit():
            model_id = int(text)
        else:
            key = normalize_key(text)
            if key.startswith("tinycraft:"):
                key = key.split(":", 1)[1]
            if key not in ENTITY_MODEL_KINDS:
                names = ", ".join(sorted(ENTITY_MODEL_KINDS.keys()))
                raise PackError(f"unknown entity model id/name {value!r}; known names: {names}")
            model_id = ENTITY_MODEL_KINDS[key]

    if model_id < 0 or model_id > 7:
        raise PackError(f"entity model id {model_id} is outside the v53 runtime range 0..7")
    return model_id


def resolve_named_kind(value, mapping, field):
    if value is None:
        value = "none"
    if isinstance(value, int):
        result = value
    else:
        key = normalize_key(value)
        if key not in mapping:
            names = ", ".join(sorted(mapping.keys()))
            raise PackError(f"unknown {field} {value!r}; known names: {names}")
        result = mapping[key]
    if result < 0 or result > 255:
        raise PackError(f"{field} {result} is outside byte range 0..255")
    return result


def resolve_name_label(value, default_name="none"):
    if value is None:
        value = default_name
    if isinstance(value, int):
        label = value
    else:
        key = normalize_key(value)
        if key.startswith("tc_name_"):
            key = key[8:]
        if key not in DEFAULT_NAME_LABELS:
            names = ", ".join(sorted(DEFAULT_NAME_LABELS.keys()))
            raise PackError(f"unknown name label {value!r}; known names: {names}")
        label = DEFAULT_NAME_LABELS[key]
    if label < 0 or label > 255:
        raise PackError(f"name label {label} is outside byte range 0..255")
    return label


def resolve_tool_kind(value):
    if value is None:
        return TOOL_KINDS["none"]
    if isinstance(value, int):
        tool = value
    else:
        key = normalize_key(value)
        if key.startswith("tc_tool_"):
            key = key[8:]
        if key not in TOOL_KINDS:
            names = ", ".join(sorted(TOOL_KINDS.keys()))
            raise PackError(f"unknown tool kind {value!r}; known names: {names}")
        tool = TOOL_KINDS[key]
    if tool < 0 or tool > 127:
        raise PackError(f"tool kind {tool} is outside signed-byte range")
    return tool


def resolve_block_render_class(value):
    if value is None:
        return BLOCK_RENDER_CLASSES["fast_cube"]
    if isinstance(value, int):
        render_class = value
    else:
        key = normalize_key(value)
        if key.startswith("tc_block_render_"):
            key = key[16:]
        if key not in BLOCK_RENDER_CLASSES:
            names = ", ".join(sorted(BLOCK_RENDER_CLASSES.keys()))
            raise PackError(f"unknown block render class {value!r}; known names: {names}")
        render_class = BLOCK_RENDER_CLASSES[key]
    if render_class < 0 or render_class > 255:
        raise PackError(f"block render class {render_class} is outside byte range 0..255")
    return render_class


def resolve_behavior_flags(value, solid, placeable, stops_sky, opens_crafting):
    if value is None:
        flags = 0
        if solid:
            flags |= BLOCK_FLAGS["solid"]
        if placeable:
            flags |= BLOCK_FLAGS["placeable"]
        if stops_sky:
            flags |= BLOCK_FLAGS["stops_sky"]
        if opens_crafting:
            flags |= BLOCK_FLAGS["opens_crafting"]
        return flags

    if isinstance(value, int):
        flags = value
    elif isinstance(value, list):
        flags = 0
        for item in value:
            key = normalize_key(item)
            if key not in BLOCK_FLAGS:
                names = ", ".join(sorted(BLOCK_FLAGS.keys()))
                raise PackError(f"unknown block behavior flag {item!r}; known names: {names}")
            flags |= BLOCK_FLAGS[key]
    else:
        key = normalize_key(value)
        if key not in BLOCK_FLAGS:
            names = ", ".join(sorted(BLOCK_FLAGS.keys()))
            raise PackError(f"unknown block behavior flag {value!r}; known names: {names}")
        flags = BLOCK_FLAGS[key]

    if flags < 0 or flags > 65535:
        raise PackError(f"block behavior flags {flags} outside range 0..65535")
    return flags


def parse_hex_rgb(text):
    value = str(text).strip()
    if value.startswith("#"):
        value = value[1:]
    if len(value) != 6:
        raise PackError("--transparent-fill must be RRGGBB, for example 000000")
    try:
        return int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16)
    except ValueError as exc:
        raise PackError("--transparent-fill must contain hexadecimal digits") from exc


def rgb_to_rgb332(r, g, b):
    r3 = (int(r) * 7 + 127) // 255
    g3 = (int(g) * 7 + 127) // 255
    b2 = (int(b) * 3 + 127) // 255
    return (r3 << 5) | (g3 << 2) | b2


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def png_channels(color_type):
    if color_type == 0:
        return 1
    if color_type == 2:
        return 3
    if color_type == 3:
        return 1
    if color_type == 4:
        return 2
    if color_type == 6:
        return 4
    raise PackError(f"unsupported PNG color type {color_type}")


def png_scanline_stride(width, bit_depth, color_type):
    return (width * bit_depth * png_channels(color_type) + 7) // 8


def png_filter_bpp(bit_depth, color_type):
    bits = bit_depth * png_channels(color_type)
    bpp = (bits + 7) // 8
    return bpp if bpp > 0 else 1


def unpack_packed_sample(row, x, bit_depth):
    if bit_depth == 8:
        return row[x]
    bit_pos = x * bit_depth
    byte_value = row[bit_pos // 8]
    shift = 8 - bit_depth - (bit_pos % 8)
    return (byte_value >> shift) & ((1 << bit_depth) - 1)


def read_png_rgba(path):
    data = Path(path).read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise PackError(f"{path} is not a PNG file")

    pos = 8
    width = None
    height = None
    bit_depth = None
    color_type = None
    interlace = None
    palette = None
    palette_alpha = None
    transparent_rgb = None
    idat = bytearray()

    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        chunk_type = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos = pos + 12 + length

        if chunk_type == b"IHDR":
            if length != 13:
                raise PackError("invalid PNG IHDR length")
            width, height, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(">IIBBBBB", chunk)
            if compression != 0 or filter_method != 0:
                raise PackError("unsupported PNG compression/filter method")
        elif chunk_type == b"PLTE":
            palette = [tuple(chunk[i:i + 3]) for i in range(0, len(chunk), 3)]
        elif chunk_type == b"tRNS":
            if color_type == 3:
                palette_alpha = bytes(chunk)
            elif color_type == 2 and len(chunk) >= 6:
                transparent_rgb = struct.unpack(">HHH", chunk[:6])
            elif color_type == 0 and len(chunk) >= 2:
                gray = struct.unpack(">H", chunk[:2])[0]
                transparent_rgb = (gray, gray, gray)
        elif chunk_type == b"IDAT":
            idat.extend(chunk)
        elif chunk_type == b"IEND":
            break

    if width is None or height is None:
        raise PackError("PNG is missing IHDR")
    if color_type in (2, 4, 6) and bit_depth != 8:
        raise PackError(f"unsupported PNG bit depth {bit_depth} for color type {color_type}; use 8-bit PNGs")
    if color_type in (0, 3) and bit_depth not in (1, 2, 4, 8):
        raise PackError(f"unsupported PNG bit depth {bit_depth}; use 1, 2, 4, or 8-bit indexed/grayscale PNGs")
    if interlace != 0:
        raise PackError("interlaced PNGs are not supported; export as non-interlaced")
    if color_type == 3 and palette is None:
        raise PackError("indexed PNG is missing PLTE")

    channels = png_channels(color_type)
    stride = png_scanline_stride(width, bit_depth, color_type)
    bpp = png_filter_bpp(bit_depth, color_type)
    raw = zlib.decompress(bytes(idat))
    expected_min = height * (stride + 1)
    if len(raw) < expected_min:
        raise PackError("PNG image data is truncated")

    rows = []
    offset = 0
    prev = bytearray(stride)
    for _y in range(height):
        filter_type = raw[offset]
        offset += 1
        scan = bytearray(raw[offset:offset + stride])
        offset += stride
        recon = bytearray(stride)

        for i, value in enumerate(scan):
            left = recon[i - bpp] if i >= bpp else 0
            up = prev[i]
            upper_left = prev[i - bpp] if i >= bpp else 0
            if filter_type == 0:
                recon[i] = value
            elif filter_type == 1:
                recon[i] = (value + left) & 255
            elif filter_type == 2:
                recon[i] = (value + up) & 255
            elif filter_type == 3:
                recon[i] = (value + ((left + up) // 2)) & 255
            elif filter_type == 4:
                recon[i] = (value + paeth(left, up, upper_left)) & 255
            else:
                raise PackError(f"unsupported PNG filter type {filter_type}")

        rows.append(recon)
        prev = recon

    rgba = []
    for row in rows:
        for x in range(width):
            i = x * channels
            if color_type == 0:
                if bit_depth == 8:
                    g = row[i]
                    transparent_sample = transparent_rgb[0] if transparent_rgb is not None else None
                else:
                    sample = unpack_packed_sample(row, x, bit_depth)
                    g = (sample * 255) // ((1 << bit_depth) - 1)
                    transparent_sample = transparent_rgb[0] if transparent_rgb is not None else None
                    if transparent_sample is not None:
                        transparent_sample = (transparent_sample * 255) // ((1 << bit_depth) - 1)
                a = 0 if transparent_rgb == (g, g, g) else 255
                if transparent_sample is not None and g == transparent_sample:
                    a = 0
                rgba.append((g, g, g, a))
            elif color_type == 2:
                r, g, b = row[i], row[i + 1], row[i + 2]
                tr = transparent_rgb
                a = 0 if tr is not None and (r, g, b) == tr else 255
                rgba.append((r, g, b, a))
            elif color_type == 3:
                index = unpack_packed_sample(row, x, bit_depth)
                if index >= len(palette):
                    raise PackError(f"palette index {index} outside PLTE")
                r, g, b = palette[index]
                a = palette_alpha[index] if palette_alpha is not None and index < len(palette_alpha) else 255
                rgba.append((r, g, b, a))
            elif color_type == 4:
                g, a = row[i], row[i + 1]
                rgba.append((g, g, g, a))
            elif color_type == 6:
                rgba.append((row[i], row[i + 1], row[i + 2], row[i + 3]))

    return width, height, rgba


def flatten_rgba_to_rgb332(rgba, fill_rgb):
    out = bytearray()
    fr, fg, fb = fill_rgb
    for r, g, b, a in rgba:
        if a <= 0:
            r, g, b = fr, fg, fb
        elif a < 255:
            r = (r * a + fr * (255 - a) + 127) // 255
            g = (g * a + fg * (255 - a) + 127) // 255
            b = (b * a + fb * (255 - a) + 127) // 255
        out.append(rgb_to_rgb332(r, g, b))
    return bytes(out)


def dilate_transparent_rgb(rgba, width, height, passes=4):
    """Fill transparent RGB from nearby opaque pixels without changing alpha."""
    out = list(rgba)
    for _ in range(passes):
        changed = False
        next_rgba = list(out)
        for y in range(height):
            for x in range(width):
                idx = y * width + x
                r, g, b, a = out[idx]
                if a != 0:
                    continue
                found = None
                for nx, ny in (
                    (x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1),
                    (x - 1, y - 1), (x + 1, y - 1), (x - 1, y + 1), (x + 1, y + 1),
                ):
                    if nx < 0 or ny < 0 or nx >= width or ny >= height:
                        continue
                    nr, ng, nb, na = out[ny * width + nx]
                    if na != 0:
                        found = (nr, ng, nb, a)
                        break
                if found is not None:
                    next_rgba[idx] = found
                    changed = True
        out = next_rgba
        if not changed:
            break
    return out


def alpha_mask_from_rgba(rgba, threshold=0):
    return [a > threshold for _r, _g, _b, a in rgba]


def pack_alpha_mask(mask):
    if len(mask) != 256:
        raise PackError("alpha mask records are currently limited to 16x16 textures")
    words = [0] * 8
    for i, opaque in enumerate(mask):
        if opaque:
            # VM texture alpha masks are read MSB-first by fillRect2DTexMasking.
            words[i // 32] |= 1 << (31 - (i % 32))
    out = bytearray()
    for word in words:
        out.extend(struct.pack("<I", word))
    return bytes(out)


def build_destroy_stages_record(stage_sources, fill_rgb):
    if not isinstance(stage_sources, list) or len(stage_sources) != DESTROY_STAGE_COUNT:
        raise PackError(f"destroy stages require exactly {DESTROY_STAGE_COUNT} PNG files")

    payload = bytearray()
    sources = []
    opaque_counts = []
    for stage, source_value in enumerate(stage_sources):
        source = Path(source_value)
        if not source.exists():
            raise PackError(f"missing destroy-stage texture {source}")
        if source.suffix.lower() != ".png":
            raise PackError(f"destroy stage {stage} must be a PNG")

        width, height, rgba = read_png_rgba(source)
        if width != DESTROY_STAGE_WIDTH or height != DESTROY_STAGE_HEIGHT:
            raise PackError(
                f"destroy stage {stage} is {width}x{height}; expected "
                f"{DESTROY_STAGE_WIDTH}x{DESTROY_STAGE_HEIGHT}"
            )

        binary_rgba = [
            (r, g, b, 255) if a >= 128 else (r, g, b, 0)
            for r, g, b, a in rgba
        ]
        mask = alpha_mask_from_rgba(binary_rgba, threshold=0)
        opaque_counts.append(sum(1 for opaque in mask if opaque))
        payload.extend(flatten_rgba_to_rgb332(binary_rgba, fill_rgb))
        payload.extend(pack_alpha_mask(mask))
        sources.append(source)

    expected = DESTROY_STAGE_COUNT * DESTROY_STAGE_BYTES
    if len(payload) != expected:
        raise PackError(f"destroy-stage payload size {len(payload)} does not match {expected}")
    return DestroyStagesRecord(
        name="environment/destroy_stages",
        sources=sources,
        payload=bytes(payload),
        opaque_counts=opaque_counts,
    )


def texture_alpha_kind(entry):
    value = entry.get("alpha", entry.get("alpha_kind", ALPHA_NONE))
    if isinstance(value, int):
        if value == ALPHA_NONE or value == ALPHA_MASK:
            return value
        raise PackError(f"unsupported alpha kind {value}")
    text = normalize_key(value)
    if text in ("none", "opaque", "0", "false"):
        return ALPHA_NONE
    if text in ("mask", "alpha_mask", "1", "true"):
        return ALPHA_MASK
    raise PackError(f"unsupported alpha kind {value!r}; use 'none' or 'mask'")


def parse_crop(value):
    if value is None:
        return None
    if isinstance(value, list) and len(value) == 4:
        x, y, width, height = value
    elif isinstance(value, dict):
        x = value.get("x", 0)
        y = value.get("y", 0)
        width = value.get("width")
        height = value.get("height")
    else:
        raise PackError("crop must be [x, y, width, height] or an object")

    if width is None or height is None:
        raise PackError("crop needs width and height")
    x, y, width, height = int(x), int(y), int(width), int(height)
    if x < 0 or y < 0 or width <= 0 or height <= 0:
        raise PackError("crop coordinates must be non-negative and size must be positive")
    return x, y, width, height


def crop_rgba(width, height, rgba, crop):
    if crop is None:
        return width, height, rgba

    x, y, crop_w, crop_h = crop
    if x + crop_w > width or y + crop_h > height:
        raise PackError(f"crop {crop_w}x{crop_h} at {x},{y} exceeds source image {width}x{height}")

    out = []
    for row in range(y, y + crop_h):
        start = row * width + x
        out.extend(rgba[start:start + crop_w])
    return crop_w, crop_h, out


def default_size_for(name, texture_id):
    key = normalize_key(name)
    if key in ENTITY_TEXTURE_SIZES:
        return ENTITY_TEXTURE_SIZES[key]
    if texture_id in ENTITY_TEXTURE_ID_VALUES:
        return (64, 32)
    return (16, 16)


def load_texture_payload(source, width, height, fill_rgb, crop=None, alpha_kind=ALPHA_NONE):
    suffix = source.suffix.lower()
    if suffix == ".png":
        png_w, png_h, rgba = read_png_rgba(source)
        png_w, png_h, rgba = crop_rgba(png_w, png_h, rgba, parse_crop(crop))
        if width is not None and width != png_w:
            raise PackError(f"{source} resolves to {png_w} wide, manifest says {width}")
        if height is not None and height != png_h:
            raise PackError(f"{source} resolves to {png_h} high, manifest says {height}")
        payload = flatten_rgba_to_rgb332(rgba, fill_rgb)
        if alpha_kind == ALPHA_MASK:
            if png_w != 16 or png_h != 16:
                raise PackError(f"{source}: alpha-mask texture records are currently limited to 16x16")
            payload += pack_alpha_mask(alpha_mask_from_rgba(rgba))
        return png_w, png_h, payload

    if suffix in (".rgb332", ".rg332"):
        if alpha_kind != ALPHA_NONE:
            raise PackError(f"{source}: alpha-mask texture records require PNG source alpha")
        if crop is not None:
            raise PackError("crop is only supported for PNG sources")
        if width is None or height is None:
            raise PackError(f"{source} needs width and height in the manifest")
        payload = source.read_bytes()
        expected = width * height
        if len(payload) != expected:
            raise PackError(f"{source} is {len(payload)} bytes, expected {expected} for {width}x{height}")
        return width, height, payload

    raise PackError(f"unsupported texture file type {source.suffix!r}; use .png, .rgb332, or .rg332")


def manifest_entries(manifest):
    textures = manifest.get("textures")
    if textures is None:
        raise PackError("manifest must contain a 'textures' list or object")

    entries = []
    if isinstance(textures, list):
        for item in textures:
            if isinstance(item, str):
                entries.append({"name": Path(item).stem, "id": Path(item).stem, "source": item})
            elif isinstance(item, dict):
                entry = dict(item)
                if "source" not in entry:
                    raise PackError("texture list entries need a 'source'")
                entry.setdefault("name", entry.get("id", Path(entry["source"]).stem))
                entry.setdefault("id", entry["name"])
                entries.append(entry)
            else:
                raise PackError("texture list entries must be strings or objects")
        return entries

    if isinstance(textures, dict):
        for key, value in textures.items():
            if isinstance(value, str):
                entries.append({"name": key, "id": key, "source": value})
            elif isinstance(value, dict):
                entry = dict(value)
                if "source" not in entry:
                    raise PackError(f"texture {key!r} needs a 'source'")
                entry.setdefault("name", key)
                entry.setdefault("id", key)
                entries.append(entry)
            else:
                raise PackError("texture object values must be strings or objects")
        return entries

    raise PackError("'textures' must be a list or object")


def item_mesh_entries(manifest):
    meshes = manifest.get("item_meshes", manifest.get("custom_mesh_items", manifest.get("meshes")))
    if meshes is None:
        return []

    entries = []
    if isinstance(meshes, list):
        for item in meshes:
            if not isinstance(item, dict):
                raise PackError("item mesh list entries must be objects")
            entry = dict(item)
            entry.setdefault("name", entry.get("id", entry.get("item", "item_mesh")))
            entries.append(entry)
        return entries

    if isinstance(meshes, dict):
        for key, value in meshes.items():
            if not isinstance(value, dict):
                raise PackError("item mesh object values must be objects")
            entry = dict(value)
            entry.setdefault("name", key)
            entry.setdefault("item", key)
            entries.append(entry)
        return entries

    raise PackError("'item_meshes' must be a list or object")


def flat_item_entries(manifest):
    items = manifest.get("flat_items", manifest.get("generated_flat_items", manifest.get("texture_items")))
    if items is None:
        return []

    entries = []
    if isinstance(items, list):
        for item in items:
            if isinstance(item, str):
                key = normalize_key(item)
                entries.append({"name": key, "item": key})
            elif isinstance(item, dict):
                entry = dict(item)
                entry.setdefault("name", entry.get("id", entry.get("item", "flat_item")))
                entries.append(entry)
            else:
                raise PackError("flat item list entries must be strings or objects")
        return entries

    if isinstance(items, dict):
        for key, value in items.items():
            if isinstance(value, str):
                entries.append({"name": key, "item": key, "source": value})
            elif isinstance(value, dict):
                entry = dict(value)
                entry.setdefault("name", key)
                entry.setdefault("item", key)
                entries.append(entry)
            else:
                raise PackError("flat item object values must be strings or objects")
        return entries

    raise PackError("'flat_items' must be a list or object")


def block_entries(manifest):
    blocks = manifest.get("blocks", manifest.get("block_defs", manifest.get("block_definitions")))
    if blocks is None:
        return []

    entries = []
    if isinstance(blocks, list):
        for item in blocks:
            if isinstance(item, str):
                key = normalize_key(item)
                entries.append({"name": key, "id": key})
            elif isinstance(item, dict):
                entry = dict(item)
                entry.setdefault("name", entry.get("id", entry.get("block", "block")))
                entry.setdefault("id", entry.get("block", entry["name"]))
                entries.append(entry)
            else:
                raise PackError("block list entries must be strings or objects")
        return entries

    if isinstance(blocks, dict):
        for key, value in blocks.items():
            if not isinstance(value, dict):
                raise PackError("block object values must be objects")
            entry = dict(value)
            entry.setdefault("name", key)
            entry.setdefault("id", entry.get("block", key))
            entries.append(entry)
        return entries

    raise PackError("'blocks' must be a list or object")


def block_model_entries(manifest):
    models = manifest.get("block_models", manifest.get("custom_block_models", manifest.get("block_meshes")))
    if models is None:
        return []

    entries = []
    if isinstance(models, list):
        for item in models:
            if not isinstance(item, dict):
                raise PackError("block model list entries must be objects")
            entry = dict(item)
            entry.setdefault("name", entry.get("id", entry.get("block", "block_model")))
            entry.setdefault("block", entry.get("block_id", entry["name"]))
            entries.append(entry)
        return entries

    if isinstance(models, dict):
        for key, value in models.items():
            if not isinstance(value, dict):
                raise PackError("block model object values must be objects")
            entry = dict(value)
            entry.setdefault("name", key)
            entry.setdefault("block", entry.get("block_id", key))
            entries.append(entry)
        return entries

    raise PackError("'block_models' must be a list or object")


def entity_entries(manifest):
    entities = manifest.get("entities", manifest.get("entity_defs", manifest.get("entity_definitions")))
    if entities is None:
        return []

    entries = []
    if isinstance(entities, list):
        for item in entities:
            if isinstance(item, str):
                key = normalize_key(item)
                entries.append({"name": key, "id": key})
            elif isinstance(item, dict):
                entry = dict(item)
                entry.setdefault("name", entry.get("id", entry.get("entity", "entity")))
                entry.setdefault("id", entry.get("entity", entry["name"]))
                entries.append(entry)
            else:
                raise PackError("entity list entries must be strings or objects")
        return entries

    if isinstance(entities, dict):
        for key, value in entities.items():
            if not isinstance(value, dict):
                raise PackError("entity object values must be objects")
            entry = dict(value)
            entry.setdefault("name", key)
            entry.setdefault("id", entry.get("entity", key))
            entries.append(entry)
        return entries

    raise PackError("'entities' must be a list or object")


def entity_model_entries(manifest):
    models = manifest.get("entity_models", manifest.get("entity_model_defs", manifest.get("entity_geometry")))
    if models is None:
        return []

    out = []
    if isinstance(models, list):
        for item in models:
            if not isinstance(item, dict):
                raise PackError("entity model list entries must be objects")
            entry = dict(item)
            entry.setdefault("name", entry.get("id", entry.get("model", "entity_model")))
            entry.setdefault("id", entry.get("model", entry["name"]))
            out.append(entry)
        return out

    if isinstance(models, dict):
        for key, value in models.items():
            if not isinstance(value, dict):
                raise PackError("entity model object values must be objects")
            entry = dict(value)
            entry.setdefault("name", key)
            entry.setdefault("id", entry.get("model", key))
            out.append(entry)
        return out

    raise PackError("'entity_models' must be a list or object")


def mesh_entry_with_source(entry, base):
    if "source" not in entry:
        return entry

    source = Path(entry["source"])
    if not source.is_absolute():
        source = base / source
    if not source.exists():
        raise PackError(f"missing mesh source file {source}")
    data = json.loads(source.read_text(encoding="utf-8-sig"))
    if not isinstance(data, dict):
        raise PackError(f"{source} must contain a JSON object")

    merged = dict(data)
    merged.update({k: v for k, v in entry.items() if k != "source"})
    merged.setdefault("name", entry.get("name", source.stem))
    return merged


def parse_vec3(value, field):
    if not isinstance(value, list) or len(value) != 3:
        raise PackError(f"{field} must be [x, y, z]")
    return [int(value[0]), int(value[1]), int(value[2])]


def parse_uv_rect(value):
    if value is None:
        return [0, 0, 15, 15]
    if isinstance(value, list) and len(value) == 4:
        return [int(value[0]), int(value[1]), int(value[2]), int(value[3])]
    if isinstance(value, dict):
        return [
            int(value.get("u0", value.get("x", 0))),
            int(value.get("v0", value.get("y", 0))),
            int(value.get("u1", value.get("x1", 15))),
            int(value.get("v1", value.get("y1", 15))),
        ]
    raise PackError("mesh uv must be [u0, v0, u1, v1] or an object")


def clamp_coord(value, field):
    value = int(value)
    if value < -128 or value > 127:
        raise PackError(f"{field} coordinate {value} is outside signed byte range -128..127")
    return value & 0xFF


def clamp_u8(value, field):
    value = int(value)
    if value < 0 or value > 255:
        raise PackError(f"{field} value {value} is outside byte range 0..255")
    return value


def clamp_u16(value, field):
    value = int(value)
    if value < 0 or value > 65535:
        raise PackError(f"{field} value {value} is outside range 0..65535")
    return value


def clamp_i16(value, field):
    value = int(value)
    if value < -32768 or value > 32767:
        raise PackError(f"{field} value {value} is outside signed 16-bit range")
    return value


def bool_int(value, default=False):
    if value is None:
        return 1 if default else 0
    if isinstance(value, bool):
        return 1 if value else 0
    if isinstance(value, int):
        return 1 if value != 0 else 0
    key = normalize_key(value)
    if key in ("1", "true", "yes", "on", "enabled"):
        return 1
    if key in ("0", "false", "no", "off", "disabled"):
        return 0
    raise PackError(f"expected boolean-like value, got {value!r}")


def pack_u16(payload, value, field):
    value = clamp_u16(value, field)
    payload.append((value >> 8) & 0xFF)
    payload.append(value & 0xFF)


def pack_i16(payload, value, field):
    value = clamp_i16(value, field)
    if value < 0:
        value += 65536
    payload.append((value >> 8) & 0xFF)
    payload.append(value & 0xFF)


def fixed_model_coord(value, field):
    return clamp_i16(round(float(value) * 10000.0), field)


def parse_model_vec3(value, field, default=None):
    if value is None:
        if default is None:
            raise PackError(f"{field} must be [x, y, z]")
        value = default
    if not isinstance(value, list) or len(value) != 3:
        raise PackError(f"{field} must be [x, y, z]")
    return [float(value[0]), float(value[1]), float(value[2])]


def pack_mesh_face(vertices, uv, texture_id, flags=0):
    if not isinstance(vertices, list) or len(vertices) != 4:
        raise PackError("mesh face vertices must contain four [x, y, z] vertices")
    payload = bytearray()
    for vi, vertex in enumerate(vertices):
        x, y, z = parse_vec3(vertex, f"face vertex {vi}")
        payload.append(clamp_coord(x, "x"))
        payload.append(clamp_coord(y, "y"))
        payload.append(clamp_coord(z, "z"))
    u0, v0, u1, v1 = parse_uv_rect(uv)
    payload.append(clamp_u8(u0, "u0"))
    payload.append(clamp_u8(v0, "v0"))
    payload.append(clamp_u8(u1, "u1"))
    payload.append(clamp_u8(v1, "v1"))
    payload.append(clamp_u8(texture_id, "texture_id"))
    payload.append(clamp_u8(flags, "face flags"))
    payload.append(0)
    payload.append(0)
    return bytes(payload)


def fixed_milli(value, field):
    return clamp_u16(round(float(value) * 1000.0), field)


def fixed_micro(value, field):
    return clamp_u16(round(float(value) * 1000000.0), field)


def pack_entity_def(entry, texture_ids):
    name = str(entry.get("name", entry.get("id", "entity"))).strip()
    entity_id = resolve_entity_id(entry.get("id", entry.get("entity", name)))
    texture_id = resolve_texture_id(entry.get("texture", entry.get("texture_id", f"entity/{name}")), texture_ids)
    render_kind = resolve_named_kind(entry.get("render_kind", entry.get("render", "none")), ENTITY_RENDER_KINDS, "entity render kind")
    behavior_kind = resolve_named_kind(entry.get("behavior", entry.get("behavior_kind", "none")), ENTITY_BEHAVIOR_KINDS, "entity behavior kind")
    model_kind = resolve_named_kind(entry.get("model", entry.get("model_kind", "none")), ENTITY_MODEL_KINDS, "entity model kind")

    hitbox = entry.get("hitbox", {})
    if isinstance(hitbox, list):
        hitbox_x = float(hitbox[0])
        hitbox_z = float(hitbox[1] if len(hitbox) > 1 else hitbox[0])
    elif isinstance(hitbox, dict):
        hitbox_x = float(hitbox.get("x", hitbox.get("width", 0.45)))
        hitbox_z = float(hitbox.get("z", hitbox.get("depth", hitbox_x)))
    else:
        hitbox_x = float(entry.get("hitbox_x", 0.45))
        hitbox_z = float(entry.get("hitbox_z", 0.70))

    max_active = clamp_u8(entry.get("max_active", entry.get("max_loaded", 6)), "max_active")
    spawn_group = clamp_u8(entry.get("spawn_group", entry.get("initial_spawn_count", min(max_active, 4))), "spawn_group")
    spawn_floor = resolve_block_id(entry.get("spawn_floor", entry.get("spawn_floor_block", "grass")))
    spawn_light = clamp_u8(entry.get("spawn_light_min", entry.get("min_spawn_light", 7)), "spawn_light_min")

    payload = bytearray()
    payload.append(clamp_u8(render_kind, "entity render_kind"))
    payload.append(clamp_u8(behavior_kind, "entity behavior_kind"))
    payload.append(clamp_u8(texture_id, "entity texture_id"))
    payload.append(clamp_u8(model_kind, "entity model_kind"))
    payload.append(max_active)
    payload.append(spawn_group)
    payload.append(clamp_u8(spawn_floor, "entity spawn_floor"))
    payload.append(spawn_light)
    pack_u16(payload, fixed_milli(hitbox_x, "hitbox_x"), "hitbox_x")
    pack_u16(payload, fixed_milli(hitbox_z, "hitbox_z"), "hitbox_z")
    pack_u16(payload, fixed_milli(entry.get("height", 0.95), "height"), "height")
    pack_u16(payload, fixed_milli(entry.get("eye_height", 0.72), "eye_height"), "eye_height")
    pack_u16(payload, fixed_milli(entry.get("render_radius", 1.45), "render_radius"), "render_radius")
    pack_u16(payload, clamp_u16(entry.get("lod_distance_sq", entry.get("lod_dist_sq", 100)), "lod_distance_sq"), "lod_distance_sq")
    pack_u16(payload, clamp_u16(entry.get("ai_tick_ms", 96), "ai_tick_ms"), "ai_tick_ms")
    pack_u16(payload, fixed_micro(entry.get("move_speed", 0.00082), "move_speed"), "move_speed")
    pack_u16(payload, fixed_micro(entry.get("turn_speed", 0.00235), "turn_speed"), "turn_speed")
    pack_u16(payload, fixed_micro(entry.get("head_turn_speed", 0.0026), "head_turn_speed"), "head_turn_speed")
    pack_u16(payload, fixed_milli(entry.get("head_yaw_limit", 1.309), "head_yaw_limit"), "head_yaw_limit")
    pack_u16(payload, fixed_milli(entry.get("head_pitch_limit", 0.70), "head_pitch_limit"), "head_pitch_limit")

    if len(payload) != ENTITY_DEF_BYTES:
        raise PackError(f"{name}: entity payload size {len(payload)} must be {ENTITY_DEF_BYTES}")

    return EntityRecord(
        name=name,
        entity_id=entity_id,
        render_kind=render_kind,
        texture_id=texture_id,
        payload=bytes(payload),
        flags=int(entry.get("flags", 0)),
    )


def parse_entity_uv_origin(value):
    if value is None:
        return 0, 0
    if isinstance(value, list):
        if len(value) < 2:
            raise PackError("entity model part uv must contain at least [u, v]")
        return int(value[0]), int(value[1])
    if isinstance(value, dict):
        return int(value.get("u", value.get("x", 0))), int(value.get("v", value.get("y", 0)))
    raise PackError("entity model part uv must be [u, v] or an object")


def parse_entity_part_size(part, from_vec, to_vec):
    value = part.get("size", part.get("cube_size"))
    if value is None:
        return [
            round(abs(to_vec[0] - from_vec[0]) * 16.0),
            round(abs(to_vec[1] - from_vec[1]) * 16.0),
            round(abs(to_vec[2] - from_vec[2]) * 16.0),
        ]
    if not isinstance(value, list) or len(value) != 3:
        raise PackError("entity model part size must be [sx, sy, sz]")
    return [int(value[0]), int(value[1]), int(value[2])]


def pack_entity_model_part(part, texture_ids, default_texture):
    if not isinstance(part, dict):
        raise PackError("entity model parts must be objects")

    part_kind = resolve_named_kind(part.get("kind", part.get("part_kind", "body")), ENTITY_PART_KINDS, "entity part kind")
    anim_kind = resolve_named_kind(part.get("anim", part.get("animation", "none")), ENTITY_PART_ANIMS, "entity part animation")
    lod_group = clamp_u8(1 if bool_int(part.get("near_only"), default=False) else part.get("lod_group", part.get("lod", 0)), "entity part lod_group")
    texture_id = texture_for_face(part.get("texture"), texture_ids, default_texture)

    from_vec = parse_model_vec3(part.get("from", part.get("min")), "entity model part from")
    to_vec = parse_model_vec3(part.get("to", part.get("max")), "entity model part to")
    pivot_vec = parse_model_vec3(part.get("pivot"), "entity model part pivot", [0.0, 0.0, 0.0])
    uvx, uvy = parse_entity_uv_origin(part.get("uv"))
    sx, sy, sz = parse_entity_part_size(part, from_vec, to_vec)
    flags = int(part.get("flags", 0))

    payload = bytearray()
    payload.append(clamp_u8(part_kind, "entity part kind"))
    payload.append(clamp_u8(anim_kind, "entity part anim"))
    payload.append(clamp_u8(lod_group, "entity part lod_group"))
    payload.append(clamp_u8(texture_id, "entity part texture_id"))
    payload.append(clamp_u8(uvx, "entity part uvx"))
    payload.append(clamp_u8(uvy, "entity part uvy"))
    payload.append(clamp_u8(sx, "entity part sx"))
    payload.append(clamp_u8(sy, "entity part sy"))
    payload.append(clamp_u8(sz, "entity part sz"))
    payload.append(clamp_u8(flags, "entity part flags"))
    payload.append(0)
    payload.append(0)
    pack_i16(payload, fixed_model_coord(from_vec[0], "entity part x0"), "entity part x0")
    pack_i16(payload, fixed_model_coord(from_vec[1], "entity part y0"), "entity part y0")
    pack_i16(payload, fixed_model_coord(from_vec[2], "entity part z0"), "entity part z0")
    pack_i16(payload, fixed_model_coord(to_vec[0], "entity part x1"), "entity part x1")
    pack_i16(payload, fixed_model_coord(to_vec[1], "entity part y1"), "entity part y1")
    pack_i16(payload, fixed_model_coord(to_vec[2], "entity part z1"), "entity part z1")
    pack_i16(payload, fixed_model_coord(pivot_vec[0], "entity part pivot_x"), "entity part pivot_x")
    pack_i16(payload, fixed_model_coord(pivot_vec[1], "entity part pivot_y"), "entity part pivot_y")
    pack_i16(payload, fixed_model_coord(pivot_vec[2], "entity part pivot_z"), "entity part pivot_z")
    payload.append(0)
    payload.append(0)

    if len(payload) != ENTITY_MODEL_PART_BYTES:
        raise PackError(f"entity model part payload size {len(payload)} must be {ENTITY_MODEL_PART_BYTES}")
    return bytes(payload)


def build_entity_model_payload(entry, texture_ids):
    default_texture = resolve_texture_id(entry.get("texture", entry.get("texture_id", "missing")), texture_ids)
    parts = entry.get("parts", entry.get("cuboids", entry.get("boxes", [])))
    if not isinstance(parts, list):
        raise PackError("entity model parts must be a list")

    payload = bytearray()
    part_count = 0
    for part in parts:
        payload.extend(pack_entity_model_part(part, texture_ids, default_texture))
        part_count += 1

    if part_count <= 0:
        raise PackError(f"{entry.get('name', 'entity model')}: entity model needs at least one part")
    if part_count > MAX_ENTITY_MODEL_PARTS_PER_RECORD:
        raise PackError(
            f"{entry.get('name', 'entity model')}: {part_count} parts exceeds v53 per-entity-model limit "
            f"{MAX_ENTITY_MODEL_PARTS_PER_RECORD}"
        )
    return part_count, default_texture, bytes(payload)


def pack_block_def(entry, texture_ids):
    name = str(entry.get("name", entry.get("id", "block"))).strip()
    block_id = resolve_block_id(entry.get("id", entry.get("block", name)))

    render_class = resolve_block_render_class(entry.get("render_class", entry.get("render", "fast_cube")))
    placeable = bool_int(entry.get("placeable"), default=(block_id != 0))
    solid = bool_int(entry.get("solid"), default=bool(placeable))
    stops_sky = bool_int(entry.get("stops_sky", entry.get("stops_sky_light")), default=(block_id != 0))
    light_emission = clamp_u8(entry.get("light", entry.get("light_emission", 0)), "light emission")
    ui_color = clamp_u8(entry.get("ui_color", entry.get("color", 0)), "ui color")
    break_ms = clamp_u16(entry.get("break_ms", entry.get("hardness_ms", 900)), "break_ms")
    preferred_tool = resolve_tool_kind(entry.get("preferred_tool"))
    preferred_ms = clamp_u16(entry.get("preferred_break_ms", entry.get("tool_break_ms", break_ms)), "preferred_break_ms")
    drop_required_tool = resolve_tool_kind(entry.get("drop_required_tool"))
    dropped_item = resolve_item_id(entry.get("drop", entry.get("dropped_item", block_id)))
    dropped_count = clamp_u8(entry.get("drop_count", entry.get("dropped_count", 1 if block_id != 0 else 0)), "drop_count")

    textures = entry.get("textures", {})
    if textures is None:
        textures = {}
    if not isinstance(textures, dict):
        raise PackError(f"block {name}: textures must be an object")

    default_tex = entry.get("texture", entry.get("tex", "missing"))
    top_tex = resolve_texture_id(entry.get("top_texture", textures.get("top", default_tex)), texture_ids)
    bottom_tex = resolve_texture_id(entry.get("bottom_texture", textures.get("bottom", default_tex)), texture_ids)
    side_tex = resolve_texture_id(entry.get("side_texture", textures.get("side", default_tex)), texture_ids)
    icon_tex = resolve_texture_id(entry.get("icon_texture", entry.get("icon", textures.get("icon", side_tex))), texture_ids)
    name_label = resolve_name_label(entry.get("name_label"), default_name=name)
    linked_item = resolve_item_id(entry.get("item", entry.get("linked_item", block_id)))
    opens_crafting = bool_int(entry.get("opens_crafting"), default=False)
    behavior_flags = resolve_behavior_flags(entry.get("flags", entry.get("behavior_flags")), solid, placeable, stops_sky, opens_crafting)

    payload = bytearray()
    payload.append(clamp_u8(render_class, "render_class"))
    payload.append(placeable)
    payload.append(stops_sky)
    payload.append(light_emission)
    payload.append(ui_color)
    pack_u16(payload, break_ms, "break_ms")
    payload.append(clamp_coord(preferred_tool, "preferred_tool"))
    pack_u16(payload, preferred_ms, "preferred_break_ms")
    payload.append(clamp_coord(drop_required_tool, "drop_required_tool"))
    payload.append(clamp_u8(dropped_item, "dropped_item"))
    payload.append(dropped_count)
    payload.append(top_tex)
    payload.append(bottom_tex)
    payload.append(side_tex)
    payload.append(icon_tex)
    payload.append(name_label)
    payload.append(clamp_u8(linked_item, "linked_item"))
    payload.append(opens_crafting)
    pack_u16(payload, behavior_flags, "behavior_flags")
    while len(payload) < BLOCK_DEF_BYTES:
        payload.append(0)

    return BlockRecord(name=name, block_id=block_id, payload=bytes(payload))


def image_opaque(mask, width, height, x, y):
    if x < 0 or y < 0 or x >= width or y >= height:
        return False
    return mask[y * width + x]


def model_x(px, width):
    return int(px) - (width // 2)


def model_y(py, height):
    return (height // 2) - int(py)


def runs_for_row(mask, width, height, y):
    runs = []
    x = 0
    while x < width:
        while x < width and not image_opaque(mask, width, height, x, y):
            x += 1
        if x >= width:
            break
        x0 = x
        while x < width and image_opaque(mask, width, height, x, y):
            x += 1
        runs.append((x0, x - 1))
    return runs


def rects_from_row_runs(mask, width, height):
    row_runs = [runs_for_row(mask, width, height, y) for y in range(height)]
    used = set()
    rects = []
    for y in range(height):
        for run in row_runs[y]:
            key = (y, run[0], run[1])
            if key in used:
                continue
            y1 = y
            while y1 + 1 < height and run in row_runs[y1 + 1] and (y1 + 1, run[0], run[1]) not in used:
                y1 += 1
            for yy in range(y, y1 + 1):
                used.add((yy, run[0], run[1]))
            rects.append((run[0], run[1], y, y1))
    return rects


def append_flat_face(faces, vertices, uv, texture_id):
    faces.append(pack_mesh_face(vertices, uv, texture_id, 0))


def nondegenerate_uv_rect(x0, y0, x1, y1, width, height):
    u0 = max(0, min(width - 1, int(x0)))
    u1 = max(0, min(width - 1, int(x1)))
    v0 = max(0, min(height - 1, int(y0)))
    v1 = max(0, min(height - 1, int(y1)))

    if u0 == u1:
        if u1 < width - 1:
            u1 += 1
        elif u0 > 0:
            u0 -= 1

    if v0 == v1:
        if v1 < height - 1:
            v1 += 1
        elif v0 > 0:
            v0 -= 1

    return [u0, v0, u1, v1]


def build_flat_item_mesh_payload(name, width, height, rgba, texture_id):
    if width != 16 or height != 16:
        raise PackError(f"{name}: flat item meshes are currently limited to 16x16 item textures")

    mask = alpha_mask_from_rgba(rgba)
    if not any(mask):
        raise PackError(f"{name}: item PNG has no visible pixels")

    faces = []
    z_front = -1
    z_back = 1

    for x0, x1, y0, y1 in rects_from_row_runs(mask, width, height):
        mx0 = model_x(x0, width)
        mx1 = model_x(x1 + 1, width)
        my_top = model_y(y0, height)
        my_bottom = model_y(y1 + 1, height)
        uv = nondegenerate_uv_rect(x0, y0, x1, y1, width, height)
        append_flat_face(faces, [[mx0, my_bottom, z_front], [mx1, my_bottom, z_front], [mx1, my_top, z_front], [mx0, my_top, z_front]], uv, texture_id)
        append_flat_face(faces, [[mx1, my_bottom, z_back], [mx0, my_bottom, z_back], [mx0, my_top, z_back], [mx1, my_top, z_back]], uv, texture_id)

    for y in range(height):
        x = 0
        while x < width:
            while x < width and not (image_opaque(mask, width, height, x, y) and not image_opaque(mask, width, height, x, y - 1)):
                x += 1
            if x < width:
                x0 = x
                while x < width and image_opaque(mask, width, height, x, y) and not image_opaque(mask, width, height, x, y - 1):
                    x += 1
                x1 = x - 1
                mx0 = model_x(x0, width)
                mx1 = model_x(x1 + 1, width)
                my_top = model_y(y, height)
                uv = nondegenerate_uv_rect(x0, y, x1, y, width, height)
                append_flat_face(faces, [[mx0, my_top, z_back], [mx1, my_top, z_back], [mx1, my_top, z_front], [mx0, my_top, z_front]], uv, texture_id)

        x = 0
        while x < width:
            while x < width and not (image_opaque(mask, width, height, x, y) and not image_opaque(mask, width, height, x, y + 1)):
                x += 1
            if x < width:
                x0 = x
                while x < width and image_opaque(mask, width, height, x, y) and not image_opaque(mask, width, height, x, y + 1):
                    x += 1
                x1 = x - 1
                mx0 = model_x(x0, width)
                mx1 = model_x(x1 + 1, width)
                my_bottom = model_y(y + 1, height)
                uv = nondegenerate_uv_rect(x0, y, x1, y, width, height)
                append_flat_face(faces, [[mx0, my_bottom, z_front], [mx1, my_bottom, z_front], [mx1, my_bottom, z_back], [mx0, my_bottom, z_back]], uv, texture_id)

    for x in range(width):
        y = 0
        while y < height:
            while y < height and not (image_opaque(mask, width, height, x, y) and not image_opaque(mask, width, height, x - 1, y)):
                y += 1
            if y < height:
                y0 = y
                while y < height and image_opaque(mask, width, height, x, y) and not image_opaque(mask, width, height, x - 1, y):
                    y += 1
                y1 = y - 1
                mx_left = model_x(x, width)
                my_top = model_y(y0, height)
                my_bottom = model_y(y1 + 1, height)
                uv = nondegenerate_uv_rect(x, y0, x, y1, width, height)
                append_flat_face(faces, [[mx_left, my_bottom, z_back], [mx_left, my_bottom, z_front], [mx_left, my_top, z_front], [mx_left, my_top, z_back]], uv, texture_id)

        y = 0
        while y < height:
            while y < height and not (image_opaque(mask, width, height, x, y) and not image_opaque(mask, width, height, x + 1, y)):
                y += 1
            if y < height:
                y0 = y
                while y < height and image_opaque(mask, width, height, x, y) and not image_opaque(mask, width, height, x + 1, y):
                    y += 1
                y1 = y - 1
                mx_right = model_x(x + 1, width)
                my_top = model_y(y0, height)
                my_bottom = model_y(y1 + 1, height)
                uv = nondegenerate_uv_rect(x, y0, x, y1, width, height)
                append_flat_face(faces, [[mx_right, my_bottom, z_front], [mx_right, my_bottom, z_back], [mx_right, my_top, z_back], [mx_right, my_top, z_front]], uv, texture_id)

    if len(faces) > MAX_ITEM_MESH_FACES_PER_RECORD:
        raise PackError(f"{name}: generated flat item mesh has {len(faces)} faces, limit is {MAX_ITEM_MESH_FACES_PER_RECORD}")
    return len(faces), b"".join(faces)


def texture_for_face(value, texture_ids, default_texture):
    if value is None:
        return default_texture
    return resolve_texture_id(value, texture_ids)


def box_to_faces(box, texture_ids, default_texture):
    if "from" in box:
        x0, y0, z0 = parse_vec3(box["from"], "box from")
    else:
        x0, y0, z0 = parse_vec3(box.get("origin", [0, 0, 0]), "box origin")
    if "to" in box:
        x1, y1, z1 = parse_vec3(box["to"], "box to")
    else:
        sx, sy, sz = parse_vec3(box.get("size"), "box size")
        x1, y1, z1 = x0 + sx, y0 + sy, z0 + sz
    if x0 > x1:
        x0, x1 = x1, x0
    if y0 > y1:
        y0, y1 = y1, y0
    if z0 > z1:
        z0, z1 = z1, z0

    tex = box.get("texture", default_texture)
    uv = box.get("uv", [0, 0, 15, 15])
    flags = int(box.get("flags", 0))
    face_textures = tex if isinstance(tex, dict) else {}
    face_uvs = uv if isinstance(uv, dict) and not any(k in uv for k in ("u0", "v0", "x", "y")) else {}
    default_tex_id = texture_for_face(tex if not isinstance(tex, dict) else None, texture_ids, default_texture)

    faces = [
        ("front", [[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0]]),
        ("right", [[x1, y0, z0], [x1, y0, z1], [x1, y1, z1], [x1, y1, z0]]),
        ("top", [[x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1]]),
        ("left", [[x0, y0, z1], [x0, y0, z0], [x0, y1, z0], [x0, y1, z1]]),
        ("bottom", [[x0, y0, z1], [x1, y0, z1], [x1, y0, z0], [x0, y0, z0]]),
        ("back", [[x1, y0, z1], [x0, y0, z1], [x0, y1, z1], [x1, y1, z1]]),
    ]

    out = []
    for face_name, verts in faces:
        tex_id = texture_for_face(face_textures.get(face_name), texture_ids, default_tex_id)
        face_uv = face_uvs.get(face_name, uv)
        out.append(pack_mesh_face(verts, face_uv, tex_id, flags))
    return out


def build_item_mesh_payload(entry, texture_ids):
    default_texture = resolve_texture_id(entry.get("texture", entry.get("icon", "missing")), texture_ids)
    payload = bytearray()
    face_count = 0

    for box in entry.get("boxes", []):
        if not isinstance(box, dict):
            raise PackError("mesh boxes must be objects")
        for face_payload in box_to_faces(box, texture_ids, default_texture):
            payload.extend(face_payload)
            face_count += 1

    for face in entry.get("faces", []):
        if not isinstance(face, dict):
            raise PackError("mesh faces must be objects")
        texture_id = texture_for_face(face.get("texture"), texture_ids, default_texture)
        payload.extend(pack_mesh_face(face.get("vertices"), face.get("uv"), texture_id, int(face.get("flags", 0))))
        face_count += 1

    if face_count <= 0:
        raise PackError(f"{entry.get('name', 'item mesh')}: mesh needs at least one face or box")
    if face_count > MAX_ITEM_MESH_FACES_PER_RECORD:
        raise PackError(
            f"{entry.get('name', 'item mesh')}: {face_count} faces exceeds v53 per-item limit "
            f"{MAX_ITEM_MESH_FACES_PER_RECORD}"
        )
    return face_count, bytes(payload)


def build_block_model_payload(entry, texture_ids):
    face_count, payload = build_item_mesh_payload(entry, texture_ids)
    if face_count > MAX_BLOCK_MODEL_FACES_PER_RECORD:
        raise PackError(
            f"{entry.get('name', 'block model')}: {face_count} faces exceeds v53 per-block-model limit "
            f"{MAX_BLOCK_MODEL_FACES_PER_RECORD}"
        )
    return face_count, payload


def make_records(source_path, args):
    if not source_path.exists():
        raise PackError(f"asset source does not exist: {source_path}")

    if source_path.is_dir():
        try:
            manifest, pack_info = load_pack_pair(source_path)
        except PackLayoutError as exc:
            raise PackError(str(exc)) from exc
        base = source_path
        args.pack_info = pack_info
    else:
        base = source_path.parent
        manifest = json.loads(source_path.read_text(encoding="utf-8-sig"))
        args.pack_info = None

    texture_ids = load_texture_ids(args.texture_ids)
    for key, value in manifest.get("_texture_ids", {}).items():
        texture_ids[normalize_key(key)] = int(value)
    fill_rgb = parse_hex_rgb(args.transparent_fill)

    records = []
    seen_ids = set()
    seen_item_mesh_ids = set()
    seen_block_ids = set()
    seen_block_model_ids = set()
    seen_entity_ids = set()
    seen_entity_model_ids = set()
    for entry in manifest_entries(manifest):
        name = str(entry.get("name", entry.get("id", ""))).strip()
        tex_key = entry.get("texture_id", entry.get("id", name))
        texture_id = resolve_texture_id(tex_key, texture_ids)
        if texture_id in seen_ids:
            raise PackError(f"texture id {texture_id} is defined more than once")
        seen_ids.add(texture_id)

        source = Path(entry["source"])
        if not source.is_absolute():
            source = base / source
        if not source.exists():
            raise PackError(f"missing source file {source}")

        width = entry.get("width")
        height = entry.get("height")
        if width is None or height is None:
            default_w, default_h = default_size_for(name, texture_id)
            width = default_w if width is None else width
            height = default_h if height is None else height
        width = int(width)
        height = int(height)

        alpha_kind = texture_alpha_kind(entry)
        loaded_w, loaded_h, payload = load_texture_payload(source, width, height, fill_rgb, entry.get("crop"), alpha_kind)
        record = TextureRecord(name=name, source=source, texture_id=texture_id,
                               width=loaded_w, height=loaded_h, payload=payload,
                               alpha_kind=alpha_kind,
                               flags=int(entry.get("flags", 0)))
        validate_texture_record_for_v53(record)
        records.append(record)

    destroy_stage_sources = manifest.get("destroy_stages", [])
    if destroy_stage_sources:
        records.append(build_destroy_stages_record(destroy_stage_sources, fill_rgb))

    for entry in block_entries(manifest):
        record = pack_block_def(entry, texture_ids)
        if record.block_id in seen_block_ids:
            raise PackError(f"block id {record.block_id} is defined more than once")
        seen_block_ids.add(record.block_id)
        validate_block_record_for_v53(record)
        records.append(record)

    total_block_model_faces = 0
    for entry in block_model_entries(manifest):
        entry = mesh_entry_with_source(entry, base)
        name = str(entry.get("name", entry.get("block", "block_model"))).strip()
        block_id = resolve_block_id(entry.get("block_id", entry.get("block", name)))
        if block_id in seen_block_model_ids:
            raise PackError(f"block id {block_id} has more than one block model record")
        seen_block_model_ids.add(block_id)
        face_count, payload = build_block_model_payload(entry, texture_ids)
        total_block_model_faces += face_count
        if total_block_model_faces > MAX_BLOCK_MODEL_FACES_TOTAL:
            raise PackError(f"block models have {total_block_model_faces} faces, v53 runtime limit is {MAX_BLOCK_MODEL_FACES_TOTAL}")
        records.append(BlockModelRecord(
            name=name,
            block_id=block_id,
            payload=payload,
            face_count=face_count,
            flags=int(entry.get("flags", 0)),
        ))
        validate_block_model_record_for_v53(records[-1])

    for entry in entity_entries(manifest):
        record = pack_entity_def(entry, texture_ids)
        if record.entity_id in seen_entity_ids:
            raise PackError(f"entity id {record.entity_id} is defined more than once")
        seen_entity_ids.add(record.entity_id)
        validate_entity_record_for_v53(record)
        records.append(record)

    total_entity_model_parts = 0
    for entry in entity_model_entries(manifest):
        name = str(entry.get("name", entry.get("model", "entity_model"))).strip()
        model_id = resolve_entity_model_id(entry.get("model_id", entry.get("id", entry.get("model", name))))
        if model_id in seen_entity_model_ids:
            raise PackError(f"entity model id {model_id} has more than one model record")
        seen_entity_model_ids.add(model_id)
        part_count, texture_id, payload = build_entity_model_payload(entry, texture_ids)
        total_entity_model_parts += part_count
        if total_entity_model_parts > MAX_ENTITY_MODEL_PARTS_TOTAL:
            raise PackError(f"entity models have {total_entity_model_parts} parts, v53 runtime limit is {MAX_ENTITY_MODEL_PARTS_TOTAL}")
        records.append(EntityModelRecord(
            name=name,
            model_id=model_id,
            texture_id=texture_id,
            payload=payload,
            part_count=part_count,
            flags=int(entry.get("flags", 0)),
        ))
        validate_entity_model_record_for_v53(records[-1])

    total_mesh_faces = 0

    for entry in flat_item_entries(manifest):
        name = str(entry.get("name", entry.get("item", "flat_item"))).strip()
        item_key = entry.get("item_id", entry.get("item", name))
        item_id = resolve_item_id(item_key)
        if item_id in seen_item_mesh_ids:
            raise PackError(f"item id {item_id} has more than one mesh record")
        seen_item_mesh_ids.add(item_id)

        item_name = normalize_key(entry.get("item", name))
        texture_key = entry.get("texture_id", entry.get("texture", entry.get("icon_texture", entry.get("icon", DEFAULT_FLAT_ITEM_TEXTURES.get(item_name, item_name)))))
        texture_id = resolve_texture_id(texture_key, texture_ids)
        if texture_id in seen_ids:
            raise PackError(f"texture id {texture_id} is defined more than once")
        seen_ids.add(texture_id)

        source_value = entry.get("source", DEFAULT_FLAT_ITEM_SOURCES.get(item_name))
        if source_value is None:
            raise PackError(f"{name}: flat item entry needs a PNG source")
        source = Path(source_value)
        if not source.is_absolute():
            source = base / source
        if not source.exists():
            raise PackError(f"missing flat item source file {source}")
        if source.suffix.lower() != ".png":
            raise PackError(f"{name}: flat item meshes require a PNG source with alpha")

        png_w, png_h, rgba = read_png_rgba(source)
        png_w, png_h, rgba = crop_rgba(png_w, png_h, rgba, parse_crop(entry.get("crop")))
        if png_w != 16 or png_h != 16:
            raise PackError(f"{name}: flat item source resolves to {png_w}x{png_h}, expected 16x16")

        mesh_rgba = [(r, g, b, 255) for r, g, b, _a in dilate_transparent_rgb(rgba, png_w, png_h)]
        texture_payload = flatten_rgba_to_rgb332(mesh_rgba, fill_rgb) + pack_alpha_mask(alpha_mask_from_rgba(rgba))
        texture_record = TextureRecord(
            name=f"{name}_texture",
            source=source,
            texture_id=texture_id,
            width=16,
            height=16,
            payload=texture_payload,
            alpha_kind=ALPHA_MASK,
            flags=int(entry.get("texture_flags", 0)),
        )
        validate_texture_record_for_v53(texture_record)
        records.append(texture_record)

        face_count, payload = build_flat_item_mesh_payload(name, png_w, png_h, rgba, texture_id)
        total_mesh_faces += face_count
        if total_mesh_faces > MAX_ITEM_MESH_FACES_TOTAL:
            raise PackError(f"item meshes have {total_mesh_faces} faces, v53 runtime limit is {MAX_ITEM_MESH_FACES_TOTAL}")
        records.append(ItemMeshRecord(
            name=name,
            item_id=item_id,
            icon_texture_id=texture_id,
            payload=payload,
            face_count=face_count,
            flags=int(entry.get("flags", 0)),
        ))

    for entry in item_mesh_entries(manifest):
        entry = mesh_entry_with_source(entry, base)
        name = str(entry.get("name", entry.get("item", "item_mesh"))).strip()
        item_id = resolve_item_id(entry.get("item_id", entry.get("item", name)))
        if item_id in seen_item_mesh_ids:
            raise PackError(f"item id {item_id} has more than one mesh record")
        seen_item_mesh_ids.add(item_id)
        icon_tex = resolve_texture_id(entry.get("icon_texture", entry.get("icon", entry.get("texture", "missing"))), texture_ids)
        face_count, payload = build_item_mesh_payload(entry, texture_ids)
        total_mesh_faces += face_count
        if total_mesh_faces > MAX_ITEM_MESH_FACES_TOTAL:
            raise PackError(f"item meshes have {total_mesh_faces} faces, v53 runtime limit is {MAX_ITEM_MESH_FACES_TOTAL}")
        records.append(ItemMeshRecord(
            name=name,
            item_id=item_id,
            icon_texture_id=icon_tex,
            payload=payload,
            face_count=face_count,
            flags=int(entry.get("flags", 0)),
        ))

    entity_texture_count = 0
    for record in records:
        if isinstance(record, TextureRecord) and record.width == 64 and record.height == 32:
            entity_texture_count += 1
    if entity_texture_count > 6:
        raise PackError(f"entity textures have {entity_texture_count} 64x32 records, v53 runtime limit is 6")

    if len(records) > MAX_RECORDS:
        raise PackError(f"too many cache records: {len(records)} > {MAX_RECORDS}")
    return records


def validate_texture_record_for_v53(record):
    expected_payload = record.width * record.height
    if record.alpha_kind == ALPHA_MASK:
        if record.width != 16 or record.height != 16:
            raise PackError(f"{record.name}: alpha masks are currently supported only for 16x16 textures")
        expected_payload += TEXTURE_MASK_BYTES
    elif record.alpha_kind != ALPHA_NONE:
        raise PackError(f"{record.name}: unsupported alpha kind {record.alpha_kind}")

    if len(record.payload) != expected_payload:
        raise PackError(f"{record.name}: payload size {len(record.payload)} does not match expected {expected_payload}")
    if len(record.payload) % 4 != 0:
        raise PackError(f"{record.name}: payload byte count must be divisible by 4")

    if record.width == 16 and record.height == 16:
        return

    if record.width == 64 and record.height == 32:
        if record.alpha_kind != ALPHA_NONE:
            raise PackError(f"{record.name}: entity texture records are opaque in v53")
        return

    raise PackError(f"{record.name}: v53 accepts only 16x16 textures and 64x32 entity textures")


def validate_block_record_for_v53(record):
    if record.block_id < 0 or record.block_id > 15:
        raise PackError(f"{record.name}: block id {record.block_id} outside range 0..15")
    if len(record.payload) != BLOCK_DEF_BYTES:
        raise PackError(f"{record.name}: block payload size {len(record.payload)} must be {BLOCK_DEF_BYTES}")


def validate_block_model_record_for_v53(record):
    if record.block_id < 0 or record.block_id > 15:
        raise PackError(f"{record.name}: block id {record.block_id} outside range 0..15")
    if record.face_count <= 0:
        raise PackError(f"{record.name}: block model needs at least one face")
    if record.face_count > MAX_BLOCK_MODEL_FACES_PER_RECORD:
        raise PackError(f"{record.name}: block model has too many faces")
    if len(record.payload) != record.face_count * BLOCK_MODEL_FACE_BYTES:
        raise PackError(f"{record.name}: block model payload size does not match face count")


def validate_entity_record_for_v53(record):
    if record.entity_id < 0 or record.entity_id > 7:
        raise PackError(f"{record.name}: entity id {record.entity_id} outside range 0..7")
    if len(record.payload) != ENTITY_DEF_BYTES:
        raise PackError(f"{record.name}: entity payload size {len(record.payload)} must be {ENTITY_DEF_BYTES}")
    if record.word_count != (ENTITY_DEF_BYTES // 4):
        raise PackError(f"{record.name}: entity payload word count is invalid")


def validate_entity_model_record_for_v53(record):
    if record.model_id < 0 or record.model_id > 7:
        raise PackError(f"{record.name}: entity model id {record.model_id} outside range 0..7")
    if record.texture_id < 0 or record.texture_id > 31:
        raise PackError(f"{record.name}: entity model texture id {record.texture_id} outside range 0..31")
    if record.part_count <= 0:
        raise PackError(f"{record.name}: entity model needs at least one part")
    if record.part_count > MAX_ENTITY_MODEL_PARTS_PER_RECORD:
        raise PackError(f"{record.name}: entity model has too many parts")
    if len(record.payload) != record.part_count * ENTITY_MODEL_PART_BYTES:
        raise PackError(f"{record.name}: entity model payload size does not match part count")


def record_header(record, offset):
    if isinstance(record, TextureRecord):
        return struct.pack(
            ">IIIIIIII",
            record.texture_id,
            record.width,
            record.height,
            record.word_count,
            offset,
            len(record.payload),
            record.alpha_kind,
            record.flags,
        )

    if isinstance(record, DestroyStagesRecord):
        return struct.pack(
            ">IIIIIIII",
            0,
            DESTROY_STAGE_WIDTH,
            DESTROY_STAGE_HEIGHT,
            record.word_count,
            offset,
            len(record.payload),
            DESTROY_STAGE_COUNT,
            RECORD_KIND_DESTROY_STAGES_FLAG | record.flags,
        )

    if isinstance(record, ItemMeshRecord):
        return struct.pack(
            ">IIIIIIII",
            record.item_id,
            ITEM_MESH_RENDER_KIND,
            record.face_count,
            record.word_count,
            offset,
            len(record.payload),
            record.icon_texture_id,
            RECORD_KIND_ITEM_MESH_FLAG | record.flags,
        )

    if isinstance(record, BlockRecord):
        return struct.pack(
            ">IIIIIIII",
            record.block_id,
            record.payload[0],
            1,
            record.word_count,
            offset,
            len(record.payload),
            0,
            RECORD_KIND_BLOCK_DEF_FLAG | record.flags,
        )

    if isinstance(record, BlockModelRecord):
        return struct.pack(
            ">IIIIIIII",
            record.block_id,
            BLOCK_MODEL_RENDER_KIND,
            record.face_count,
            record.word_count,
            offset,
            len(record.payload),
            0,
            RECORD_KIND_BLOCK_MODEL_FLAG | record.flags,
        )

    if isinstance(record, EntityRecord):
        return struct.pack(
            ">IIIIIIII",
            record.entity_id,
            record.render_kind,
            1,
            record.word_count,
            offset,
            len(record.payload),
            record.texture_id,
            RECORD_KIND_ENTITY_DEF_FLAG | record.flags,
        )

    if isinstance(record, EntityModelRecord):
        return struct.pack(
            ">IIIIIIII",
            record.model_id,
            ENTITY_MODEL_RENDER_KIND,
            record.part_count,
            record.word_count,
            offset,
            len(record.payload),
            record.texture_id,
            RECORD_KIND_ENTITY_MODEL_FLAG | record.flags,
        )

    raise PackError(f"unsupported cache record type {type(record).__name__}")


def write_tca(records, output_path):
    payload_offset = HEADER_BYTES + len(records) * RECORD_BYTES
    record_bytes = bytearray()
    payload_bytes = bytearray()

    for record in records:
        offset = payload_offset + len(payload_bytes)
        record_bytes.extend(record_header(record, offset))
        payload_bytes.extend(record.payload)

    out = bytearray()
    out.extend(MAGIC)
    out.extend(struct.pack(">III", VERSION, len(records), 0))
    out.extend(record_bytes)
    out.extend(payload_bytes)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(bytes(out))


def default_game_folder():
    return Path(__file__).resolve().parents[1] / "Tinycraft"


def default_source_folder():
    return Path(__file__).resolve().parents[1] / "assets"


def command_pack(args):
    source_path = Path(args.source)
    records = make_records(source_path, args)
    if args.output:
        output_path = Path(args.output)
    else:
        output_path = Path(args.game_folder) / "assets.tca" if args.game_folder else default_game_folder() / "assets.tca"
    write_tca(records, output_path)

    print(f"wrote {output_path} ({output_path.stat().st_size} bytes)")
    if args.pack_info is not None:
        print(f"  source resource pack: {args.pack_info['resource_pack']}")
        print(f"  source behavior pack: {args.pack_info['behavior_pack']}")
        for ignored in args.pack_info["ignored"]:
            print(f"  note: {ignored} is present but not emitted by TCAC v8")
    for record in records:
        if isinstance(record, TextureRecord):
            alpha_label = "mask" if record.alpha_kind == ALPHA_MASK else "opaque"
            print(f"  tex  id {record.texture_id:2d}  {record.width:2d}x{record.height:<2d}  {alpha_label:6s}  {record.name}  <- {record.source}")
        elif isinstance(record, DestroyStagesRecord):
            counts = ",".join(str(value) for value in record.opaque_counts)
            print(f"  destroy stages {len(record.sources):2d}  16x16 mask  opaque_pixels=[{counts}]")
        elif isinstance(record, ItemMeshRecord):
            print(f"  mesh item {record.item_id:2d}  faces={record.face_count:<2d}  icon_tex={record.icon_texture_id:2d}  {record.name}")
        elif isinstance(record, BlockModelRecord):
            print(f"  model block {record.block_id:2d}  faces={record.face_count:<2d}  {record.name}")
        elif isinstance(record, EntityRecord):
            print(f"  entity id {record.entity_id:2d}  render={record.render_kind:<2d}  tex={record.texture_id:<2d}  {record.name}")
        elif isinstance(record, EntityModelRecord):
            print(f"  entity model {record.model_id:2d}  parts={record.part_count:<2d}  tex={record.texture_id:<2d}  {record.name}")
        else:
            print(f"  block id {record.block_id:2d}  render={record.payload[0]:<2d}  drop={record.payload[11]:<2d}  {record.name}")


def command_inspect(args):
    path = Path(args.file)
    data = path.read_bytes()
    if len(data) < HEADER_BYTES:
        raise PackError("file is too small to be a TCAC cache")
    if data[:4] != MAGIC:
        raise PackError("bad magic; expected TCAC")
    version, count, reserved = struct.unpack(">III", data[4:16])
    print(f"magic=TCAC version={version} records={count} reserved={reserved}")
    pos = HEADER_BYTES
    for i in range(count):
        if pos + RECORD_BYTES > len(data):
            raise PackError("record table is truncated")
        rec_id, width, height, words, payload_off, payload_bytes, alpha, flags = struct.unpack(">IIIIIIII", data[pos:pos + RECORD_BYTES])
        record_kind = flags >> RECORD_KIND_SHIFT
        if record_kind == RECORD_KIND_DESTROY_STAGES:
            print(
                f"record {i}: destroy_stages stages={alpha} size={width}x{height} words={words} "
                f"payload_offset={payload_off} payload_bytes={payload_bytes} flags={flags & 0xFFFFFF}"
            )
        elif record_kind == RECORD_KIND_ITEM_MESH:
            print(
                f"record {i}: item_mesh item_id={rec_id} render_kind={width} faces={height} words={words} "
                f"payload_offset={payload_off} payload_bytes={payload_bytes} icon_tex={alpha} flags={flags & 0xFFFFFF}"
            )
        elif record_kind == RECORD_KIND_BLOCK_DEF:
            print(
                f"record {i}: block id={rec_id} render={width} words={words} "
                f"payload_offset={payload_off} payload_bytes={payload_bytes} flags={flags & 0xFFFFFF}"
            )
        elif record_kind == RECORD_KIND_BLOCK_MODEL:
            print(
                f"record {i}: block_model block_id={rec_id} render_kind={width} faces={height} words={words} "
                f"payload_offset={payload_off} payload_bytes={payload_bytes} flags={flags & 0xFFFFFF}"
            )
        elif record_kind == RECORD_KIND_ENTITY_DEF:
            print(
                f"record {i}: entity id={rec_id} render_kind={width} words={words} "
                f"payload_offset={payload_off} payload_bytes={payload_bytes} texture_id={alpha} flags={flags & 0xFFFFFF}"
            )
        elif record_kind == RECORD_KIND_ENTITY_MODEL:
            print(
                f"record {i}: entity_model model_id={rec_id} render_kind={width} parts={height} words={words} "
                f"payload_offset={payload_off} payload_bytes={payload_bytes} texture_id={alpha} flags={flags & 0xFFFFFF}"
            )
        else:
            alpha_label = "mask" if alpha == ALPHA_MASK else "opaque"
            print(
                f"record {i}: texture id={rec_id} size={width}x{height} words={words} "
                f"payload_offset={payload_off} payload_bytes={payload_bytes} alpha={alpha_label} flags={flags}"
            )
        pos += RECORD_BYTES


def build_parser():
    parser = argparse.ArgumentParser(description="Pack TinyCraft assets into Tinycraft/assets.tca")
    sub = parser.add_subparsers(dest="command")

    pack = sub.add_parser("pack", help="pack resource_pack + behavior_pack into assets.tca")
    pack.add_argument(
        "source",
        nargs="?",
        default=str(default_source_folder()),
        help="folder containing resource_pack and behavior_pack; default is ../assets beside this tool",
    )
    pack.add_argument("-o", "--output", help="explicit output .tca path; overrides --game-folder")
    pack.add_argument("--game-folder", help="deployable Tinycraft folder; default is ../Tinycraft beside this tool's version folder")
    pack.add_argument("--texture-ids", help="optional texture_ids.json mapping")
    pack.add_argument("--transparent-fill", default="000000",
                      help="RRGGBB color used when flattening transparent PNG pixels; default 000000")
    pack.set_defaults(func=command_pack)

    inspect = sub.add_parser("inspect", help="print the records inside a .tca file")
    inspect.add_argument("file", help="assets.tca file")
    inspect.set_defaults(func=command_inspect)

    return parser


def main(argv):
    if argv and argv[0] not in ("pack", "inspect", "-h", "--help"):
        argv = ["pack"] + argv
    parser = build_parser()
    args = parser.parse_args(argv)
    if not hasattr(args, "func"):
        parser.print_help()
        return 2
    try:
        args.func(args)
        return 0
    except PackError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
