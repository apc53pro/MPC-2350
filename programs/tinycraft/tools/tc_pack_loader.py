"""Load TinyCraft's Bedrock-style resource/behavior pack source tree.

The loader resolves editable cross-pack JSON into the legacy in-memory
manifest shape consumed by tc_asset_pack.py. Runtime output is TCAC v8.
"""

import json
from pathlib import Path


class PackLayoutError(Exception):
    pass


def _read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except FileNotFoundError as exc:
        raise PackLayoutError(f"missing required file {path}") from exc
    except json.JSONDecodeError as exc:
        raise PackLayoutError(f"invalid JSON in {path}: {exc}") from exc


def _norm(value):
    return str(value).strip().replace("\\", "/").replace("-", "_").replace(" ", "_").lower()


def _short_identifier(value):
    key = _norm(value)
    if ":" in key:
        key = key.split(":", 1)[1]
    return key


def _geometry_name(value):
    key = _short_identifier(value)
    if "." in key:
        key = key.rsplit(".", 1)[1]
    return key


def _is_tinycraft_identifier(value):
    return _norm(value).startswith("tinycraft:")


def _is_tinycraft_geometry(value):
    key = _norm(value)
    return key.startswith("geometry.tinycraft.") or key.startswith("tinycraft:")


def _object(value, field):
    if not isinstance(value, dict):
        raise PackLayoutError(f"{field} must be a JSON object")
    return value


def _vec3(value, field):
    if not isinstance(value, list) or len(value) != 3:
        raise PackLayoutError(f"{field} must be [x, y, z]")
    return [float(value[0]), float(value[1]), float(value[2])]


def _pixel_vec(value, field):
    return [component / 16.0 for component in _vec3(value, field)]


def _iter_json(folder):
    if not folder.exists():
        return []
    return sorted(path for path in folder.rglob("*.json") if path.is_file())


def _validate_manifest(path, module_type):
    data = _object(_read_json(path), path)
    header = _object(data.get("header"), f"{path}: header")
    uuid = str(header.get("uuid", "")).strip()
    version = header.get("version")
    if not uuid:
        raise PackLayoutError(f"{path}: header.uuid is required")
    if not isinstance(version, list) or len(version) != 3:
        raise PackLayoutError(f"{path}: header.version must be [major, minor, patch]")
    modules = data.get("modules")
    if not isinstance(modules, list) or not modules:
        raise PackLayoutError(f"{path}: modules must be a non-empty list")
    if not any(isinstance(module, dict) and module.get("type") == module_type for module in modules):
        raise PackLayoutError(f"{path}: expected a {module_type!r} module")
    uuids = [uuid]
    for module in modules:
        if not isinstance(module, dict):
            raise PackLayoutError(f"{path}: every module must be an object")
        module_uuid = str(module.get("uuid", "")).strip()
        if module_uuid:
            uuids.append(module_uuid)
    if len(uuids) != len(set(uuids)):
        raise PackLayoutError(f"{path}: header and module UUIDs must be unique")
    return data, uuid, version


def _validate_dependency(behavior_manifest, resource_uuid, resource_version, path):
    dependencies = behavior_manifest.get("dependencies", [])
    for dependency in dependencies:
        if not isinstance(dependency, dict):
            continue
        if dependency.get("uuid") == resource_uuid and dependency.get("version") == resource_version:
            return
    raise PackLayoutError(
        f"{path}: behavior pack must depend on resource pack {resource_uuid} version {resource_version}"
    )


def _resource_file(resource_pack, reference, extensions=(".png", ".rgb332", ".rg332")):
    if isinstance(reference, list):
        if not reference:
            raise PackLayoutError("texture reference list cannot be empty")
        reference = reference[0]
    if not isinstance(reference, str) or not reference.strip():
        raise PackLayoutError(f"invalid resource path {reference!r}")
    relative = Path(*reference.replace("\\", "/").split("/"))
    candidate = resource_pack / relative
    candidates = [candidate]
    if candidate.suffix == "":
        candidates.extend(candidate.with_suffix(ext) for ext in extensions)
    for path in candidates:
        if path.exists() and path.is_file():
            return path.resolve()
    tried = ", ".join(str(path) for path in candidates)
    raise PackLayoutError(f"missing resource {reference!r}; tried {tried}")


def _behavior_file(behavior_pack, reference):
    relative = Path(*str(reference).replace("\\", "/").split("/"))
    candidate = behavior_pack / relative
    if candidate.suffix == "":
        candidate = candidate.with_suffix(".json")
    if not candidate.exists():
        raise PackLayoutError(f"missing behavior resource {candidate}")
    return candidate


def _texture_registry(path, resource_pack):
    data = _object(_read_json(path), path)
    texture_data = _object(data.get("texture_data", {}), f"{path}: texture_data")
    entries = []
    lookup = {}
    for identifier, raw in texture_data.items():
        spec = {"textures": raw} if isinstance(raw, str) else _object(raw, f"{path}: {identifier}")
        if not _is_tinycraft_identifier(identifier) and "tinycraft_texture_id" not in spec:
            continue
        source = _resource_file(resource_pack, spec.get("textures"))
        short_name = _short_identifier(identifier)
        runtime_name = str(spec.get("tinycraft_runtime_name", short_name))
        texture_id = spec.get("tinycraft_texture_id", runtime_name)
        entry = {
            "identifier": str(identifier),
            "name": runtime_name,
            "texture_id": texture_id,
            "source": str(source),
        }
        if "width" in spec:
            entry["width"] = spec["width"]
        if "height" in spec:
            entry["height"] = spec["height"]
        if "crop" in spec:
            entry["crop"] = spec["crop"]
        entries.append(entry)
        lookup[_norm(identifier)] = entry
        lookup[_norm(short_name)] = entry
    return entries, lookup


def _geometry_map(resource_pack):
    geometries = {}
    for path in _iter_json(resource_pack / "models"):
        data = _object(_read_json(path), path)
        values = data.get("minecraft:geometry", [])
        if not isinstance(values, list):
            raise PackLayoutError(f"{path}: minecraft:geometry must be a list")
        for geometry in values:
            geometry = _object(geometry, f"{path}: geometry")
            description = _object(geometry.get("description"), f"{path}: geometry description")
            identifier = str(description.get("identifier", "")).strip()
            if not identifier:
                raise PackLayoutError(f"{path}: geometry description.identifier is required")
            if not _is_tinycraft_geometry(identifier):
                continue
            key = _norm(identifier)
            if key in geometries:
                raise PackLayoutError(f"geometry {identifier} is defined more than once")
            geometries[key] = (geometry, path)
    return geometries


def _client_entities(resource_pack):
    result = {}
    textures = []
    texture_ids = {}
    for path in _iter_json(resource_pack / "entity"):
        data = _object(_read_json(path), path)
        client = _object(data.get("minecraft:client_entity"), f"{path}: minecraft:client_entity")
        description = _object(client.get("description"), f"{path}: description")
        identifier = str(description.get("identifier", "")).strip()
        if not identifier:
            raise PackLayoutError(f"{path}: client entity identifier is required")
        if not _is_tinycraft_identifier(identifier):
            continue
        runtime = _object(description.get("tinycraft:runtime", {}), f"{path}: tinycraft:runtime")
        texture_aliases = _object(description.get("textures", {}), f"{path}: textures")
        geometry_aliases = _object(description.get("geometry", {}), f"{path}: geometry")
        texture_ref = texture_aliases.get("default")
        geometry_id = geometry_aliases.get("default")
        if texture_ref is None or geometry_id is None:
            raise PackLayoutError(f"{path}: default texture and geometry aliases are required")
        source = _resource_file(resource_pack, texture_ref)
        short_name = _short_identifier(identifier)
        texture_name = str(runtime.get("texture_name", f"entity/{short_name}"))
        texture_id = runtime.get("texture_id", texture_name)
        entry = {
            "identifier": identifier,
            "name": short_name,
            "texture": texture_name,
            "texture_id": texture_id,
            "texture_source": source,
            "geometry_id": str(geometry_id),
            "model": _geometry_name(geometry_id),
            "render_kind": runtime.get("render_kind", "cuboids"),
        }
        key = _norm(identifier)
        if key in result:
            raise PackLayoutError(f"client entity {identifier} is defined more than once")
        result[key] = entry
        textures.append({
            "name": texture_name,
            "texture_id": texture_id,
            "source": str(source),
        })
        texture_ids[_norm(texture_name)] = texture_id
        texture_ids[_norm(short_name)] = texture_id
    return result, textures, texture_ids


def _entity_model(geometry, path, client):
    description = _object(geometry.get("description"), f"{path}: geometry description")
    runtime = _object(description.get("tinycraft:runtime", {}), f"{path}: geometry runtime")
    parts = []
    bones = geometry.get("bones", [])
    if not isinstance(bones, list):
        raise PackLayoutError(f"{path}: bones must be a list")
    for bone in bones:
        bone = _object(bone, f"{path}: bone")
        bone_runtime = _object(bone.get("tinycraft:runtime", {}), f"{path}: bone runtime")
        pivot = _pixel_vec(bone.get("pivot", [0, 0, 0]), f"{path}: bone pivot")
        cubes = bone.get("cubes", [])
        if not isinstance(cubes, list):
            raise PackLayoutError(f"{path}: bone cubes must be a list")
        for cube_index, cube in enumerate(cubes):
            cube = _object(cube, f"{path}: cube")
            origin_pixels = _vec3(cube.get("origin"), f"{path}: cube origin")
            size_pixels = _vec3(cube.get("size"), f"{path}: cube size")
            from_vec = [value / 16.0 for value in origin_pixels]
            to_vec = [(origin_pixels[i] + size_pixels[i]) / 16.0 for i in range(3)]
            uv = cube.get("uv", [0, 0])
            if not isinstance(uv, list) or len(uv) < 2:
                raise PackLayoutError(f"{path}: TinyCraft currently requires per-cube [u, v] UVs")
            uv_size = cube.get("tinycraft:uv_size", size_pixels)
            part = {
                "name": str(bone.get("name", f"part_{cube_index}")),
                "kind": bone_runtime.get("kind", "body"),
                "anim": bone_runtime.get("anim", "none"),
                "lod_group": bone_runtime.get("lod_group", cube.get("tinycraft:lod_group", 0)),
                "from": from_vec,
                "to": to_vec,
                "pivot": pivot,
                "uv": [uv[0], uv[1]],
                "size": [round(float(value)) for value in uv_size],
            }
            parts.append(part)
    return {
        "name": client["model"],
        "id": runtime.get("model_id", client["model"]),
        "texture": client["texture"],
        "parts": parts,
    }


def _loot_drop(path):
    data = _object(_read_json(path), path)
    pools = data.get("pools", [])
    if not isinstance(pools, list):
        raise PackLayoutError(f"{path}: pools must be a list")
    if not pools:
        return 0, 0, data.get("tinycraft:required_tool")
    if len(pools) != 1:
        raise PackLayoutError(f"{path}: TCAC v8 block drops support exactly one loot pool")
    pool = _object(pools[0], f"{path}: pool")
    if pool.get("rolls", 1) != 1:
        raise PackLayoutError(f"{path}: TCAC v8 block drops require one roll")
    entries = pool.get("entries", [])
    if not isinstance(entries, list) or len(entries) != 1:
        raise PackLayoutError(f"{path}: TCAC v8 block drops require exactly one entry")
    entry = _object(entries[0], f"{path}: loot entry")
    if entry.get("type", "item") != "item":
        raise PackLayoutError(f"{path}: TCAC v8 block drops support item entries only")
    count = 1
    for function in entry.get("functions", []):
        function = _object(function, f"{path}: loot function")
        if function.get("function") != "set_count":
            raise PackLayoutError(f"{path}: unsupported loot function {function.get('function')!r}")
        value = function.get("count", 1)
        if isinstance(value, dict):
            minimum = value.get("min", 1)
            maximum = value.get("max", minimum)
            if minimum != maximum:
                raise PackLayoutError(f"{path}: TCAC v8 requires a fixed drop count")
            value = minimum
        count = int(value)
    return entry.get("name", 0), count, data.get("tinycraft:required_tool")


def _material_texture(materials, key, fallback):
    value = materials.get(key, fallback)
    if isinstance(value, dict):
        value = value.get("texture", fallback)
    if value is None:
        return "missing"
    return _short_identifier(value)


def _block_model(geometry, path, block_name, materials):
    default_texture = _material_texture(materials, "*", "missing")
    face_textures = {
        "top": _material_texture(materials, "up", default_texture),
        "bottom": _material_texture(materials, "down", default_texture),
        "front": _material_texture(materials, "north", default_texture),
        "back": _material_texture(materials, "south", default_texture),
        "left": _material_texture(materials, "west", default_texture),
        "right": _material_texture(materials, "east", default_texture),
    }
    boxes = []
    for bone in geometry.get("bones", []):
        bone = _object(bone, f"{path}: block bone")
        for cube in bone.get("cubes", []):
            cube = _object(cube, f"{path}: block cube")
            origin = _vec3(cube.get("origin"), f"{path}: block cube origin")
            size = _vec3(cube.get("size"), f"{path}: block cube size")
            boxes.append({
                "from": [round(value) for value in origin],
                "to": [round(origin[i] + size[i]) for i in range(3)],
                "texture": face_textures,
                "uv": cube.get("tinycraft:uv_rect", [0, 0, 15, 15]),
            })
    return {
        "name": block_name,
        "block": block_name,
        "texture": default_texture,
        "boxes": boxes,
    }


def _blocks(behavior_pack, geometries):
    entries = []
    models = []
    for path in _iter_json(behavior_pack / "blocks"):
        data = _object(_read_json(path), path)
        block = _object(data.get("minecraft:block"), f"{path}: minecraft:block")
        description = _object(block.get("description"), f"{path}: block description")
        identifier = str(description.get("identifier", "")).strip()
        if not identifier:
            raise PackLayoutError(f"{path}: block identifier is required")
        if not _is_tinycraft_identifier(identifier):
            continue
        name = _short_identifier(identifier)
        components = _object(block.get("components", {}), f"{path}: block components")
        runtime = _object(components.get("tinycraft:runtime", {}), f"{path}: tinycraft:runtime")
        mining = components.get("minecraft:destructible_by_mining", {})
        if isinstance(mining, dict):
            break_seconds = float(mining.get("seconds_to_destroy", 0 if name == "air" else 0.9))
        elif mining is False:
            break_seconds = 0
        else:
            break_seconds = 0.9
        collision = components.get("minecraft:collision_box", name != "air")
        materials = components.get("minecraft:material_instances", {})
        if not isinstance(materials, dict):
            raise PackLayoutError(f"{path}: minecraft:material_instances must be an object")
        default_texture = _material_texture(materials, "*", "missing")
        entry = {
            "name": name,
            "id": runtime.get("block_id", name),
            "render_class": runtime.get("render_class", "fast_cube" if name != "air" else "none"),
            "placeable": runtime.get("placeable", name != "air"),
            "solid": runtime.get("solid", collision is not False),
            "stops_sky": runtime.get("stops_sky", name != "air"),
            "light_emission": components.get("minecraft:light_emission", runtime.get("light_emission", 0)),
            "ui_color": runtime.get("ui_color", 0),
            "break_ms": round(break_seconds * 1000.0),
            "preferred_tool": runtime.get("preferred_tool"),
            "preferred_break_ms": round(float(runtime.get("preferred_break_seconds", break_seconds)) * 1000.0),
            "texture": default_texture,
            "top_texture": _material_texture(materials, "up", default_texture),
            "bottom_texture": _material_texture(materials, "down", default_texture),
            "side_texture": default_texture,
            "icon_texture": _short_identifier(runtime.get("icon_texture", default_texture)),
            "name_label": runtime.get("name_label", name),
            "item": runtime.get("item", identifier if name != "air" else 0),
            "opens_crafting": runtime.get("opens_crafting", False),
        }
        loot_ref = components.get("minecraft:loot")
        if loot_ref:
            drop, count, required_tool = _loot_drop(_behavior_file(behavior_pack, loot_ref))
            entry["drop"] = drop
            entry["drop_count"] = count
            if required_tool is not None:
                entry["drop_required_tool"] = required_tool
        elif name == "air":
            entry["drop"] = 0
            entry["drop_count"] = 0
        geometry_id = components.get("minecraft:geometry")
        if geometry_id:
            geometry_key = _norm(geometry_id)
            if geometry_key not in geometries:
                raise PackLayoutError(f"{path}: unknown geometry {geometry_id!r}")
            geometry, geometry_path = geometries[geometry_key]
            models.append(_block_model(geometry, geometry_path, name, materials))
        entries.append(entry)
    entries.sort(key=lambda entry: int(entry["id"]) if isinstance(entry["id"], int) else 999)
    return entries, models


def _spawn_rules(behavior_pack):
    rules = {}
    for path in _iter_json(behavior_pack / "spawn_rules"):
        data = _object(_read_json(path), path)
        root = _object(data.get("minecraft:spawn_rules"), f"{path}: minecraft:spawn_rules")
        description = _object(root.get("description"), f"{path}: spawn description")
        identifier = str(description.get("identifier", "")).strip()
        if not _is_tinycraft_identifier(identifier):
            continue
        conditions = root.get("conditions", [])
        if not identifier or not isinstance(conditions, list) or not conditions:
            raise PackLayoutError(f"{path}: spawn identifier and at least one condition are required")
        condition = _object(conditions[0], f"{path}: spawn condition")
        runtime = _object(condition.get("tinycraft:runtime", {}), f"{path}: spawn runtime")
        brightness = condition.get("minecraft:brightness_filter", {})
        herd = condition.get("minecraft:herd", {})
        rules[_norm(identifier)] = {
            "spawn_group": runtime.get("initial_spawn_count", herd.get("max_size", 0) if isinstance(herd, dict) else 0),
            "spawn_floor": runtime.get("spawn_floor", "grass"),
            "spawn_light_min": brightness.get("min", 0) if isinstance(brightness, dict) else 0,
        }
    return rules


def _entities(behavior_pack, clients, geometries):
    spawn_rules = _spawn_rules(behavior_pack)
    entries = []
    models = []
    seen_clients = set()
    for path in _iter_json(behavior_pack / "entities"):
        data = _object(_read_json(path), path)
        entity = _object(data.get("minecraft:entity"), f"{path}: minecraft:entity")
        description = _object(entity.get("description"), f"{path}: entity description")
        identifier = str(description.get("identifier", "")).strip()
        if not _is_tinycraft_identifier(identifier):
            continue
        key = _norm(identifier)
        if not identifier or key not in clients:
            raise PackLayoutError(f"{path}: entity needs a matching resource_pack/entity client definition")
        seen_clients.add(key)
        client = clients[key]
        components = _object(entity.get("components", {}), f"{path}: entity components")
        runtime = _object(components.get("tinycraft:runtime", {}), f"{path}: tinycraft:runtime")
        collision = components.get("minecraft:collision_box", {})
        collision = collision if isinstance(collision, dict) else {}
        entry = dict(runtime)
        entry.update({
            "name": _short_identifier(identifier),
            "id": runtime.get("entity_id", _short_identifier(identifier)),
            "texture": client["texture"],
            "render_kind": client["render_kind"],
            "model": client["model"],
        })
        if "hitbox" not in entry:
            width = float(collision.get("width", 0.9)) * 0.5
            entry["hitbox"] = [width, width]
        entry.setdefault("height", collision.get("height", 0.95))
        if key in spawn_rules:
            entry.update(spawn_rules[key])
        loot_ref = runtime.get("loot_table")
        if loot_ref:
            _read_json(_behavior_file(behavior_pack, loot_ref))
        geometry_key = _norm(client["geometry_id"])
        if geometry_key not in geometries:
            raise PackLayoutError(f"{path}: unknown client geometry {client['geometry_id']!r}")
        geometry, geometry_path = geometries[geometry_key]
        models.append(_entity_model(geometry, geometry_path, client))
        entries.append(entry)
    unmatched = sorted(set(clients) - seen_clients)
    if unmatched:
        raise PackLayoutError(f"client entities without behavior definitions: {', '.join(unmatched)}")
    entries.sort(key=lambda entry: int(entry["id"]) if isinstance(entry["id"], int) else 999)
    models.sort(key=lambda entry: int(entry["id"]) if isinstance(entry["id"], int) else 999)
    return entries, models


def _flat_items(behavior_pack, item_textures):
    entries = []
    for path in _iter_json(behavior_pack / "items"):
        data = _object(_read_json(path), path)
        item = _object(data.get("minecraft:item"), f"{path}: minecraft:item")
        description = _object(item.get("description"), f"{path}: item description")
        identifier = str(description.get("identifier", "")).strip()
        if not _is_tinycraft_identifier(identifier):
            continue
        components = _object(item.get("components", {}), f"{path}: item components")
        runtime = _object(components.get("tinycraft:runtime", {}), f"{path}: tinycraft:runtime")
        if _norm(runtime.get("render", "none")) != "flat":
            continue
        icon = components.get("minecraft:icon")
        if isinstance(icon, dict):
            icon = icon.get("texture")
        icon_key = _norm(icon)
        texture = item_textures.get(icon_key) or item_textures.get(_short_identifier(icon))
        if texture is None:
            raise PackLayoutError(f"{path}: icon {icon!r} is absent from textures/item_texture.json")
        name = _short_identifier(identifier)
        entries.append({
            "name": name,
            "item": runtime.get("item_id", name),
            "item_id": runtime.get("item_id", name),
            "texture": texture["name"],
            "texture_id": texture["texture_id"],
            "source": texture["source"],
        })
    entries.sort(key=lambda entry: int(entry["item_id"]) if isinstance(entry["item_id"], int) else 999)
    return entries


def _destroy_stage_files(resource_pack):
    environment = resource_pack / "textures" / "environment"
    stages = [environment / f"destroy_stage_{stage}.png" for stage in range(10)]
    present = [path.exists() for path in stages]
    if not any(present):
        return []
    if not all(present):
        missing = ", ".join(path.name for path, exists in zip(stages, present) if not exists)
        raise PackLayoutError(f"destroy-stage texture sequence is incomplete; missing {missing}")
    return [str(path.resolve()) for path in stages]


def _nonempty_files(folder):
    if not folder.exists():
        return []
    return [path for path in folder.rglob("*") if path.is_file() and path.name != ".keep"]


def load_pack_pair(source_root):
    source_root = Path(source_root).resolve()
    resource_pack = source_root / "resource_pack"
    behavior_pack = source_root / "behavior_pack"
    if not resource_pack.is_dir() or not behavior_pack.is_dir():
        raise PackLayoutError(
            f"{source_root} must contain sibling resource_pack and behavior_pack directories"
        )

    _, resource_uuid, resource_version = _validate_manifest(resource_pack / "manifest.json", "resources")
    behavior_manifest, _, _ = _validate_manifest(behavior_pack / "manifest.json", "data")
    _validate_dependency(behavior_manifest, resource_uuid, resource_version, behavior_pack / "manifest.json")

    terrain_entries, terrain_lookup = _texture_registry(
        resource_pack / "textures" / "terrain_texture.json", resource_pack
    )
    _, item_lookup = _texture_registry(
        resource_pack / "textures" / "item_texture.json", resource_pack
    )
    geometries = _geometry_map(resource_pack)
    clients, entity_textures, entity_texture_ids = _client_entities(resource_pack)
    blocks, block_models = _blocks(behavior_pack, geometries)
    entities, entity_models = _entities(behavior_pack, clients, geometries)
    flat_items = _flat_items(behavior_pack, item_lookup)
    destroy_stages = _destroy_stage_files(resource_pack)

    texture_ids = {}
    for entry in terrain_entries:
        texture_ids[_norm(entry["name"])] = entry["texture_id"]
    for entry in item_lookup.values():
        texture_ids[_norm(entry["name"])] = entry["texture_id"]
    texture_ids.update(entity_texture_ids)

    unsupported_roots = [
        "animation_controllers", "animations", "attachables", "block_culling",
        "fogs", "items", "materials", "particles", "render_controllers",
        "sounds", "texts", "ui",
    ]
    ignored = []
    for relative in unsupported_roots:
        files = _nonempty_files(resource_pack / relative)
        if files:
            ignored.append(f"resource_pack/{relative} ({len(files)} file(s))")
    for relative in ["blocks.json", "sounds.json", "biomes_client.json", "textures/flipbook_textures.json"]:
        path = resource_pack / Path(*relative.split("/"))
        if path.exists():
            ignored.append(f"resource_pack/{relative}")
    behavior_unsupported = [
        "animation_controllers", "animations", "biomes", "cameras", "dialogue",
        "features", "feature_rules", "functions", "item_catalog", "recipes",
        "scripts", "structures", "texts", "trading",
    ]
    for relative in behavior_unsupported:
        files = _nonempty_files(behavior_pack / relative)
        if files:
            ignored.append(f"behavior_pack/{relative} ({len(files)} file(s))")
    for relative in ["loot_tables/entities", "loot_tables/chests", "loot_tables/equipment", "loot_tables/gameplay"]:
        files = _nonempty_files(behavior_pack / Path(*relative.split("/")))
        if files:
            ignored.append(f"behavior_pack/{relative} ({len(files)} file(s))")

    manifest = {
        "textures": sorted(terrain_entries + entity_textures, key=lambda entry: int(entry["texture_id"])),
        "blocks": blocks,
        "block_models": block_models,
        "entities": entities,
        "entity_models": entity_models,
        "flat_items": flat_items,
        "destroy_stages": destroy_stages,
        "_texture_ids": texture_ids,
    }
    info = {
        "resource_pack": resource_pack,
        "behavior_pack": behavior_pack,
        "ignored": ignored,
        "terrain_registry_entries": len(terrain_entries),
        "item_registry_entries": len({id(value): value for value in item_lookup.values()}),
    }
    return manifest, info
