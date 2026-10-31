# TinyCraft v59 — NARM development branch

This is the native NARM continuation of TinyCraft v59 for the RP2350 GP
Computer. The game logic and TCAC v8 content are taken from the latest complete
v59 tree; the program now compiles directly to Armv8-M Thumb-2 with NARMCC.

The included deploy output is `Tinycraft/tinycraft_v59.narm`. The old VMCC
`.bin` is intentionally not part of this branch.

## Branch status

- NARMCC `-O3` build succeeds.
- The generated image requires NARM firmware API 1.9 or newer.
- Rendering uses the firmware-owned 320×240 RGB332/depth buffers.
- Persistent content uses SD-card paths rooted at `/Tinycraft`.
- `Tinycraft/assets.tca` is reproducible from the editable packs and matches the
  v59 TCAC v8 cache byte-for-byte.
- Render distances 1–3 now warm the complete visible 8-block mesh window before
  gameplay instead of warming only the current camera frustum.
- Gameplay streaming uses a real-time work budget with guaranteed low-FPS
  progress, plus an interval-limited one-ring background prefetch.
- Cache movement and render-distance changes retain overlapping terrain meshes,
  procedural column results, and natural-height metadata.
- The terrain mesh pool holds 24,576 rectangles. Allocation failure leaves a
  section unbuilt and retryable instead of accepting a partial mesh.
- The port is compile- and image-validated. Hardware gameplay validation is
  still required after copying the deploy directory to the SD card.

## Layout

```text
tinycraft/
  README.md                  NARM build, deployment, and port notes
  MIGRATION_NOTES.md         VMCC-to-NARM compatibility decisions
  build_tinycraft.ps1        rebuild assets, validate them, then compile NARM
  tinycraft_v59.tc           ordered single-translation-unit entry
  narm.tch                   matching public NARM runtime surface
  narm_compat.tch            small v59 compatibility layer
  src/                       editable Tiny-C game modules
  assets/                    editable resource/behavior packs
  tools/                     TCAC v8 converter and validator
  Tinycraft/                 copy this directory to the SD-card root
    tinycraft_v59.narm
    assets.tca
```

`config.txt` and `*.tcw` world files are created at runtime inside the SD
card's `/Tinycraft` directory. They are not development inputs and should not be
committed as generated test state.

## Build

From this directory in PowerShell:

```powershell
.\build_tinycraft.ps1
```

That command rebuilds and validates `Tinycraft/assets.tca`, then compiles the
program with the project's `Compiler_For_NARM/narmcc.exe`. To rebuild only the
native image:

```powershell
.\build_tinycraft.ps1 -SkipAssets
```

An alternate compiler or Python launcher can be supplied explicitly:

```powershell
.\build_tinycraft.ps1 -Compiler D:\sdk\narmcc.exe -Python py
```

Equivalent NARMCC command:

```text
narmcc tinycraft_v59.tc -I . -O3 --default-mode foreground --name "TinyCraft v59" --stack-bytes 32768 --heap-bytes 4096 -o Tinycraft/tinycraft_v59.narm
```

NARMCC compiles one entry source file. `tinycraft_v59.tc` includes all files in
`src/` in dependency order; keep that order unless the compiler gains a
separate-object linker.

## Deploy and launch

Copy the complete local `Tinycraft/` directory to the root of the SD card so
the device contains:

```text
/Tinycraft/tinycraft_v59.narm
/Tinycraft/assets.tca
```

Launch it with the firmware command interface or FILEMGR, for example:

```text
SELECT_PROG_SD "/Tinycraft/tinycraft_v59.narm"
```

Do not place only the `.narm` file on the card: startup requires the adjacent
TCAC cache at the fixed path `/Tinycraft/assets.tca`.

## NARM port details

`narm_compat.tch` intentionally contains only compatibility that v59 needs:

- `millis`, `rand32`, `sinf`, and `cosf` map to stable NARM services;
- legacy PS/2 Set-2 `key(...)` polls `narm_ps2_key_down` and clamps service
  errors to “not pressed”;
- array-sized `sdFileRead` and `sdFileWrite` calls map to explicit-length NARM
  file operations;
- `sdDirEntry` supplies the output capacity and pointer results required by the
  native API.

NARM file operations are SD-root-relative; they do not inherit the launched
program's directory. For that reason, `assets.tca`, `config.txt`, directory
enumeration, and generated `.tcw` paths are explicitly rooted at `/Tinycraft`.

## Terrain streaming and diagnostics

Terrain still uses retained greedy 8×8×8 section meshes, but the NARM branch no
longer treats a low measured FPS as permission to stop streaming indefinitely.
Each completed section is timed. Expensive builds space subsequent work in real
milliseconds, while visible missing terrain is guaranteed another attempt at
least every 200 ms. Direct-edit mesh nodes are unlinked and recycled immediately;
greedy edit cleanup runs only when the visible stream is empty and FPS has
headroom.

The render cache is aligned to eight-block mesh boundaries. When it moves, only
outgoing sections are discarded; overlapping meshes are repacked to the new
cache-local coordinates. Natural column data packs height, surface, soil, tree,
and trunk decisions into a lazy 128-stride cache shared by height discovery and
section generation.

Press `T` repeatedly to cycle through FPS, coordinates/light, firmware renderer
statistics, and streaming statistics. The streaming page uses these labels:

- `M` missing visible sections, `V` visible sections required, `Q` queued work
- `B` completed builds, `U` mesh slots used, `D` dead mesh slots
- `A` allocation failures, `I` incomplete/retried builds, `T` last build time
- `F` forced low-FPS builds, `R` full resets, `S` rolling shifts
- `P` sections preserved by the last shift, `H` column-cache hits, `C` misses

## Asset workflow

Human-readable content lives under sibling resource and behavior packs:

```text
assets/
  resource_pack/
  behavior_pack/
```

The converter validates cross-pack references, converts supported textures to
RGB332, bakes item/block/entity geometry, and writes the compact TCAC v8 file.
Run it directly when needed:

```powershell
python .\tools\tc_asset_pack.py pack .\assets -o .\Tinycraft\assets.tca
python .\tools\tc_asset_pack.py inspect .\Tinycraft\assets.tca
```

See `tools/README.md` and `assets/README.md` for the pack schema, compiled
reference chains, unsupported-but-reserved categories, and current limits.

## Current runtime limits

- 64 TCAC records; block IDs 0–15 and texture IDs 0–31
- six opaque 64×32 entity texture slots
- eight item meshes, sixteen custom block models, and eight entity models
- six active streamed entities backed by 128 world-file entity records
- ten dedicated masked block-breaking stages
- controllers currently implemented: `controller_passive_00` and
  `controller_aggressive_00`
- generalized entity loot execution, projectile arrows, creeper explosions,
  armor/status effects, and several reserved pack categories remain future work

## Development cautions

- Keep the local `narm.tch` synchronized with the compiler/runtime version used
  to build the firmware.
- Keep the deploy directory name `Tinycraft` unless all fixed runtime paths are
  changed together.
- Preserve TCAC record IDs and world-file compatibility when extending content.
- Rebuild both artifacts after source or pack changes; a successful NARM compile
  does not validate the asset graph, and a valid asset cache does not validate
  the native program.
