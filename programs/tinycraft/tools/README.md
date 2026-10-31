# TinyCraft Pack Converter

`tc_asset_pack.py` converts the editable pack pair in `../assets/` into the
deployable `../Tinycraft/assets.tca` cache used by TinyCraft v59.

The NARM program does not parse JSON or PNG during gameplay. The PC converter
validates the files, resolves cross-pack references, converts textures to
RGB332, bakes geometry, and writes compact TCAC v8 records.

## Quick Start

From the package root:

```powershell
python .\tools\tc_asset_pack.py pack .\assets
python .\tools\tc_asset_pack.py inspect .\Tinycraft\assets.tca
```

The source argument is optional because `../assets` relative to the tool is the
default:

```powershell
python .\tools\tc_asset_pack.py pack
```

## Pack Layout

The source root must contain sibling resource and behavior packs:

```text
assets/
  resource_pack/
    manifest.json
    blocks.json
    sounds.json
    biomes_client.json
    animation_controllers/
    animations/
    attachables/
    block_culling/
    entity/
    fogs/
    items/
    materials/
    models/
      entity/
      blocks/
      attachables/
    particles/
    render_controllers/
    sounds/
      random/
      mob/
    texts/
      font/
    textures/
      terrain_texture.json
      item_texture.json
      flipbook_textures.json
      blocks/
      items/
      entity/
      particle/
      ui/
      misc/
      environment/
      colormap/
    ui/
  behavior_pack/
    manifest.json
    animation_controllers/
    animations/
    biomes/
    blocks/
    cameras/presets/
    dialogue/
    entities/
    features/
    feature_rules/
    functions/
    item_catalog/
    items/
    loot_tables/
      blocks/
      entities/
      chests/
      equipment/
      gameplay/
    recipes/
    scripts/
    spawn_rules/
    structures/
    texts/
    trading/
```

Empty categories contain `.keep` files so the complete layout survives ZIP
archives and folder copies.

## Responsibilities

The resource pack answers how content looks and sounds:

- `textures/terrain_texture.json` maps block texture names to image files.
- `textures/item_texture.json` maps item icon names to PNG files.
- `entity/*.entity.json` binds entity identifiers to textures and geometry.
- `models/entity/*.geo.json` contains entity bones, cubes, pivots, and UVs.
- `models/blocks/*.geo.json` contains custom block geometry.
- `render_controllers/`, `animations/`, and the other visual categories are
  ready for later converter/runtime support.

The behavior pack answers what content does:

- `blocks/*.json` contains hardness, collision, textures, model links, tools,
  item links, and crafting-table behavior.
- `items/*.json` contains item identity, icon links, stack data, and TinyCraft
  render/tool metadata.
- `entities/*.json` contains controller selection, hitboxes, movement, health,
  and runtime limits.
- `spawn_rules/*.json` contains light, floor, and initial group data.
- `loot_tables/blocks/*.json` supplies block drops and required tools.
- `loot_tables/entities/*.json` is validated and organized now; entity death
  drops still use the existing runtime drop dispatch until loot records are
  added to TCAC.

## Compiled Chains

The v59 converter resolves these chains:

```text
behavior_pack/blocks/*.json
  -> resource_pack/textures/terrain_texture.json
  -> resource_pack/textures/blocks/*
  -> optional resource_pack/models/blocks/*.geo.json
  -> behavior_pack/loot_tables/blocks/*.json

behavior_pack/items/*.json
  -> resource_pack/textures/item_texture.json
  -> resource_pack/textures/items/*.png
  -> masked texture + baked flat item mesh

behavior_pack/entities/*.json
  -> behavior_pack/spawn_rules/*.json
  -> resource_pack/entity/*.entity.json
  -> resource_pack/models/entity/*.geo.json
  -> resource_pack/textures/entity/*

resource_pack/textures/environment/destroy_stage_0.png .. destroy_stage_9.png
  -> ten RGB332 textures + ten thresholded 1-bit masks
  -> one TCAC v8 destroy-stage record
```

Both `manifest.json` files are required. The behavior pack dependency must
match the resource pack header UUID and version. Entity identifiers must match
across behavior entity, spawn rule, and client entity files.

## TinyCraft Extensions

The JSON is Bedrock-shaped, with `tinycraft:*` fields for fixed VM details not
represented by ordinary Bedrock files:

- `tinycraft_texture_id` assigns a stable runtime texture slot.
- `tinycraft_runtime_name` assigns names such as `item/wood_sword`.
- `tinycraft:runtime` on blocks stores block ID, UI color, preferred tool,
  fast/custom render class, and TinyCraft behavior flags.
- `tinycraft:runtime` on items stores item ID, flat/custom render selection,
  tool kind, and combat values.
- `tinycraft:runtime` on entities stores entity ID, controller name, separate
  X/Z hitbox radii, movement rates, LOD, and AI tick interval.
- `tinycraft:runtime` on geometry descriptions/bones stores model ID, runtime
  part kind, limb animation role, and LOD group.
- `tinycraft:uv_size` preserves special cuboid atlas dimensions where they do
  not match a cube's unrotated physical size.
- `tinycraft:required_tool` on a block loot table controls whether its drop is
  allowed without the expected tool.

These extensions are resolved entirely on the PC and do not add runtime JSON
cost.

## Texture Inputs

Terrain and entity texture registry paths may resolve to PNG, `.rgb332`, or
`.rg332` files. PNGs can be indexed, grayscale, RGB, or RGBA and must be
non-interlaced.

Flat item textures must currently be `16x16` PNGs with alpha. The converter
creates a 256-byte RGB332 texture, a 32-byte one-bit alpha mask, and a thin 3D
mesh from visible pixel runs. No flat item mesh is hardcoded in game source.

Entity textures are currently opaque `64x32` records. Normal external block
textures are `16x16`.

Block-breaking textures are fixed `16x16` PNGs named `destroy_stage_0.png`
through `destroy_stage_9.png` under `textures/environment/`. Either all ten or
none must exist. Alpha values below 128 become fully transparent mask bits;
alpha values from 128 through 255 become fully opaque. The ten stages are
stored in one dedicated record and do not consume normal texture IDs.

## Geometry Inputs

Entity models use Bedrock-style `.geo.json` files with pixel-space `origin`,
`size`, `pivot`, and `[u, v]` cube UVs. The converter changes pixel coordinates
to TinyCraft block units and bakes fixed 32-byte cuboid part records.

Custom block `.geo.json` cubes retain `0..16` block-model coordinates and are
baked into six 20-byte faces per cube. Ordinary full cubes still use the fast
terrain mesher; the model record path is for special geometry.

## Present But Not Compiled Yet

The full directory structure exists, but TCAC v8 does not yet emit records for:

- animation and animation-controller files
- render controllers, attachables, and resource-pack item render descriptions
- particles, sounds, materials, fog, and UI definitions
- localization, root sound/block compatibility files, and flipbook textures
- biomes, features, feature rules, structures, and spawn biome filters
- recipes, trading, dialogue, functions, scripts, cameras, and item catalogs
- generalized entity loot-table execution

Files placed in unsupported categories are left untouched. The converter notes
non-empty unsupported categories instead of silently claiming they were baked.
Step 10 will add the first animation records.

## Output Options

Default source and output:

```powershell
python .\tools\tc_asset_pack.py pack
```

Custom source pack pair and deploy folder:

```powershell
python .\tools\tc_asset_pack.py pack D:\MyPack --game-folder D:\Builds\Tinycraft
```

Explicit output path:

```powershell
python .\tools\tc_asset_pack.py pack D:\MyPack -o D:\Builds\Tinycraft\assets.tca
```

A legacy single JSON manifest can still be passed as a migration aid, but v59
does not ship one and the pack-pair layout is authoritative.

## Runtime Limits

- 64 cache records total
- block IDs `0..15`
- texture IDs `0..31`
- six opaque `64x32` entity texture slots
- eight item mesh records, 128 faces each, 640 faces total
- sixteen block models, 128 faces each, 384 faces total
- eight entity definitions and eight entity models
- sixteen cuboid parts per entity model, 64 parts total
- six active streamed entities backed by 128 world-file entity records
- ten dedicated masked block-breaking stages
- current controllers: `controller_passive_00` and
  `controller_aggressive_00`

## Files

- `tc_asset_pack.py`: TCAC binary record builder and CLI.
- `tc_pack_loader.py`: resource/behavior pack validation and cross-reference
  resolver.
- `texture_ids.json`: compatibility aliases for compiled/runtime texture slots.
- `python_command.txt`: full local command for rebuilding the default cache.
