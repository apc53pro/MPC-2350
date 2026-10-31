# Minecraft 1.0 source traceability

The decompiled Java tree is authoritative. NARM platform glue is kept separate
from reproduced game behavior, and incomplete dependencies are identified as
placeholders rather than replaced with approximate mechanics.

The normal build verifies the SHA-256 of each audited reference file and writes
`MC10/source_manifest.json`. A changed or different decompilation stops the
build until the corresponding port is reviewed again.

## Build 29 additions (supersede historical placeholder rows below)

| Owned reference | Implementation / validation | Boundary |
| --- | --- | --- |
| Block registration/subclasses, Material | `mc10_blocks_extra.tc`, `mc10_collision.tc`; 122 IDs, 11,688 texture and 1,920 collision vectors plus 638 connected/far-coordinate cases | Static source facts; ID36 needs moving-piston tile state |
| RenderBlocks complex types | `convert_block_meshes.py` executes owned methods; `mc10_block_render_extra.tc` decodes templates; 1,340 fresh source cases and actual Thumb | Flat lighting, binary32 geometry/RGB332 palette adapter; active callbacks separate |
| BlockGrass/Leaves/Vine/TallGrass, ColorizerGrass/Foliage, BiomeGenSwamp | `mc10_block_colors.tc`, `mc10_biome_tint.tc`, original host-converted colorizers/tiles; 6,738 Java climate/color vectors | Exact 1.0 single-column lookup, not later 3x3 smoothing; bounded texture LRU preserves queued storage |
| EntityRenderer, World/Block/Vec3D ray trace | `mc10_third_person.tc`; 263 binary64 rays, 150 source camera matrices | Existing projection/resolution adapter, four-block camera; eight exact selection-bound rays |
| RenderPlayer/ModelBiped/ModelBox, GuiInventory/RenderItem | Shared third-person/inventory model and icon path; source poses/UVs/matrices, native pixels and actual Thumb argument/geometry probes | No invented hurt/riding/item-use state, capes, glint, animated special items |
| TileEntityChestRenderer/SignRenderer, ModelChest/LargeChest/SignModel | Closed single/double chest and sign board/post models; 32 source transform/UV cases | Active chest state, sign text and other tile models still pending |
| World.tick time subset, WorldProvider, World sky/daylight | `mc10_world_clock.tc`, `mc10_cos_celestial.tc`, `mc10_sky.tc`; 2,100 clock vectors, 236 color cases, 427 horizon cases and actual Thumb | Time advances on 20Hz attempts, not frames; full weather, clouds, fog and world simulation are not claimed |
| RenderGlobal.renderStars, owned sun/moon textures | `mc10_sky_pack.py` executes original deterministic star construction and converts textures | 780 original star quads; no runtime PNG/trigonometric star generation |
| GenLayer DAG, WorldChunkManager | `mc10_biome.tc`; 26,016 exact Java cells, seed/extreme-coordinate and cancellation/cache tests | Cooperative finite workspace; live queries only outside player FP expressions |
| NoiseGeneratorPerlin/Octaves, ChunkProviderGenerate base/surface | `mc10_generation_noise.tc`, `mc10_generation.tc`; 10,680 binary64 outputs, 24 base/surface chunks | Intermediate phases only, not complete generated chunks |
| MapGenBase/Caves/Ravine | `mc10_generation_cave.tc`, `mc10_generation_ravine.tc`; source arrays and actual Thumb 32KiB guards | Ordered isolated carving; structures/population/light/persistence remain absent and play-readiness stays false |

The 1 MiB NBT/geometry storage overlay, native batches, tint recipes/LRU, keyboard
mouse and RGB332 rasterization are explicit NARM adaptations. They must not
alter game-state/random transitions or fabricate missing dependencies. Original
Java/JAR/PNG files remain local test/import inputs, never source-package files.

## Historical implementation map

| Reference source | NARM implementation | Current boundary |
| --- | --- | --- |
| `java.util.Random` behavior used by 1.0 | `mc10_java_random.tc` | Exact 48-bit state, `next(bits)`, bounded `nextInt`, `nextFloat`, `nextLong`, and binary bits for `nextDouble` |
| `NoiseGeneratorPerlin(Random)` | `mc10_noise_perlin.tc` | Exact constructor: three coordinates and 512-entry permutation table |
| `NoiseGeneratorPerlin.func_805_a` | placeholder | Not implemented until general Java binary64 arithmetic is exact |
| `NoiseGeneratorOctaves` | placeholder | Depends on exact Perlin evaluation and signed Java `long` remainder/floor behavior |
| `NibbleArray` constructors, `getNibble`, `setNibble`, `isValid` | `mc10_chunk.tc` | Exact behavior over caller-owned fixed NARM storage |
| `Chunk.isAtLocation/getHeightValue/getBlockID/getBlockMetadata/canBlockSeeTheSky/getFalse` | `mc10_chunk.tc` | Exact read-only behavior for the 128-high 1.0 world layout |
| `Chunk.getRandomWithSeed` | `mc10_chunk.tc` | Exact Java int overflow, Java long arithmetic, XOR, and Random seeding |
| `NBTBase`, `NBTTagEnd`, primitive tags, `NBTTagByteArray`, `NBTTagString` | `mc10_nbt.tc` | Exact 1.0 tag IDs 0-10, named-tag encoding, big-endian scalar bits, Java modified UTF-8, copy and equality behavior |
| `NBTTagList`, `NBTTagCompound` | `mc10_nbt.tc` | Ordered NARM storage reproduces list behavior and compound key replacement/lookups; serialized compound order is intentionally unspecified as in Java `HashMap` |
| `CompressedStreamTools` | `mc10_nbt.tc` | Named-compound root validation, uncompressed/compressed memory and file paths, atomic `_tmp` replacement; gzip reader supports all DEFLATE block types and validates CRC/ISIZE |
| Java allocation used by NBT | `mc10_nbt.tc` platform adapter | Fixed-capacity tag/link/UTF-16/byte pools; exhaustion becomes an explicit error because NARM has no allocator |
| `Chunk` mutation callbacks, lighting, entities | placeholder | Requires mutable `World`, tile entity, and entity ports |
| `Material` and subclasses | `mc10_blocks.tc` | Source flags for all 30 material singletons: liquid/solid/block-movement/opaque/tool requirement/burnability |
| `Block` static registration and resident subclasses | `mc10_blocks.tc` | A 256-slot table reproduces every non-air ID present in the staged 3x3 window, including material, base texture, render type, opacity, normal-block state, light opacity/value and bounds. Unencountered IDs are explicitly absent, not approximated |
| `BlockGrass`, `BlockLog`, `BlockCloth`, `BlockStep`, `BlockWorkbench`, `BlockCrops`, `BlockFarmland`, `BlockStairs` texture/bounds overrides | `mc10_blocks.tc` | Source metadata/side-dependent rules needed by the resident block set |
| `GuiMainMenu` constructor splash selection | `mc10_asset_pack.py`, `mc10_menu.tc` | Original UTF-8 list, Java trimming/hash rejection, glyph mapping, Random consumption and pulse; NARM entropy supplies the no-argument Random seed |
| `GuiMainMenu.initGui` and `GuiButton` | `mc10_menu.tc` | Exact 320x240-derived geometry, five buttons, disabled null-session multiplayer state, hit testing, states, source textures and translated labels |
| `FontRenderer` menu subset | `mc10_asset_pack.py`, `mc10_menu.tc` | Original atlas, Java width scan, left-to-right glyph sampling, shadow colors, centering and transformed splash text |
| `GuiMainMenu` skybox | placeholder | Deliberately disabled for the current milestone; `mc10_menu.tc` clears to black before drawing the reproduced menu UI |
| `GuiMainMenu.drawScreen` logo/footer | `mc10_asset_pack.py`, `mc10_menu.tc` | Both normal and rare logo branches, splash, version and copyright; logo centered to fit 320 pixels |
| `GuiScreen` pointer input | `mc10_menu.tc`, `mc10_main.tc` | Virtual mouse coordinates feed the source button hover/hit path; arrows move two pixels while held (8 ms at the default cursor speed), Enter is mouse button 0, and the smaller cursor plus independent cursor-rate slider are NARM input adaptations |
| `GuiOptions`, `GuiVideoSettings`, `GuiControls`, `GuiSlider`, `GuiSmallButton` | `mc10_options.tc`, `mc10_asset_pack.py` | All three Minecraft 1.0 screens, exact 320x240-derived button geometry/order, source labels, slider formulas, option cycles, Done/Escape navigation, key capture and duplicate-key highlighting. Converted option records are preferred; exact en_US source strings and a knob derived from the already-cached original GUI pixels provide backward asset-pack compatibility. The keyboard-driven virtual pointer is the declared NARM input adaptation |
| `GameSettings`, `EnumOptions`, `KeyBinding` | `mc10_options.tc` | Exact 1.0 defaults, 13 binding identifiers/codes, display rules, and `options.txt` field names/order. NARM HID keys translate to LWJGL codes at capture; fixed RGB332 resolution means GUI scale is retained but does not resize the physical framebuffer |
| Main-menu calendar splash overrides | placeholder | NARM exposes elapsed time but no calendar/RTC date API |
| `SaveConverterMcRegion.getSaveList`, `SaveFormatOld.getWorldInfo` | `mc10_select_world.tc` | Scans up to 32 arbitrary directories under the NARM-adapted `/MC10/saves`, uses `level.dat` then `level.dat_old`, marks versions other than 19132, and sorts by descending `LastPlayed` then folder name |
| `WorldInfo(NBTTagCompound)` | `mc10_select_world.tc` | Loads all constructor fields including MapFeatures default-true behavior and optional Player.Dimension; Java longs remain exact two-word values |
| `GuiSelectWorld`, `GuiWorldSlot`, `GuiSlot` | `mc10_select_world.tc`, `mc10_asset_pack.py` | Original 320x240 geometry, 36-pixel rows, selection/double-click rules, labels, button states, conversion/game-mode/hardcore lines and source GUI textures; keyboard-driven pointer and click-to-page scrollbar are input adaptations |
| `SimpleDateFormat()` in `GuiSelectWorld` | `mc10_select_world.tc` platform adapter | en_US short date/time form with fixed UTC-03:00 because NARM has no locale or time-zone service |
| `RegionFile`, `RegionFileCache` | `mc10_region.tc` | Exact read-only 4 KiB sector/header addressing, timestamp lookup, record bounds, gzip/zlib dispatch, and `r.<x>.<z>.mcr` mapping; cache eviction and writes are outside this milestone |
| `SaveOldDir.getChunkLoader`, `ThreadedChunkLoader.loadChunk` | `mc10_region.tc` | Resolves arbitrary chunk coordinates using Java floor semantics, validates root/`Level`/`Blocks`, and applies the source coordinate-relocation rule |
| `ChunkLoader.loadChunkIntoWorldFromCompound` | `mc10_region.tc`, `mc10_chunk.tc` | Loads the exact legacy arrays and scalar chunk fields; valid source arrays are required within fixed NARM storage, while malformed-array regeneration awaits `World` lighting support |
| `ChunkLoader` entity, tile-entity and scheduled-tick reconstruction | explicit placeholder | Lists are recognized and counted, but object construction requires the corresponding entity, tile entity, world and tick systems |
| `Entity.readFromNBT`, relevant `EntityLiving`/`EntityPlayer` state | `mc10_world.tc` | Preserves exact serialized position, motion and rotation bits; reproduces the motion clamp, integer floor coordinates, dimension and core scalar fields needed by bootstrap. Inventory and active entity behavior remain placeholders |
| `World` read-only block/metadata/height/light access | `mc10_world.tc` | Source coordinate bounds, 128-block height, negative-coordinate mapping and chunk-local indexing over resident chunks; a `try` result makes nonresident terrain explicit |
| `WorldProvider` surface bootstrap | `mc10_world.tc` | Dimension 0 flags, height and exact 16-entry brightness-table formula; Nether and End return an explicit unsupported-dimension error |
| `ChunkProvider`/`ChunkProviderLoadOrGenerate` residency | `mc10_world.tc` platform adapter | Source provider/load semantics over a fixed 3x3 player-centered window. This replaces Java's unbounded map to fit NARM memory; missing/corrupt chunks are explicit and no generation is invented |
| `RenderBlocks.renderBlockByRenderType` resident subset | `mc10_render.tc` | Geometry/UV paths for types 0 standard, 1 crossed, 2 torch, 4 fluid, 6 crops, 7 door, 8 ladder, 10 stairs, 11 fence and 18 pane. The fixed projection remains the separately device-confirmed path |
| `WorldProvider.lightBrightnessTable`, `World.getSavedLightValue`, `RenderBlocks` directional factors | `mc10_render.tc` NARM adapter | Saved sky/block light, source celestial/time/weather subtraction, gamma and directional face factors select one of sixteen precomputed RGB332 terrain brightness levels. The earlier attribution of distortion to this atlas was disproved by ARM execution: the NARM compiler clobbered the first vertex's Z argument when loading a large frame. That compiler defect is corrected. Exact colored lightmap and ambient occlusion remain placeholders |
| `BlockGrass.getBlockColor`, `BlockTallGrass.getBlockColor`, `ColorizerGrass.getGrassColor`, `RenderBlocks.fancyGrass` | `mc10_render.tc` | Samples the converted owned `misc/grasscolor.png` at the source fallback temperature 0.5/rainfall 1.0, tints grass-top tile 0 and tall-grass/fern tiles 39/56, and draws masked side-overlay tile 38 in Fancy graphics. Dead-shrub metadata 0 remains untinted as in `BlockTallGrass.colorMultiplier`. Per-coordinate `WorldChunkManager`/GenLayer biome tint remains explicit pending work because legacy McRegion chunks do not store biome arrays |
| `EntityRenderer.setupCameraTransform` first-person view inputs | `mc10_render.tc` | Saved player position and rotation feed the previously device-confirmed fixed NARM projection. Dynamic FOV and render-distance projection changes are deferred until they can be isolated and validated independently; view bobbing, hurt effects, anaglyph, third-person view and fog remain outside this milestone |
| `World.getSkyColor`, biome colorizers, `RenderGlobal.renderSky/renderClouds` | explicit placeholder | Exact sky/climate and per-coordinate biome colors depend on the not-yet-ported Java binary64 noise path. The static sky remains black; grass uses the separately documented source fallback rather than invented biome data |
| `RenderGlobal` terrain submission | `mc10_render.tc` platform adapter | Traverses the explicit 3x3 resident window and submits source-shaped faces directly to NARM's depth-tested textured-quad API. Chunk display-list compilation, frustum sorting and incremental rebuilds are not claimed |
| `GuiSelectWorld.selectWorld` chunk/world start | `mc10_world.tc`, `mc10_render.tc` | Selected `WorldInfo` restores player state, creates the read-only multi-chunk world layer and displays its first static terrain frame |
| `GuiCreateWorld`, `GuiRenameWorld`, delete confirmation | placeholder | Buttons and source enable states are present; actions are deliberately non-mutating until their source dependencies are ported |
| Other main-menu destinations and click sound | placeholder | Multiplayer, texture packs, and owned sound conversion are not ported yet |

## Build 28 player/streaming foundation (supersedes earlier table boundaries)

The owned 1.0 source remains authoritative. The verifier additionally pins the
inventory/container/item, HUD, first-person model, loading-screen and source
input/render dependency files; it includes no owned Java source in releases.

| Source responsibility | New implementation | Boundary |
| --- | --- | --- |
| InventoryPlayer / ItemStack / ContainerPlayer / SlotArmor | `mc10_items.tc`, `mc10_inventory.tc` | 36 main + four armor slots, source click/right-click/quick-transfer ordering, limits/subtypes, selected hotbar zero on load; private immutable serialized compounds survive parser reuse |
| EntityPlayer / FoodStats / PlayerCapabilities NBT | inventory loader hooked in `mc10_world.tc` | Loaded health/food/air/XP snapshots, not invented survival ticks; world game type controls HUD/creative mode |
| GuiIngame / GuiInventory / RenderItem | `mc10_player_ui.tc` | Original cropped owned assets, HUD/selection/stack counts/durability, supported item and flat-block icons; read-only session UI, unsupported actions/visuals explicitly labelled |
| ItemRenderer / RenderPlayer / ModelBiped / ModelBox / TexturedQuad | `mc10_viewmodel.tc` | Source equip/swing/arm/bob transforms and supported hand/item meshes, separate depth pass, pose/stack/light snapshot captured with terrain camera; special use/glint/map dependencies remain pending |
| EntityPlayer cameraPitch / Java Math.atan | `mc10_atan.tc` + existing binary64 primitives | Permissively licensed Sun fdlibm finite atan, preserving its notice, tested against Java; not a replacement animation curve or firmware math service |
| ChunkProvider / ChunkProviderLoadOrGenerate / ThreadedChunkLoader | `mc10_stream.tc`, incremental renderer helpers | Exact signed coordinates and staged complete chunk data in a fixed nine-slot adapter; one read per pump, no new chunk generation or whole-window rollback guarantee |
| LoadingScreenRenderer / Minecraft.preloadWorld | world loading UI + game loop | Original title/subtext/progress layout at 320x240, retain image during cache work; remove both custom compilation screens |
| Mouse/keyboard platform adapter | timed cursor + game input | Preserve elapsed repeat intervals, independent sensitivity/speed, Enter-left/Space-right inventory clicks, original rebound inventory key and hotbar selection |

World access is invalid throughout provider loading/cache compilation and on
any error. Unsupported block IDs/render types are recorded with their world
coordinates and stop before access is published. Surface-capacity overflow
continues bounded validation of all remaining cells before choosing the exact
scan fallback. It cannot hide an unsupported block late in a chunk. Tests cover
a real adjacent owned window containing unported ID44: explicit error rather
than a disappearing slab. This is a deliberate dependency boundary, not a
claim of an unrestricted renderer/registry.

Presentation adaptations remain declared: opaque GUI background rather than
frozen-world alpha, white masked crosshair rather than destination-invert,
RGB332 lighting/quantization, and the existing fixed terrain camera. Hand FOV
uses the source fluid-surface predicate at this adapter's captured near-center
camera point, not a claim to reproduce the missing original OpenGL unprojection
chain. Hand View Bobbing is active; terrain camera bob/hurt/portal effects are
still pending. GUI cubes/slabs, player preview, crafting/drop entities, creative
inventory, special item actions/glint and saving are not falsely approximated.

Validation includes original-asset full-screen/pixel checks, source Java GL
matrix recording, exact animation/data vectors, actual NARM Thumb execution,
stream failures/retries and ordered terrain-cache parity. The generic firmware
depth clear is independently tested for color preservation, generation wrap,
depth separation and unchanged diagnostic state. Arduino Verify/ELF confirms
unchanged static SRAM headroom and previous rendering kernels. Device stability,
appearance and performance remain to be tested on the RP2350.

## Build 27 menu and input follow-up

The original `GuiOptions`, `GuiVideoSettings`, `GuiControls`, `GuiSlider` and
`GuiIngameMenu` remain the behavior/label/layout oracles. The option formatter
must append its value without erasing the translated label and colon. GUI
tests must inspect complete labels and their actual bitmap output, not only
the existence of label strings in source files. Slider hold/drag uses Enter
as virtual mouse button zero, with source clamping and release handling.

Two explicitly requested platform controls extend unused screen slots:
`cursorSpeed` controls menu-pointer repeat rate independently of the original
`mouseSensitivity` camera formula; `occlusionCulling` selects the generic
native depth-occlusion mode independently of the retained Advanced OpenGL
option. Backface culling remains enabled in ordinary gameplay. The existing
near-plane/inside/unknown and non-eligible-shape fallbacks are unchanged.

`EntityRenderer.renderWorld` enables `GL_CULL_FACE` before the opaque terrain
pass. The exact saved-pose regression identified the old two-sided renderer
drawing one pixel of a genuinely back-facing top at local bounds
x=6..7, y=74, z=19..20, with the camera below at y=73.620. Culling that face
lets the adjacent front-facing wall own pixel (250,48), as the source's
culling state requires. This explains why Build 26's strict legacy-output
hash check rejected this pose. It is not evidence of a new firmware or
compiler defect. The new regression records the one-pixel edge/depth result
instead of silently relaxing output checks globally.

The opaque owned pause background remains an explicit substitute for the
unavailable translucent world-overlay service; no alpha approximation,
Minecraft-specific firmware API, world-saving behavior, achievements or
statistics implementation is invented in this update.

## Build 10: interactive-player boundary

| Reference | Implementation | Reproduced boundary |
| --- | --- | --- |
| Java binary64 finite arithmetic | `mc10_fp64.tc` | Integer two-word add/subtract/multiply/divide/sqrt, round-to-nearest-even and float conversions; positions/motions/AABBs are not narrowed to float. 12,571 host vectors pass |
| `MathHelper.sin/cos/sqrt_float` | `mc10_math.tc`, host `mc10_math_pack.py` | Original Java sine table exported offline into CRC-checked `math10.dat`; source float index expressions and sqrt conversion |
| `AxisAlignedBB`, `World.getCollidingBoundingBoxes` | `mc10_collision.tc` | Sweep/expand/offset, intersection and axis clipping; X/Z/Y source scan order; 512-box fixed pool replaces Java lists |
| Block collision methods | `mc10_collision.tc` | Resident standard blocks, farmland, stairs, fence, pane, door and ladder shapes; source null boxes for plants/torch/crops/fluids/pressure plate. Unknown block IDs pause instead of becoming invented cubes |
| `Entity.readFromNBT/setRotation/setAngles/moveFlying/moveEntity` | `mc10_player.tc` | Source motion clamp, rotation remainder, binary64 positions/bounds, sensitivity-scaled look/pitch limits, Y/X/Z collision order, stepping/ySize, grounded/motion clipping and fall-distance tracking |
| `MovementInputFromOptions`, `EntityPlayerSP.onLivingUpdate/pushOutOfBlocks` | `mc10_player.tc`, `mc10_game.tc` | Configured movement, sneak scaling, push-out, sprint double-tap/timers and Creative flight. Item-use slowdown, potion effects and portals remain pending |
| `EntityLiving.moveEntityWithHeading/jump`, fluid/vector methods | `mc10_player.tc` | Jump/drag/friction/gravity, water/lava acceleration/escape, ladders and flight's vertical restoration. Damage, hunger/exhaustion and entity-object collisions are not implemented |
| `Timer`, runtime input/presentation | `mc10_game.tc`, `mc10_render.tc` | 20 Hz integer-ms adapter with 1s elapsed clamp and 10-tick catch-up ceiling; bounded render batches permit input/ticks between submissions. Fixed camera per frame; no partial-tick interpolation, clock drift sync, FPS guarantee or world-time ticking claimed |
| `GuiIngameMenu` | `mc10_game.tc` | Source button geometry, pause/resume and Options return; disabled Achievements/Stats. Exit explicitly says without saving and returns to Select World. Existing owned-texture backdrop is a placeholder for source translucent world overlay |
| Resident-window failure | NARM-only safety adapter | Missing required terrain/unported block/pool exhaustion rolls back complete player tick and pauses. Streaming/generation/saving remain absent |

Verification: `test_player.py` executes extracted owned Java methods and
unchanged production C modules over 602 bit-exact ticks (17 fixtures).
`--arm-compiler` executes nine eight-tick trajectories as actual NARM Thumb
instructions using Unicorn; this is not the unsupported GP-PICO emulator.
`test_game_loop.py` covers input/timer/pause/options behavior with service
stubs. Hardware acceptance must still check look direction, configured keys,
jump/sneak, shape collisions, fluids/ladders, pause/Options/resume and boundary
diagnostics. Renderer projection/lighting/tint stay on the confirmed fix9 path.

The new source classes are pinned by `tools/mc10_verify_source.py` and appear
in the generated `source_manifest.json`; proprietary Java is only loaded from
the user's owned decompilation into temporary test directories.

## Build 11: terrain performance adapter

The renderer's geometry, UV, mask, directional-light/gamma and grass-overlay
rules are unchanged. A per-entry compact surface list caches static visibility
and saved-light levels for the read-only resident world, following the original
chunk/X/Z/Y order. Hidden standard blocks emit no quads and need no per-frame
entry; special blocks keep their exact existing render path. Cache exhaustion
returns to the full renderer rather than omitting geometry. World entry rebuilds
the list; future mutation/streaming must explicitly invalidate it.

The staged world reduces to 7,642 eight-byte records. The previous eight-block
batch cap is removed: input pumping remains time-bounded and the renderer checks
elapsed time every 16 candidates or 16 emitted faces. Key-code translation is
cached until bindings change, without changing held-key/pause semantics.

Verification compares full ordered streams of 20 vertex/UV words, mask and
light selection over 15 camera/gamma/Fancy cases, including near-vertical sky
views, against both raw replay and archived Build 10. Overflow parity is tested.
The actual compiled ARM cache decoder emits the same 480 coordinate/UV words
over 24 faces at metadata 0/5/15 and clears its transient flags after replay.
The movement arithmetic/source-oracle tests remain unchanged. Device frame
times are still needed; no hardware FPS guarantee is made.

## Audited source hashes

```text
NibbleArray.java            d0e7672ea3377b6434f10d3ab4c353de56363bde901bac9f175c5cd6b9739c42
NoiseGeneratorPerlin.java   b7db3c2dc320466c4437f27f77e563945187e9f53ded29abe2b2c5e9766e663e
NoiseGeneratorOctaves.java  0f3816e2dc4a7912637db4c2feb7df4cdbca4bd39f5596fa7df73d82a960256d
Chunk.java                  a3b5186a9951ed5671c3bae1bfad394ec4394cd26be5c8ed2718026c06139984
Block.java                  64f18844b6af11ae7e983aad7b569ac0323d4c920ed2bb152fc0ee75d56332b4
GuiMainMenu.java            9202b4f14fb6a20309e9355f599db469efce46758be98d5b9c32a786cf0b82fb
GuiOptions.java             0f611c67826da400c51aba8e120a3da622fbb20b3667010747bb6eb6da3c1537
GuiVideoSettings.java       0c27980823fc18c90783ecfedc5a71e01b469fceb5bd540e501672c9b409c94d
GuiControls.java            4cfcb76de61e05e8341f882bf96477619531e88c1b5cf350b0eb6dc6508f8594
GuiSlider.java              146429ffba6cd1d2a237ccaac7ed0b19a2ca1fbb8c1d6b099d8fc826d136f057
GuiSmallButton.java         667fb0aba130cac3353acf864cb524365373d80f381c1eea79ffa73b16b33012
GameSettings.java           74b0c1b662728cf3dd4f289db3d6c657c454814ae5ce6566cea78d06a19d6ff3
EnumOptions.java            62fc54ddaf9bf1d3d4cd0f8f111fc0854de6be292443f219b05cd969782215fc
KeyBinding.java             decac5186532c48398d0b2c3299ad09d1d1a70d41248c1807c3ab4a684d51e9e
GuiButton.java              1aeeb4a991f9d11497f9a841eb88eb6ae7da00002f76e3ad9ae61b88e157beb6
GuiScreen.java              e0da3cf7de65559871df2369fbccc591b01dd13473c443f70a49c597a490b0eb
Gui.java                    85f93ce98d034e23b1af4f7d063d5eb92060b478635ffce58d2a5bb0a0825f82
FontRenderer.java           937cb072ae9e5d7e5ec2becab95930c93070280bd2674cd7a410ba5a3ec549a9
StringTranslate.java        730d359e2745a19691ca366dd27b0112bab8f0825f38185fcefb53b28992997d
ChatAllowedCharacters.java  8436b10d6216bff75742029f16d9133ac9cef073c505ca6c81d7e8c4d6d13fdd
RenderEngine.java           ece8984a4674e949281049affe75c20160cb162b3ea2ed309239ca2a5bcbf600
MathHelper.java             00ee99e03355f54b3db13d8f486beb7affa6dea7fc62e662fb758e4d8f9debff
NBTBase.java                7a216a3036856c0f8d92dc163109898c624afcb4f3ccd8f714df0ce0d633a433
NBTTagEnd.java              e8146b46c1fdea6b532de485beeba287b1da521fd5b6236df7737bb16b5c4c6d
NBTTagByte.java             aa5ee9ef8caec019485f25e65f1246ac9d5bca90ce3f5b4f059d666472a5a005
NBTTagShort.java            f9a086f0b7a0caeaa0caa054e765686033320961802f7b84c0aa918414b1c8d9
NBTTagInt.java              820021fd92dfc9d4760f84faeafd7f3f8057b910148ece85adf6db2e104b3617
NBTTagLong.java             75f41ce2b3e6f3a1bcbd0e68c93980243403e7dd1f47c9163c1672c931b43fc9
NBTTagFloat.java            f697304cc26a3ab569c5d2024bc6956b1f9d373324c4c2f113d6f657c0f0bd99
NBTTagDouble.java           50f410f55b05ded351b59e3209e4735d760061b5f737e6d16680f7b1e3a7aa3d
NBTTagByteArray.java        96dc8c0c33fa6345943b0783ef3063ff8bdf8dc6125990d62eb651481346b0eb
NBTTagString.java           431c73646bcc06308f5d6d120b6033c6400707bdb7c59d74d895028e81a4e220
NBTTagList.java             d32a921da1ebb2334a64c94fd252a190b5ceb382ff4edca71984026e6c0449aa
NBTTagCompound.java         cfaf68cd850b60b85c3901a0c56b9b4996c3ea46d5918ac5e3f868068a2b066b
CompressedStreamTools.java  4fd727b9d8ec76119ad8aed9f692500fc8eca0116c3eee48002d528b1ccf3d05
GuiSelectWorld.java         43b2c5729481bc8658950ad6338a9a188eeb7324995d6c3d5a2be0e3fc93dbc7
GuiWorldSlot.java           1a068806a036a57b42f217b8149212fd824436c2a6c22fd70d6a27870cf27c4d
GuiSlot.java                60fca610b098b230b4e24176d837a7186b495d548daca37c529c71ce836f74e6
SaveFormatComparator.java   336c10df2f0e9fa77ee62feb9b7a8af054198e585dd0a0b0e4b9bbf2ca9c8d9f
SaveFormatOld.java          ca6d95c46069ee921810d0ab0cb151dae23657abda6e30b8738ecdb3a297ae2e
SaveConverterMcRegion.java  7e1c3e3317f888e8e7c64e99a3412db9d6875391f73c839a13c00deeb2033067
WorldInfo.java              9428dfd3f85e5aeccd2a14722e576b32aea76456b933b74e8c236d0597a97cca
SaveHandler.java            452aacfbd295d31bea8064ac167f500b3e2124a8ccb6d55e86650cb269923ed3
SaveOldDir.java             7f1f4cb497f7373121f45b4382ce477760bcdbe2ee51023924b8a066159d8560
RegionFile.java             565b59b957a6470933f4a3f15cac2af1b421093f3cef806a77cc87b9fe04e1b6
RegionFileCache.java        f0e78e1ab38f36c5ebe34f1855bcf72bd523175dd55cbc4049767d4f543b42f7
ThreadedChunkLoader.java    27377be061390f644d9baf5196a403e7a47049c7ef7ce8ed41ce3a346fbd0db7
ChunkLoader.java            b37573b51d19698b6a77b98dedc6d7d505a2249bdc2b8d51f14591b46aa0f613
Entity.java                 59eb1b2c6dd2c9fa863b7de1c1066ea48d1d508ed17ba6c113cd6b5cf2e9e657
EntityLiving.java           8cce2bcfcbbb9dae215a5caa6f57ffcd51047b9dae74e1e189ec457ac9b2958e
EntityPlayer.java           ed4f77220b2e9545124c4d1ec7fb8978538b86182f2eea6e08a0342f0622db81
World.java                  8ba535e2cad6014b1f2168d5eeb7b566301f537056615aab3175a8b70b5e2008
WorldProvider.java          536c4683473312bd6571fc5b57e868b46cf7ee0a2d4c8efbb09517a61de027be
ChunkProvider.java          bda2465690efbadb34993c54f3a2bbd74c9726998b2833f93d1c19ce291decbc
ChunkProviderLoadOrGenerate.java 7ef3ceb4a6ce6206a82d644d959d144b50980e0e542d2e1e03e8b3498022e3ef
Material.java               dc64d5b945f01f445d4b45757413b7019f57078a234150bef51875676bcd6083
MaterialTransparent.java    1d1de1f6e50d85d4478365d98924b147ac9ef1af9f7b5d19b5fb18ec92edfab4
MaterialLiquid.java         64fb076a1b6a28587d71244a32d0d2974ce061d012c84fe750d33365c913c64f
MaterialLogic.java          ee835183210e57261d492116372b05edd8c62d9210b49acab4eba41500374d3e
MaterialPortal.java         05496a9b30460cb238d3c966f64a573c470f745e5a2f580f3eaeb8b971d805e4
MaterialWeb.java            ba0e5ff3f7cdda1d45017d7aa29f66cbb0a5db9f2a2c658bfee43427a6072d0d
BlockGrass.java             4c33803c941352c315744d480acc2f59f6a5b599c041a1b99bd919cef6430df7
ColorizerGrass.java         2f8c170c46780b6c576c29d73980440a144928603ace00080596f651d03ee916
BiomeGenBase.java           80f6c86081e95b7ef4f41445c535b166812569631de873005d88d93a9bc88250
WorldChunkManager.java      9753f8e69f34c7a5155076eedbc54fb8a85cef7d1893e349a9bfc2abe71f297e
BlockLog.java               b66bb939df043461d023449fd1cd4f4d4615562f29e4797d380e2c784eea20ca
BlockCloth.java             9235e39cc400d42404924022ce3847cb33bf941b34e60fadb807d4ba6c45b3ac
BlockStep.java              dfa3b230a61d2947fb4080a269ae6f5fe54760e3351d3a5c845c9cbbf86dce2a
BlockWorkbench.java         897d1e55410ec1910387f3dc044075701d9c928b9d0636b4e09d4117526dd1c1
BlockFarmland.java          2fd765c4608737f304571703d09d8e97835cac730f1b3efad40e4490ca409455
BlockPressurePlate.java     d9f483bd4ad3840645c59f9b7525342a4b677a6e4c961fd3423959cede90e47c
BlockFluid.java             ccca6a80a9ed37d728f5d4dc14933aa2ebb3dee675cf37219e38aa1ff953393c
BlockTallGrass.java         2c54e1964d9f3d349ac9017346e5ea2e351061c95c1b57fbb9a85610d81a93a7
BlockFlower.java            03ec91d7d9448034fd0d01545a0a1ad199946fce5a42e938238cc6006ea1a629
BlockTorch.java             30a860f2da6a3687d10be678a9bd101cdd832c178a78dd9b4e6b3f092569a3d9
BlockStairs.java            371d7490a61a54fc26b28e2c8a03af3482baf71bd075783ccb8bf09def2a4460
BlockCrops.java             1909747b52dff0a5374ba00c303051c57939724c9ccd21f211d8d693f263561f
BlockDoor.java              5504a92b245f9115efee8830a23a3ce916c8f1d9458dcf89afac6746a182d34b
BlockLadder.java            24d51aee014c0bb110a8b344523aa10b4cbfa161629c64d39c33e1ab1665ad9f
BlockFence.java             c85ee045e1ca06c8969417ea0a1ace43241d3829c0c4ea1f9c0d70c81dda6cd1
BlockPane.java              4f591a2fda5c962d7c26d051316e87432129b2a70e35d1c4e0fc7b1f76230b56
RenderBlocks.java           19d9e87c13338f467bc197650cc5854a62c142526a12fc79dffb2492fb37b101
EntityRenderer.java         b3df47f1157b6d8f15cd3eae1c1f20e583792bef6959a253e0c6f54aa6e9ddaa
```

## Build 12: NARM frame profiling

`mc10_profile.tc` is platform-only diagnostic instrumentation, not a Minecraft
behavior implementation. It times existing terrain, native draw, input/tick,
yield, setup and UI/presentation paths without changing their geometry or game
rules. Nested draw/tick intervals are subtracted from their enclosing render/
input totals; the remaining categories partition one complete wall-clock frame.
The overlay shows the previous completed sample, including matching face and
batch counts. Explicit yield intervals do not capture all implicit preemption:
preemption within a measured call remains charged to that call. Pauses discard
partial samples; tests cover unsigned microsecond rollover and injected call
durations. `test_profile.py` checks accounting; unchanged full-stream renderer
and source-player tests remain compatibility checks. No new gameplay, original
source dependency or substitute Minecraft asset is introduced by this build.

## Build 13: spatial runs and exact geometry replay

The geometry pool records output from the existing source-derived renderer as
20 unchanged binary32 vertex/UV words per quad plus light/mask/overlay flags.
Original surface traversal and intra-block face order are retained. Light keys
are resolved through the current gamma lookup at replay; Fancy grass controls
the original recorded texture-38 overlay. Cache exhaustion rolls back a whole
block to the original renderer, without replacing shapes or dropping faces.

Spatial runs are contiguous portions of that same surface list. Eight-block
section bounds conservatively bound the old per-block culling expressions,
including their 1.6 margin and an extra float-rounding guard. Rejected runs can
be skipped without regrouping transparent or overlapping faces. The unchanged
per-block test still applies to every visited cached/fallback block. Tests
compare 63 complete ordered vertex/UV/mask/light streams, edge positions,
independent bypasses and forced pool overflow. Compiled ARM capture/replay
also verifies unchanged words, live lighting, Fancy toggles and mask selection.
HUD/presentation wall time is split inside the existing UI category. No
firmware drawing API, projection, texture asset or gameplay rule was changed.

## Build 14: automatic renderer benchmark

`mc10_benchmark.tc` is compiled only with `MC10_AUTOPROFILE`. It is NARM-only
diagnostic orchestration, not invented Minecraft gameplay. Existing save
discovery, WorldInfo/NBT bootstrap, player initialization, owned-asset loading,
and the unchanged terrain renderer provide its inputs. It freezes that player
state, samples the saved view and pitch -90 degrees without gameplay ticks or
movement, and restores the original orientation. No source game state is saved.
Initialization and data fingerprints are outside the measured frame intervals.

The SD CSV writer emits uniform escaped records, raw samples and independent
min/median/max timing statistics; chooses a new sequential path; handles short
writes; and validates size and byte-for-byte readback. File I/O occurs after
measurement, never inside a timed frame. Escape/focus/close abort samples. Host
tests verify frame/warm-up ordering, frozen/restored camera, zero tick work,
CSV structure/statistics, world pin validation, unique naming, error/overflow
handling, and post-measurement I/O. Device storage/boot remains unverified until
the user runs this benchmark build. Normal menu/game entry is preserved without
the compile flag, and Build 13's source-derived geometry/lighting remain intact.

## Build 15: exact glyph batching and HUD A/B benchmark

The flat FontRenderer adapter transposes the owned font atlas's existing mask
into 256 exact MSB-first 8x8 masks, using its unchanged glyph-pixel predicate.
One native glyph service replaces individual native pixel calls; original
spacing, colors, full-string shadow/foreground ordering, overlaps and clipping
are retained. The original pixel implementation remains the error fallback.
This is a presentation optimization, not replacement font artwork or metrics.
Tests compare all 256 masks and 1,616 rendered glyph/string cases against the
original owned font pixels, including clipping, shadows and fallback.

The dedicated benchmark now has four fixed-camera HUD-on/off cases, one warm-up
plus five samples each, with buffered CSV output after all measurements. HUD-off
does not disable presentation, timings, cancel handling or terrain; it only
omits the overlay/crosshair. Schema v2 records each case separately and the
reader validates matched geometry workload. Native terrain rasterization and
Minecraft rules remain unchanged; timing reductions are not claimed until a
new device log is collected.

Native terrain-path inspection: the MC10 quad adapter submits the unchanged
256x256 light-atlas texture with flags 0. The firmware's specialized unmasked
16x16 sampler requires a 16x16 texture and flags low bits 3, so MC10 does not
take that fast path. This is a future investigation target, not a measured
attribution of the complete Draw interval. Build 15 makes no sampler, UV,
projection, mask, firmware or rasterizer change.

