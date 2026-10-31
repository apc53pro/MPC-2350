# Porting rules

1. MC10 remains a separate program. Do not include TinyCraft modules or asset
   packs.
2. Decompiled Minecraft source is a behavior oracle, not text to translate
   line-for-line. Record invariants, inputs, state transitions, and expected
   outputs, then implement them in NARM-oriented modules.
3. Preserve numeric IDs, tick ordering, random-number consumption, chunk
   indexing, nibble packing, and save semantics when they affect observable
   behavior.
4. Presentation-only adaptations may change resolution, color quantization,
   controls, and UI geometry, but not gameplay state transitions.
5. Every compatibility primitive gets deterministic host vectors and an
   on-device self-test where practical.
6. Do not invent reduced gameplay behavior. If a source dependency is not yet
   reproduced, leave an explicit placeholder instead of an approximation.
7. Do not commit Mojang textures, sounds, source files, JAR contents, or other
   proprietary inputs. The local importer consumes the user's owned 1.0
   installation and writes ignored, deployable NARM asset files.
8. Prefer fixed-capacity pools and arrays. NARMCC currently has no public
   `malloc`/`free` API.
9. Performance-profiler builds must retain interactive play-testing after the
   automated run. Save and verify the CSV first, restore the saved viewpoint and
   HUD, then offer Enter to play / Escape to exit. Play uses the optimized path,
   normal input/ticks/pause controls, fresh timing and held-key gating; benchmark
   A/B alternation and detailed counters must not continue during play. Keep
   saves read-only and the recorded CSV unchanged. Startup-isolation diagnostics
   are a separate, explicitly labelled non-gameplay tool.
10. In interactive play, F3 toggles profiling at a completed frame boundary.
    Disabled profiling performs no optional timing/counter collection or
    diagnostic drawing, except the FPS readout. Keep rendering's required
    yield budget and gameplay clocks; do not disable input or physics. Native
    diagnostic flags stay zero, and the automated benchmark/CSV is unaffected.

## Initial module map

| Minecraft 1.0 responsibility | MC10 module direction |
| --- | --- |
| `java.util.Random` use | `mc10_java_random.tc` |
| `Block`, materials and block tables | `mc10_blocks.tc` |
| `Chunk` byte/nibble storage | `mc10_chunk.tc` |
| `World` read-only bootstrap and resident chunks | `mc10_world.tc` |
| World mutation, scheduled updates, and ticking | future world modules |
| `ChunkProviderGenerate` and noise | future `mc10_generation.tc` |
| Java finite binary64 arithmetic for player/collision | `mc10_fp64.tc` |
| `MathHelper` sine table / float sqrt | `mc10_math.tc`, host math-table exporter |
| `AxisAlignedBB` and resident block collision | `mc10_collision.tc` |
| Local `Entity`/`EntityLiving`/`EntityPlayerSP` locomotion | `mc10_player.tc` |
| `Timer`, gameplay input, pause and bounded render pumping | `mc10_game.tc` |
| Other active `Entity` objects | future fixed-pool entity modules |
| `RenderBlocks` resident render types and continuous frames | `mc10_render.tc` |
| `RenderGlobal` chunk compilation/sorting and entity rendering | future renderer modules |
| `GuiMainMenu`, `GuiButton`, menu `FontRenderer` path | `mc10_menu.tc` |
| `GuiOptions`, `GuiVideoSettings`, `GuiControls`, `GameSettings` | `mc10_options.tc` |
| `GuiSelectWorld`, `GuiWorldSlot`, `WorldInfo`, save discovery | `mc10_select_world.tc` |
| NARM-only diagnostics | `mc10_ui.tc` |
| Remaining GUI screens | future source-specific screen modules |
| region files and chunk loading | `mc10_region.tc` |

