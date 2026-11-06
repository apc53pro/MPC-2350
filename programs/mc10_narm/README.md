# MC10 NARM compatibility project

## Build 39: native wide scalars and generic numeric runtime

This build requires firmware API 1.17. Compile and upload the main
`MPC-2350_Firmware` sketch using your Arduino IDE settings before launching it.
Build 38 remains available in its own folder for the earlier firmware.

The compiler now supports C `long long`, `unsigned long long` and `double`,
including expressions, storage, conversions, function arguments and returns.
Eligible locals and parameters stay in callee-saved registers across the
function. The firmware provides one generic numeric service, shared by all NARM
programs. Compiler-generated calls obtain its address through the runtime API
table; programs contain no fixed firmware function addresses.

MC10's binary64 adapters use native double arithmetic and conversions on NARM,
with the portable integer implementation retained for host reference tests.
The DCP path defers scheduler preemption for each bounded arithmetic call while
allowing higher-priority interrupts. Strict integer fallbacks handle IEEE edge
cases in flash. There are no Minecraft-specific firmware operations.

The build uses `--legacy-float-literals` to preserve the port's existing
single-precision literals in renderer/UI source. Explicit `double` values in
the numeric adapter retain binary64 precision. Owned assets, saves, RNG order
and the interactive game loop are preserved. The package excludes saves and
options. Merge its `MC10` contents into your existing game directory.

Compiler/CPU execution checks and the full Minecraft compatibility suite pass,
including native movement against the owned Java reference. Device performance
has not yet been measured. See `BUILD_VALIDATION.md` for verification details.

## Build 37: faster terrain preparation

Chunk transitions validate every resident block, then publish the surface
list. Interactive frames capture detailed geometry only for visible blocks.
An ordinary cube inside its chunk can read adjacent block opacity directly.
Chunk edges and blocks with special shapes retain the source visibility rules.

On the staged save, a host run completed the blocking rebuild in 591 steps
instead of 3,320. It submitted the same terrain quad streams in 24
view, quality, and culling combinations. The first view of new blocks may
spend some frame time filling the geometry cache. Device load times remain
to be measured. Build 37 uses the same firmware and assets as Build 36.

## Build 36: faster chunk loading

Biome colors are cached by source lookup index and by resident chunk. Moving
one chunk across reuses the six overlapping chunks' colors. Uniform GenLayer
corners skip redundant random work. NBT byte arrays copy in batches and zlib
still verifies the complete Adler-32 checksum.

On the staged save, the first 3×3 window used one color lookup read compared
with 2,304 in Build 35. The next cardinal crossing used zero, with identical
RGB888 colors throughout both windows. These are host I/O counts.

Use the same NARM API 1.14 firmware and companion assets as Build 35.

## Build 35: firmware cursor

Minecraft uses the regular firmware cursor on the title screen, world list,
pause menu, settings, and inventory. The cursor hides during world loading
and camera look. When a mouse is connected, its position controls hover and
clicks. With the mouse disconnected, arrow keys still move the keyboard
hover position. The firmware draws its cursor when a mouse is connected.

Build 35 uses the same NARM API 1.14 firmware and companion files as Build 34.

## Build 34: relocatable installation

Keep `mc10_narm.narm` and its six data files together in any directory on
the SD card. The directory and the executable can both be renamed. Launch
the executable using its actual path. Keep `saves`, `options.txt`, and
`profiles` beside it if you want to carry over worlds, settings, or logs.

Build 34 requires the NARM API 1.14 firmware in the project sketch. It uses
the generic program path query to find the executable's directory and
storage volume. A previous `/MC10` installation works in place as well.

## Build 33: terminal startup

The main menu reads the owned asset directory in one SD transfer and presents
before the platform self-tests or mouse initialization. Asset load and RGB332
framebuffer errors now show a readable exit screen using the native 640x480
display path. Build 33 uses the existing firmware.

Merge the packaged `MC10` folder into the SD root, preserving `saves` and
`options.txt`. Launch `/MC10/mc10_narm.narm` from the terminal. This package
includes the executable and all six data files.

## Build 31: mouse startup recovery

The mouse build presents the menu before its first mouse call, and limits
mouse queue draining so input cannot starve rendering. The separate
`mc10_narm_build31_keyboard` executable uses the same game and assets
without the mouse API, for recovery and comparison. Both use the Build 29
asset files; replace only `/MC10/mc10_narm.narm` if those are already installed.
The mouse build still requires NARM API 1.13 with the mouse service.

## Build 30: physical mouse input

Requires MPC-2350 firmware with NARM API 1.13 and mouse support, plus
the existing depth-clear service. No new firmware code is needed.
Replace `/MC10/mc10_narm.narm` from this build; Build 29 assets, saves
and settings can stay in place.

- Move/click in the main menu, world list, pause menu and settings.
  Scroll the world list with the wheel, double-click a world to load it,
  and drag settings sliders with the left button.
- In play the mouse is captured for camera look. Sensitivity and Invert Mouse
  apply; the wheel selects hotbar slots. Left click (default Attack binding)
  swings the hand or held item; mining, placement and item use are not added.
- In survival inventory, left/right click and Shift-click use the existing
  pickup, split and quick-transfer rules. Escape and the inventory binding
  still close it. Keyboard pointer, look and click controls remain available.

## Build 29: cameras, daylight and broad static block coverage

`build_mc10.ps1 -Interactive -SkipAssets` creates the separate
`mc10_narm_build29_world_systems` release. This is a **device-test build**, not a
claim of a complete Minecraft port. No firmware was changed for Build 29. Use
the Build 28 depth-clear-capable firmware already compiled through Arduino IDE.

Merge this release's `MC10` directory onto the SD root, preserving your existing
`saves`, `options.txt` and `profiles`. Copy **all seven data/program files**, not just
the `.narm`: the new `sky10.dat`, `blocks10.dat`, `blocktint10.dat` and
`biomecolor10.dat` are required alongside existing `assets.mc10` and `math10.dat`.
There is no runtime PNG decoding and no automatic save or firmware writes.

F5 cycles first-person, rear third-person, front third-person. The eight source
camera rays test block selection bounds. Third-person uses the full original
player model/skin, armor, held items, walk/swing/sneak poses and ordinary world
depth. Inventory now has the shared character preview and source flat/3D block
icons, including slabs, stairs, fences, pistons, chests and dragon eggs.

World time advances on 20 Hz simulation attempts, not rendered frames. Pause,
inventory and chunk loading freeze it. Daylight changes cached terrain lighting
without rebuilding geometry; source sky/biome colors, sunrise/sunset, original
sun/moon textures, eight moon phases and 780 deterministic source star quads are
included. Render geometry, UVs, masks and colors remain adapted to 320x240 RGB332.
Clouds, weather transitions, lightning and source terrain fog are not yet ported.
Loaded rain/thunder flags affect the sky/daylight, but this is not weather RNG.

All vanilla IDs 1..122 now have source registration/material/metadata facts.
Static shape/collision support and source-generated templates cover ordinary
blocks and complex rails, wire, pistons, beds, slabs, fire, stems, panes, vines,
cauldrons, brewing stands, portal frames and dragon eggs. Grass/foliage uses the
original biome/colorizer data, including pine, birch and swamp rules—not a
generic green. Static closed single/double chests and sign board/post models
share world depth. Backface culling remains standard; optional occlusion stays
in Video Settings, default OFF. Optional diagnostics remain F3-controlled.

Registration/static geometry does **not** implement active block callbacks.
Moving piston ID36 still stops explicitly because its physical shape needs
TileEntityPiston state. Sign text, chest contents/opening, spawner contents,
enchantment-table book, animated End portal, redstone simulation, fluids updating,
leaf decay, web/soul-sand/cactus effects and block mutation remain pending.
The debug pending count can therefore be nonzero without rejecting ordinary
static blocks. No invented air/cube replaces a missing dependency.

Generation foundations are source-tested: GenLayer biome/climate graphs,
compact Perlin/octaves, base density, biome surfaces, caves and ravines.
**New chunk generation is not enabled in gameplay.** Structures, population,
generated lighting and persistence are still required; `mc_gen_ready_for_play()`
deliberately returns false. Missing saved chunks still stop safely. This build
does not yet complete that part of the requested milestone.

Memory: decoding and cached geometry share one 1 MiB arena with mutually
exclusive lifetimes, recovering 1,032,192 bytes versus Build 28. A bounded tint
LRU flushes queued draws before replacing texture storage. Geometry/recipe/list
overflow retains a correct slower path, never silently drops surfaces. Exact
image totals and remaining 6 MiB arena space are recorded in `image_audit.json`.
Initial biome/cache preparation can be slow; it yields cooperatively and keeps
the previous loading/world image, without a custom compiling-terrain screen.

Host source comparisons, native raster parity and actual compiled Thumb probes
are included in the source snapshot/audits. They are not RP2350 device testing.
On device, check startup/keyboard, original saved view, F5 near walls/ceilings,
inventory preview/armor/held items, grass/leaves/vines, streaming across existing
chunks, pause/resume and long-running daylight. Report missing-asset messages
verbatim. Original saves remain read-only; inventory/time changes are session-only.

## Build 28: streaming and player presentation foundation

`build_mc10.ps1 -Interactive -SkipAssets` creates the separate
`mc10_narm_build28_player_foundation` folder. Normal menus lead into a read-only
existing world with bounded chunk streaming, session inventory, original-asset
HUD and first-person hand/held items. Backface culling stays on; optional
occlusion remains a Video Settings toggle, default OFF.

This build requires the new generic firmware depth-only clear operation.
Arduino Verify and the ELF audit preserve 62,100 bytes of linker heap, the
15,360-byte SRAM tiles, the 32 KiB NARM stack arena and 6 MiB NARM PSRAM arena.
No static graphics buffer was added; existing renderer kernels, clock and
startup configuration are unchanged. This is not device verification.
Only generic command 0x0009 / `narm_half_clear_depth()` and its feature probe
were added. Minecraft models/rules remain in the NARM program.

Upload the matching resource_development firmware through Arduino IDE, then
merge this release's `MC10` directory onto the SD root. Preserve `saves`,
`options.txt` and `profiles`. Owned converted assets/math data are unchanged;
there is no runtime PNG decoding or automatic firmware upload.

WASD, Space, Shift and arrow-key look remain. E (or the configured binding)
opens inventory; 1..9 select the hotbar; Enter adapts Mouse 1 to visual swing
only. Inventory uses arrows/Enter/Space for pointer/left/right click and
Shift+click for quick transfer. Escape/the inventory binding closes it after
placing a carried stack. Items are never silently dropped. Rearrangements
are session-only: no save is written.

HUD health, food, air, armor and experience reflect loaded state, not invented
survival simulation. Equip, six-tick swing, arm smoothing and hand/item bobbing
follow source updates. View Bobbing applies to this new path; the proven
terrain camera remains unchanged. Ordinary item and audited flat-block sprites
work; GUI cube/slab icons explicitly remain `?`. Crafting, drops, creative
inventory, character preview, special item-use effects, maps, layered potions,
animated compass/clock and enchantment glint remain pending dependencies.

Streaming stages one validated chunk before replacing a coordinate-tagged
slot. Physics and rendering remain blocked until all nine target chunks and
their incremental cache are ready. Missing/corrupt chunks and unported block
types stop with explicit Retry loading / exit, never invented air. A single
staging buffer cannot roll back a whole replaced window: on failure the valid
partial slots remain inaccessible until retry/retarget completes. The current
block registry still covers the previously ported subset; this is not yet an
unrestricted-world port. Only existing McRegion terrain is loaded, not generated.

Both custom "Compiling terrain" screens are removed. Initial cache preparation
retains the original-style Loading level / Building terrain image; subsequent
streaming retains the last presented frame. Cursor Speed now accumulates elapsed
repeat intervals so it works during slow redraws; camera Sensitivity is separate.
F3 only toggles optional diagnostics, not the gameplay HUD/hand or FPS.

## Build 27: normal gameplay, menus and separate cursor speed

Archived `mc10_narm_build27_menu_settings` opens the normal main menu instead of
running an automated benchmark. The optimized ordered tiled renderer and
proven legacy bitmap-font path remain enabled. Use the already installed
Build 26 visibility-culling firmware; this update changes the NARM program
only. No Arduino Verify or firmware upload is required for Build 27.

Backface culling is standard in gameplay, without a video toggle. The new
Video Settings **Occlusion Culling** toggle controls the existing optional
native depth-occlusion path and defaults to OFF. It is a NARM addition, distinct
from the retained original **Advanced OpenGL** setting. Occlusion was slower
in the measured Build 26 saved view, so enabling it is not a guaranteed gain.

Options now has a separate **Cursor Speed** slider. The original **Sensitivity**
slider still controls only in-game camera look using the source formula.
Cursor Speed controls only the keyboard-driven menu pointer. Its default
100% preserves two pixels every 8 ms; the 25% to 200% range uses 32 ms to 4 ms
intervals. Move the pointer with arrows and press Enter to click; hold Enter
and move with arrows to drag a slider. The added settings persist as
`cursorSpeed` and `occlusionCulling` in `/MC10/options.txt`; existing options
files receive defaults when those keys are absent.

The missing option names were caused by clearing the formatted label buffer
while adding its value. Full labels, values and button text are now checked
together. Original Options, Video Settings and Controls slots/order remain;
the two explicit port controls occupy previously empty slots. Pause keeps
source button geometry, Back to game and return-from-Options behavior.

Presentation/port boundaries remain explicit: the pause/options background
is still the opaque owned GUI texture, not Minecraft's translucent world
overlay. Achievements and Statistics remain disabled placeholders. The exit
button says **Exit without saving** because world saving is not implemented.
Panorama, other unported graphical options and other gameplay placeholders
are unchanged. This is not a claim that every Minecraft GUI is complete.

Interactive Build 27 starts with only FPS diagnostics; F3 restores/toggles the
existing optional profiler overlay. No CSV benchmark runs automatically.
Merge the release's `MC10` folder onto the SD root without replacing saves,
settings or profiles. Keep the original converted assets and math table; no
runtime PNG decoding or Minecraft-specific firmware API is introduced.

## Build 26 three-way visibility-culling comparison

`build_mc10.ps1 -CullProfile -SkipAssets` creates the separate
`mc10_narm_build26_visibility_culling` folder and defines `MC10_CULL_PROFILE=1`.
It implies Preparation/Geometry/Stage/Tile/AutoProfile and LegacyFont. This
milestone starts from archived Build 24, not Build 25; no retained/indexed
mesh built-in or service is included. Owned converted assets stay unchanged.

All three measured modes retain optimized Build 24 submission/preparation:

| stage_mode | View suffix | Culling bits | List worker | Enabled |
|---|---|---:|---:|---|
| 0 | baseline | 0 | 2 | Neither feature |
| 1 | backface | 1 | 2 | Backface only |
| 2 | combined | 3 | 258 | Backface plus occlusion |

Every timed mode uses `renderStats(1025)`. This does not use the old 3073
legacy-geometry baseline. The report separates OFF-to-BACKFACE, OFF-to-BOTH
and BACKFACE-to-BOTH, including paired milliseconds, percentages, speedup and
ranges; the last comparison isolates occlusion's marginal cost/benefit.
Same-image percentages are not automatically Build-24-to-Build-26 release gains.

The existing four saved/sky and HUD on/off views now render **72 frames and
60 timed samples**: one discarded warm-up per mode per view, then five measured
triples per view. Six rotating permutations (012,120,201,021,210,102) are selected
by `(view*5+sample)%6`, using zero-based indices. The historical `pair_order`
column stores that permutation ID 0..5 on all three rows in a triple, not a
physical position. Each mode starts a separate cleared frame with the same
camera/input/settings; physics/world ticks remain frozen.

Baseline warm-ups use 1025. Backface-only and combined warm-ups use 1537 with
game rejection diagnostics enabled; each has its own metadata. Game
backface-eligible/rejected counts and native 71..75 (Hi-Z triangles tested,
whole triangles occluded, summary-cell checks, fully-covered cells published,
numeric-bound fallbacks) are discarded warm-up diagnostics only. Backface-only
native occlusion counters must be zero. Timed frames and interactive F3-off
play do not collect optional rejection counters. Culling itself remains active.

Matching firmware must expose visibility protocol `43550001` at statistic 70,
plus preparation/geometry/stage/tile protocols 66/65/34/24, checked before world
loading and collection. Reserved 60..64 and unsupported 67..69 remain zero.
The generic existing textured-list service uses worker bit 0x100 for Hi-Z;
there is no Minecraft-specific firmware API or new depth-query API.

Logs remain `/MC10/profiles/b26_0001.csv` (next free number), with the distinct
77-column schema `mc10_culling_abc_v1`. Use
`python tools/mc10_cull_report.py <log.csv>` (optional `--json`).
The report requires 60 complete samples, all three modes/permutation IDs,
immutable world/camera/pool/settings, matching per-triple clocks, valid timing
accounting and exact depth/validity hashes. HUD-hidden color hashes must match.
Submitted/raster work may decrease; occlusion work can vary with required 8 ms
flush boundaries, so warm-up/timed work counts need not match. Worker timers
overlap, not serially add. Zero occlusion rejections is valid but does not prove
an occlusion gain. The 96 KiB log buffer fits the three-way format; a host
maximum-width numeric fixture (mandatory timed detailed fields kept zero)
verifies the bound and demonstrates why 64 KiB is insufficient.

No front sorting, primitive-order change, texture/lighting reduction or invented
world is used. Backface rejection is restricted to eligible ordinary opaque full
cubes with conservative inside/unknown/near-plane fallbacks. Native occlusion
runs after transformation, clipping and gradient setup, before queuing; only
already completed depth may occlude. The normal saved view stays the
release-to-release reference. A wall-heavy test must use an existing owned-world
viewpoint and separate CSV, not synthesized data or a cherry-picked camera.

Cancellation/error restores camera/HUD, renderer mode and diagnostic mode
without partial logs. Verified CSV results offer Enter to play/Escape to exit.
Enter selects culling bits 3, optimized submission and native diagnostics zero.
F3 disables optional profiling at a completed frame boundary, leaving only FPS,
while input, physics and mandatory yields remain. Play does not repeat the
three-way benchmark or modify its CSV. Older profiler configurations explicitly
disable new culling during automated runs and retain their original formats;
successful play handoff enables the current optimized renderer. Archived
releases remain untouched. Device speedup still needs CSV measurement.

## Build 24 preparation fixes

Build with `-PreparationProfile` to create the separate
`mc10_narm_build24_preparation_fixes` folder. This implies the existing geometry,
stage, tile, auto-profile and legacy-font switches, and additionally defines
`MC10_PREPARATION_PROFILE=1`. Plain `-GeometryProfile` retains Build 23's folder,
identification, protocol, comparison cases and report format.

Build 24 records `/MC10/profiles/b24_0001.csv` (next free number), identifies
itself as `24_PREPARATION_FIXES_LEGACY_FONT`, and retains the exact 77-column
`mc10_geometry_ab_v1` format. The existing `mc10_geometry_report.py` reads it.
Before world loading, Build 24 requires firmware preparation protocol
`renderStat(66)==0x50500001`, logged as `preparation_protocol=50500001`; the
unchanged Build 23 geometry protocol alone is insufficient. Updated report
validation requires this extra metadata for Build 24 while accepting Build 23.
The common fixes are inline native argument marshalling and warm-cache mask
traversal in the proven per-pixel font; neither enables the buffered native
glyph service. Both fixes apply to both benchmark sides.

The paired comparison remains legacy game submission/native preparation
(`renderStats(3073)`, submission mode 0) versus optimized submission/preparation
(`renderStats(1025)`, mode 1), with equal stage instrumentation. The optimized
discarded warm-up uses 1537 for structural counters. This is an internal
same-image comparison, not Build 23 release versus Build 24 release, so its
percentage must not be reported as release-to-release speedup. Actual progress
requires comparing matching optimized samples across the two release CSVs.

The four camera/HUD cases, alternating five pairs, 48 rendered frames, 40
recorded samples, untimed hashes, save-read-only behavior and verified-CSV
Enter-to-play handoff are unchanged. Gameplay explicitly selects optimized
submission with native diagnostics off. F3 still toggles optional profiling
at completed frame boundaries and leaves only FPS when disabled.

MC10 is a standalone NARM reimplementation of the Minecraft 1.0 single-player
client. It does not include, link to, or compile any TinyCraft source. The only
shared dependency is the public NARM runtime API supplied by the firmware.

The installed Minecraft 1.0 client and server sources are used as a behavioral
reference. MC10 code is written independently for NARMCC. Java sources and
game archives are not copied into the project; owned resources are converted
locally into an ignored deployable package.

## Build storage

Each release is kept in its own folder directly under this project, for example
`mc10_narm_build11_terrain_cache/`. The folder contains the deployable `MC10/`
files, its ZIP and build notes. Copy/merge `MC10/` onto the SD root; saved worlds
and options are not included in release packages.

`build_mc10.ps1` automatically archives successful builds in uniquely timestamped
`mc10_narm_build_<timestamp>/` folders. Use `-BuildName
mc10_narm_build13_description` to name a release explicitly. Existing build
folders are never overwritten. `MC10/` at the project root remains the current
working image, not the release archive.

## Current milestone

Build 24 addresses native setup inlining and reduces HUD/submission work
(see "Build 24 preparation fixes" above), with a same-build old/new profiler followed by
interactive play. During play press **F3** to switch profiling off/on at the
next completed frame. Off stops optional timing/counter collection and all
diagnostic text/crosshair drawing, leaving only a one-decimal FPS readout.
The renderer's responsiveness budget and normal input/physics clocks remain
active. This toggle does not run during automated measurement or alter the CSV.

All performance-profiler builds must offer interactive play-testing after the
automated run: once `PROFILE SAVED AND VERIFIED` appears, Enter starts play in
the same loaded world and Escape exits. Gameplay uses the optimized renderer,
restored viewpoint/HUD, normal movement and pause controls, and fresh input and
timing state. The benchmark does not rerun or append gameplay to its CSV. Saves
remain read-only. Startup-isolation diagnostics are not performance profilers.

### Previous Build 17

Build 17 is the **full Build 15 profiler with legacy pixel-font submission**.
It runs normal world bootstrap, math/player initialization, terrain cache
compilation and all four HUD-on/off cases (24 frames including warm-ups,
20 recorded samples). There is no Build 16 checkpoint logging or early stop.
Build 15's glyph-cache preparation and fixed pools remain; only font drawing
uses the old pixel path. The font cache is intentionally retained to reduce
the number of changed startup operations. Chunk loading and renderer behavior,
assets, settings, camera sequence and post-measurement CSV verification remain
the same. Identifying labels and output filenames change to avoid confusion.

Logs use the next free `/MC10/profiles/b17_0001.csv`, schema
`mc10_autoprofile_v2`, build `17_LEGACY_FONT_HUD_AB`, and explicitly identify
the legacy font backend. The old font is expected to have expensive HUD
drawing; this is a stability comparison, not a claimed speedup or crash fix.
As with any separately compiled binary, addresses/layout and timing change,
so a successful run alone does not prove the glyph service caused the crash.

Merge this build's `MC10/` into the SD root, preserving saves and options.
No firmware update is required. Wait for `PROFILE SAVED AND VERIFIED`, then
Escape exits (Enter starts gameplay, also using legacy text). Supply the newest
`b17_*.csv`. Build with `build_mc10.ps1 -SkipAssets -AutoProfile -LegacyFont
-BuildName mc10_narm_build17_legacy_font_profile`.

### Build 16 (startup-only diagnostic)

Build 16 is a **startup-isolation diagnostic**, not a performance update or a
confirmed crash fix. Build 15 has produced intermittent whole-RP2350 lockups
between loading chunks 2 and 9; do not use it as a known-good release.

Build 16 uses the original per-pixel font path throughout the run, skips glyph
cache preparation, and never submits the new native glyph command. It boots
directly into the same read-only world selection/bootstrap and stops after
checking all nine resident chunks. It does not initialize player physics,
compile/render terrain, benchmark, or enter gameplay. The benchmark/renderer
functions and fixed pools are retained to avoid intentionally shrinking the
Build 15 working set. Firmware, owned assets and terrain algorithms are unchanged.

Before startup it creates the next unused `/MC10/profiles/s16_0001.log`.
Each checkpoint is short-write checked, closed by the firmware file service,
size checked and read back before continuing. Records include sequence, chunk
number, chunk coordinates, status/byte count and stage: loading-screen entry,
region headers, payload read, inflate, NBT parse, array copying, chunk return
and cross-chunk access validation. Existing reports, saves and options are not
overwritten. Logging errors stop startup; a failed run may leave a partial log.
If log creation fails before fonts initialize, the program exits without a UI.

Checkpoint overlays are drawn *after* the corresponding log record verifies.
The last surviving line bounds progress; it does not identify a faulting
instruction or guarantee durability through sudden power loss. Extra SD I/O
and legacy text make this build slower and alter scheduling. Success narrows
the problem but does not prove the glyph API caused Build 15's lockup.

Copy the package's `MC10/` folder onto the SD root without replacing saves or
options; no firmware update is required. On success the screen reports
`STARTUP COMPLETE - NO 3D RENDER`; Enter/Escape exits. If it locks up, recover
as needed and supply the newest `s16_*.log`, even if incomplete. Do not repeatedly
reproduce a whole-device lockup. Build with:
`build_mc10.ps1 -SkipAssets -StartupDiagnostic -BuildName mc10_narm_build16_startup_isolation`.
Host/compiled-ARM checks are not device verification.

### Previous Build 15 (hardware regression under investigation)

Build 15 replaces flat-font per-pixel native calls with cached exact 8x8 masks
through the existing `narm_half_glyph` service. Metrics, RGB332 color/shadow,
glyph orientation, drawing order and clipping are unchanged. Empty/off-screen
glyphs issue no call; service errors use the original pixel path and increment
the logged fallback counter. The menu/options/world text path shares this
optimization; transformed splash rendering remains unchanged.

The automatic test now samples four cases: `saved_hud_on`, `saved_hud_off`,
`sky_hud_on`, `sky_hud_off`, each with one warm-up and five measured frames.
HUD-off suppresses diagnostic text, footer and crosshair only; terrain,
presentation, CSV timing and cancel polling stay active. Player/camera/HUD mode
are restored on completion/cancellation. The renderer and native rasterizer
are otherwise unchanged. This directly measures the optimized overlay cost
without assuming that subtracting an old HUD timing predicts real performance.

New logs use `/MC10/profiles/b15_0001.csv` (next free sequence), 27-column schema
`mc10_autoprofile_v2`, twenty raw samples and independent summaries per case.
The report reader remains compatible with Build 14 logs and checks that paired
HUD-on/off cases keep identical face/visited/skipped counts. Build with
`build_mc10.ps1 -SkipAssets -AutoProfile -BuildName
mc10_narm_build15_buffered_hud_profile`. No further firmware update is required;
the existing cap-removed loader/glyph service is used. Hardware speed remains
unverified until the new CSV is collected.

Build 14 is a dedicated **automatic render-only benchmark** of the Build 13
renderer below. It skips menu interaction, selects the most recently played
valid world, and requires nine valid resident chunks. Optional
`/MC10/profile_world.txt` contains one exact save-folder name to pin the world.
Options and owned assets load normally; effective and inactive settings are
recorded separately. No player or world tick, camera input, save write or options
write executes during measurement. Source player initialization still runs so
the saved viewpoint matches normal world entry.

Each saved-view and straight-up view (pitch -90 degrees, same yaw/position)
gets one discarded warm-up and five measured frames. Samples are buffered.
The original camera is restored even on Escape/focus-loss cancellation; aborted
runs produce no log. Successful runs then fingerprint the nine chunks, format
raw and per-column min/median/max results, and write/readback-verify a new
`/MC10/profiles/b14_0001.csv` (next free sequence on later runs). Existing logs,
saves and options are not replaced. The completion screen displays the path;
Enter starts normal gameplay, Escape exits.

The CSV is uniform 27-column `mc10_autoprofile_v1`: metadata rows use `key`/
`value`; sample/min/median/max rows use the numeric columns. Timings are exact
microseconds, and summaries are independent per-column statistics, not one
physical frame whose medians necessarily sum. `HUD`/`Present` are included in
`UI`, not additional categories. Frames include setup, terrain, diagnostics,
presentation and between-batch yields; inter-frame work, initialization, chunk
fingerprinting and SD logging are excluded. Tick time/count is zero. These
results are not directly comparable to a full gameplay-loop Frame ms with
physics enabled. OS/background-process load can still affect wall-clock calls.

Run `python tools/mc10_profile_report.py <SD-log.csv>` to validate and summarize
an accessible log (`--json` for machine-readable output). No automatic PC upload
is performed. The device's SD write/readback still needs hardware verification.
Build with `build_mc10.ps1 -SkipAssets -AutoProfile -BuildName
mc10_narm_build14_auto_profile`; omit `-AutoProfile` for the normal menu/game
entry point. Existing Build 13 remains archived separately.

Build 13 adds spatial-section rejection and exact cached geometry to
Build 12's profiling and Build 11's compact terrain cache for the
**Interactive First-Person World Loop: Movement in the Loaded World**. It retains
the device-confirmed projection, renderer, assets and player behavior.

Eight-block sections are indexed as contiguous runs in the original traversal
order, never sorted or regrouped. Conservative section bounds include the old
per-block culling margin and a rounding guard. Sections wholly rejected by the
old test are skipped at run granularity. Per-block culling remains unchanged.
The staged sky-view fixture visits 752 entries and skips 6,890 of 7,642; this is
a host operation count, not a measured hardware speedup.

A bounded pool stores 12,288 quads as the exact original 20 float vertex/UV
values and a light-key/mask/conditional-grass-overlay descriptor. The staged
world caches 6,287 complete blocks; remaining blocks use the original geometry
code. Pool exhaustion rolls back the entire block, never dropping part of it.
Gamma is resolved at replay, and Fancy grass toggles the recorded overlay;
neither changes geometry precision or requires recapturing the world. The
cache is generated through the existing source-derived shape/texture routines,
not a replacement block renderer. Native quad submissions remain individual;
firmware batching and rasterizer changes are not part of this build.

The world HUD reports a coherent snapshot of the previous completed frame,
including its face/batch counts, in milliseconds with 0.1 ms precision:

- `Gen`: terrain traversal, culling and face generation, excluding drawing calls.
- `Draw`: native textured-quad call intervals, including validation, transform,
  clipping and rasterization; submitted faces need not produce visible pixels.
- `Input`: input/event/timer processing, excluding player ticks.
- `Tick`: player ticks and their movement-binding queries.
- `Yield`: explicit cooperative-yield call intervals between frames/batches.
- `Setup`: camera, clear and gamma-lookup preparation.
- `UI`: diagnostic HUD, crosshair and presentation call intervals.
- `HUD` / `Present`: the two components of `UI`, not additional frame categories.
- `Other`: remaining wall time, including profiling/accounting overhead.

These are wall-clock intervals, not exclusive CPU times. Implicit preemption
inside a native call or tick is charged to that interval; `Yield` is not a full
scheduler/preemption counter. Nested drawing/tick time is subtracted from
`Gen`/`Input`, so categories sum to the frame total before display rounding.
After the first frame, totals include the preceding post-present yield and input
pump. Pausing discards the partial sample; Options time is excluded. The first
frame shows `collecting first frame`, and subsequent overlays are one frame
behind, allowing UI/presentation cost to be measured rather than omitted.
Profiling adds timer calls and HUD work. `Visited`/`skipped` show the previous
frame's section traversal, and `Geometry`/`cached blocks` show pool usage.
Compare a slow terrain view and a sky view, including the new HUD/Present split.
Player input/ticks run between bounded rendering batches; camera transforms
stay fixed for each frame. Frame time is measured, with no FPS promise.
Host and compiled ARM verification pass; device verification remains pending.

The cache is compiled once per world entry and retains only exposed standard
blocks plus special-render/unsupported entries, in the original traversal
order. Each eight-byte record stores block location/ID/metadata, six visibility
bits and six four-bit saved-light levels. Standard neighbor/light checks are
not repeated each frame. Gamma lookup is refreshed when changed; Fancy grass
uses the original overlay geometry. Shapes and UVs are neither simplified
nor quantized. The staged 3x3 world has 7,642 entries instead of 162,604 non-air
blocks (294,912 scanned positions). Overflow uses a non-dropping raw scan.

Time-based batches replace the eight-block limit, and keyboard binding
translation is cached until a binding changes. The HUD reports surface-cache
entries and batch count alongside full wall-clock frame time. Startup's
`COMPILING RESIDENT TERRAIN...` phase is a one-time cost, not a frame rate.
Build 13 uses more than the former 4 MiB data cap and requires the updated
loader with that cap removed (already built separately). No new firmware
feature is needed. The actual shared native pool remains 6 MiB, including
text/data/stack for all live native processes; other live programs reduce the
available space. The final image's allocation is checked against that pool.

The current image establishes the low-level compatibility rules needed by
later world and gameplay code and consumes the original owned 1.0 resources:

- Minecraft 1.0 main-menu layout at the adapted 320x240 RGB332 target;
- original bitmap font metrics/glyphs, GUI button textures, both logo branches,
  translated labels, splash selection/pulse and footer;
- an explicit black background placeholder while the expensive panorama is
  disabled for this milestone;
- a smaller virtual mouse whose coordinates feed the reproduced hover/hit-test
  path, with two-pixel motion sampled every 8 ms while an arrow is held;
- the Minecraft 1.0 Options, Video Settings, and Controls hierarchy, including
  the original screen layouts, labels, sliders, value cycles, 13 key bindings,
  duplicate-key warning and Done/Escape navigation;
- converted option labels and slider pixels are preferred; exact source-label
  and original cached-GUI fallbacks keep Options compatible with the preceding
  asset-pack revision instead of silently refusing to open;
- `GameSettings` defaults plus load/save compatibility with
  `/MC10/options.txt`; NARM USB HID keys are translated at the controls-screen
  boundary while the file retains Minecraft 1.0 LWJGL key codes;
- Java-compatible 48-bit `Random` state transition without 64-bit integers;
- exact bounded `nextInt` rejection behavior;
- explicit two-word Java `long` arithmetic and `Random.nextLong` behavior;
- exact `NibbleArray` indexing and packing over NARM-owned storage;
- exact read-only `Chunk` methods that do not yet require `World` or `Block`;
- exact `Chunk.getRandomWithSeed` integer overflow and Java-long behavior;
- the complete Minecraft 1.0 NBT tag family (IDs 0 through 10), including
  compounds, homogeneous lists, deep copying, equality and typed accessors;
- Java `DataInput`/`DataOutput` big-endian encoding and modified UTF-8;
- gzip loading with stored, fixed-Huffman and dynamic-Huffman DEFLATE blocks,
  CRC/size validation, and Java-readable gzip output;
- `CompressedStreamTools`-style compressed, uncompressed and atomic file I/O;
- the Minecraft 1.0 Select World screen, reached from Singleplayer, with the
  original list geometry, translated labels, button states and virtual-mouse
  hit testing;
- `SaveConverterMcRegion`-style discovery of every valid world directory under
  `/MC10/saves`, including `level.dat_old` fallback and last-played ordering;
- `WorldInfo` loading for seed, game type, map features, spawn, time, last
  played, size, level name, save version, weather, hardcore and player
  dimension;
- the Minecraft 1.0 `RegionFileCache`/`RegionFile` read path for `.mcr` files,
  including the two 4 KiB header tables, sector bounds and timestamps;
- gzip and zlib chunk-record decompression with integrity validation;
- the `ThreadedChunkLoader`/`ChunkLoader` arbitrary-chunk path, loading `Blocks`,
  `Data`, `SkyLight`, `BlockLight`, `HeightMap`, `LastUpdate` and
  `TerrainPopulated` into the legacy 128-high chunk representation;
- source-equivalent correction of mismatched stored chunk coordinates, plus
  explicit counts for entity, tile-entity and pending-tick lists;
- exact-bit restoration of the saved player's `Pos`, `Motion`, `Rotation`,
  dimension and core `Entity` fields, including the source motion clamp;
- an overworld `WorldProvider` bootstrap and its 16-entry brightness table;
- a bounded 3x3 resident chunk-provider window centered on the saved player,
  with spawn used only when the `Player` compound is absent;
- source-shaped read-only world access for block ID, metadata, height,
  sky-light and block-light across positive and negative chunk boundaries;
- explicit missing/invalid chunk states and a residency diagnostic instead of
  treating unavailable terrain as successfully loaded air;
- source-derived `Material` flags and a 256-slot `Block` registry containing
  every non-air block ID found in the staged 3x3 resident window;
- source texture-selection rules, render types, opacity, normal-block state,
  light values and bounds for that resident block set;
- continuous 3D terrain frames from the moving player, using source
  `RenderBlocks` face/UV order;
- source-shaped standard blocks, crossed plants, crops, stairs, fences,
  torches, fluids, doors, ladders and panes;
- saved sky/block light, source directional shading and gamma through sixteen
  RGB332 terrain atlases, built with the corrected NARM call-argument backend;
- grass-block top, tall-grass/fern tint and fancy-grass side overlay from the owned
  `misc/grasscolor.png`, using `BlockGrass.getBlockColor()`'s source-defined
  temperature 0.5/rainfall 1.0 fallback while `WorldChunkManager` is pending;
- the device-confirmed fixed projection; dynamic FOV and render-distance
  projection changes remain isolated from this static-frame renderer;
- runtime use of the host-converted `terrain.png` RGB332 pixels and one-bit
  transparency mask, with no PNG decoder in the NARM program;
- deterministic on-device startup self-tests;
- host conversion of every PNG/text/language resource from `1.0.jar`;
- RGB332 textures and one-bit transparency masks in `assets.mc10`;
- a source-digest manifest for every packaged resource;
- the original Minecraft 1.0 GUI, font and logo resources on the NARM main
  menu.

The menu renderer keeps its static GUI images resident after initialization
and redraws at the source's 20 Hz update cadence. The owned panorama resources
remain in the complete asset pack but are not loaded or rendered.

PNG decoding is strictly a host-build operation. `mc10_asset_pack.py` converts
the owned 1.0 images, including `terrain.png`, to RGB332 records in
`assets.mc10`; the NARM program never contains or invokes a PNG decoder.

NBT objects use bounded global pools because NARM deliberately has no dynamic
allocator. The present limits are 2,048 tags, 4,096 child links, 32,768 UTF-16
code units, 256 KiB of byte-array payload and 384 KiB input/output buffers.
Capacity exhaustion is reported explicitly instead of pretending that a save
loaded successfully. Java `long`, `float`, and `double` values are preserved by
their exact serialized bits; NARM code does not round-trip them through its
32-bit floating-point type.

The Select World list is bounded to 32 saves and folder/display names are
bounded to 63/79 bytes. These are explicit fixed-memory platform limits; valid
entries within those limits retain Minecraft's metadata and ordering rules.

It is intentionally not a playable game yet. Gameplay code is added only when
its complete source dependencies can be reproduced; missing systems remain
explicit placeholders.

## Reference tree

The local reference used for compatibility work is:

```text
C:/Users/enzob/AppData/Roaming/.minecraft/versions/1.0/
```

The client has 930 Java source files and the server has 660. Client rendering
and UI behavior come from the client tree; authoritative world, block, entity,
and save behavior should be cross-checked against both trees.

## Build

From PowerShell in this directory:

```powershell
.\build_mc10.ps1
```

The deployable files are written to:

```text
MC10/mc10_narm.narm
MC10/assets.mc10
MC10/math10.dat
MC10/assets_manifest.json
MC10/source_manifest.json
```

Copy the complete `MC10` directory to the root of the SD card. The asset pack
is generated locally from the user's owned Minecraft 1.0 installation and is
intentionally ignored by source control.

World folders live under `MC10/saves/<folder>/`. The current deployable folder
includes the user's owned `New World` metadata and the region file containing
its spawn and current player neighborhood, so the loader can be exercised
immediately.

Use `-SkipAssets` only after a valid package has already been generated. The
normal build fails if the complete resources (including `title/mclogo.png`)
cannot be found in the JAR/resource tree.

Every build also verifies the audited decompiled classes before compilation.
Java/Javac and GCC are host build/test dependencies. `math10.dat` contains the
original `MathHelper` sine table exported from the owned Java source; runtime
loading validates its header and CRC. No Java or PNG decoder runs on NARM.
See `SOURCE_TRACEABILITY.md` for the exact implemented boundaries and declared
placeholders. The resident block set is complete for the bundled 3x3 chunk
window; unencountered block classes remain explicitly absent rather than being
given approximate behavior.

The main menu, Options hierarchy, and Select World destination are ported.
Options are persisted in `/MC10/options.txt`. Music, sound, GUI scale,
framerate limit, graphics, ambient occlusion, clouds, particles, anaglyph and
view bobbing retain their Minecraft state and presentation, but settings whose
consumer system has not been ported yet deliberately have no downstream
effect. Gamma feeds the terrain light-atlas adapter. Sensitivity, inverted
mouse, movement bindings, gamma and Fancy grass affect the current world loop.
FOV and render distance remain stored pending separate projection verification
and do not alter the device-confirmed projection in this build.

Selecting a world
loads its real `WorldInfo` and player state, bootstraps the overworld provider,
loads a 3x3 McRegion-backed resident window around the player, and renders the
continuous terrain frames. The renderer covers standard blocks, crossed
plants, crops, stairs, fences, torches, fluids, doors, ladders and panes. The
sixteen-level RGB332 light atlas supplies saved-light shading and gamma. ARM
execution of fix8 demonstrated that the actual distortion was caused by
NARMCC overwriting the third argument register while addressing a large stack
frame. The compiler is now corrected; fix9's camera matrix and all six ordinary
block faces match the compiled, device-confirmed fix4 geometry. Ambient
occlusion, seed-driven biome tint variation, sky/cloud drawing, fog and animated
texture behavior remain explicit pending work. Entity object
construction, terrain generation, mutation and world ticking also remain
subsequent milestones. Player movement does tick at 20 Hz. Create, Rename and
Delete are explicit non-mutating placeholders.
Calendar-specific splash overrides await a NARM calendar or RTC API.
Multiplayer is disabled because the current standalone runtime has no
Minecraft `Session`, matching the source's null-session branch.

## Controls

- Arrow keys: move the virtual mouse pointer by two pixels every 8 ms while held
- Enter: feed mouse button 0 at the pointer hotspot
- Escape: return from Select World
- Enter twice within 250 ms on a selected row: source-equivalent double-click
- Enter on the scrollbar: page by 24 pixels (virtual-mouse adaptation)
- Options: Enter activates the hovered button or slider; Escape returns one
  screen, and selecting a Controls binding makes the next pressed key its value
- In a world: WASD movement, Space jump, Left Shift sneak (or configured
  Controls bindings); double-tap forward to sprint; in Creative, double-tap
  jump to toggle flight, then jump/sneak to ascend/descend
- In a world: arrows feed relative virtual-mouse motion (500 counts/sec);
  source sensitivity/inversion/pitch limits apply; this is keyboard adaptation
- Escape: pause/resume the world; the paused menu uses the menu pointer and
  Enter, and Options returns to the same paused player
- Exit without saving: return to Select World; no world/player file is written
- F1: open the NARM diagnostic screen
- Quit Game: exit the program

## Compatibility policy

"Same result" means matching observable Minecraft 1.0 rules and deterministic
behavior wherever the platform permits it. Deliberate platform adaptations are
kept behind the presentation/input boundary: 320x240 output, RGB332 colors,
keyboard-only controls, and NARM storage/audio services.

Player position, velocity and bounding boxes use a tested integer binary64
adapter, not float approximations. Source collision order, step-up/sneak-edge
handling, jump delay, drag/gravity, water/lava currents and ladders are ported
for the resident block set. The source Java oracle passes 602 bit-exact ticks;
selected trajectories also pass actual compiled NARM ARM execution. The loop
test covers bindings, timer catch-up, pause/options, held-key release gating,
focus/close and boundary pauses. Optional ARM tests require Python Unicorn:
`python tests/test_player.py --arm-compiler <narmcc.exe>`.

`test_render_cache.py` compares complete direct/cached geometry streams across
63 camera/position/gamma/Fancy combinations, independent optimization bypasses,
tiny geometry pools and forced surface overflow. Its original 15 views also
match the archived Build 10 renderer bit for bit. Actual ARM checks cover
the packed cache decoder, metadata, exact quad capture/replay, live light lookup,
Fancy overlays, masks, six-face geometry and flag cleanup.
No measured device speedup is claimed until the new image is tested.

## Build 22 draw-stage profiler

`build_mc10.ps1 -StageProfile -SkipAssets` selects the separate
`mc10_narm_build22_draw_stage_profile` build folder. It requires the matching
draw-stage firmware (protocol `44530001`) and preserves the Build 21
`-TileProfile` format and immediate-versus-tiled comparison unchanged.

Build 22 compares the optimized synchronous tiled renderer against itself:
`renderStats(1)` is the uninstrumented baseline and `renderStats(1025)` adds
stage timers and list-call counts. Five pairs alternate order for each saved
view/straight-up view with the HUD on/off. One discarded warm-up per mode and
view precedes the 40 measured frames. Instrumented warm-ups use flags `1537`
to count detailed traffic/depth operations; these warm-up timings are not
performance samples. Job boundaries/traffic in the instrumented warm-up may
differ because instrumentation can change when the 8 ms generation yield fires.

The unique, verified log is `/MC10/profiles/b22_0001.csv` (or the next unused
number), schema `mc10_draw_stages_ab_v1`, 77 columns. Run
`python tools/mc10_stage_report.py <log.csv>` to validate the frame partition,
paired workload/clock identity, exact depth hashes and HUD-off color hashes,
timer nesting and discarded traffic counters. The report shows paired observer
overhead separately from the measured stage breakdown; this build does not
claim a renderer speedup. Validation, exclusive triangle setup and tile flush
are serial parts of native-service time. Core 0 and Core 1 load/raster/commit
times overlap; their active times must not be added together or added to native
service time. Join wait is already inside tile-flush time.

After the CSV is saved and read back byte-for-byte, Enter starts normal
interactive play with the optimized renderer, restored viewpoint/HUD, normal
controls and native diagnostics disabled. The usual NARM profiling overlay is
initially on; F3 turns its logic and drawing off, leaving FPS only, and F3 again
restores it with a fresh sample. Escape opens the pause menu. Play does not rewrite the
recorded log or modify saves, and the profiler's A/B alternation does not
continue during gameplay.

## Build 23 geometry optimization comparison

`build_mc10.ps1 -GeometryProfile -SkipAssets` selects the separate
`mc10_narm_build23_geometry_optimization` folder. Matching firmware must expose
geometry protocol `47500001` at render statistic 65, in addition to the existing
tile and draw-stage protocols. Build 21 `-TileProfile` and Build 22
`-StageProfile` retain their original formats and comparisons.

Both Build 23 paths use the same ordered, synchronous dual-core tiled
rasterizer and identical stage timers. The baseline uses original game-side
submission plus native geometry preparation (`renderStats(3073)`); the candidate
uses optimized submission/preparation (`renderStats(1025)`). One discarded
warm-up per mode/view precedes five alternating pairs for each saved/straight-up
view and HUD on/off combination: 48 total frames, 40 measured samples. Only the
discarded optimized warm-ups enable detailed counters (`renderStats(1537)`).
The image, camera, quality settings, texture/light/depth semantics and drawing
order remain unchanged. List, batch and tile-job counts may legitimately change.

CSV is saved and verified at `/MC10/profiles/b23_0001.csv` (or the next unused
number), with distinct schema `mc10_geometry_ab_v1` and 77 columns. The retained
`stage_mode` column means geometry baseline 0 / optimized 1 in this schema;
it no longer means instrumentation off/on. Analyze it with
`python tools/mc10_geometry_report.py <log.csv>` (optional `--json`). The report
rejects changed workloads/clocks, depth hashes and HUD-off color hashes, checks
stage accounting, and reports paired frame, game-generation, native-validation,
triangle-setup and tile-flush speedups/deltas. Warm-up timings are never used as
performance results. Worker times overlap and join wait is nested inside flush.

After verification, Enter starts play with both optimized paths explicitly
selected and native diagnostics disabled. The restored view/HUD, normal controls,
read-only saves and recorded CSV are preserved. F3 disables optional profiler
logic/text and leaves only FPS; pressing it again restores profiling. There is
no A/B alternation or detailed benchmark instrumentation during gameplay.

The loaded 3x3 window does not stream. A missing collision/fluid neighbor,
unported block or exhausted fixed pool rolls back the entire player tick and
pauses with an explicit diagnostic. Resume and move back into available data;
there is no invisible replacement floor or fabricated terrain. Non-finite
player NBT numbers are explicitly unsupported and rejected before movement.

Pending gameplay includes damage/death progression, hunger updates, potion
effects, block/item interaction, inventory/HUD, moving entities, world ticks,
streaming and saving. Achievements/Statistics are disabled placeholders. The
pause backdrop uses the existing owned-texture GUI background; the original
translucent world overlay and saving indicator are not claimed. Noise/biome
generation remains unported even though movement's binary64 foundation exists.

# Build 38: general compiler improvements

This interactive build uses the general NARMCC Cortex-M33 code-generation pass.
It retains Build 37's deferred terrain compilation and executable-relative
asset paths. No firmware change or upload is required for Build 38.

The compiler keeps eligible float expressions in caller-saved FPU registers,
folds constant array offsets, uses scaled addressing and recognizes safe bit
extractions. MC10's two-word Java-long adapters now call generic `UMULL` and
modulo-2^64 intrinsics; NBT byte-array copying uses the generic native `memcpy`
helper. Source rounding, Java-long wraparound, random consumption, original
assets, and save data are preserved. The binary64 adapter remains binary64.

The native helpers and automatic optimizations apply to any NARM program.
The compiler execution tests and Minecraft compatibility checks pass. Device
frame and loading times still need measurement; instruction-count reductions
in isolated tests are not an FPS claim.

Use `MC10/mc10_narm.narm` in the build folder, merging its packaged `MC10`
contents into the existing game directory. Retain your own `saves` and options.
Run it from the terminal as before. The archive contains no saves or settings.
