# TinyCraft v59 VMCC-to-NARM migration notes

## Source selected

The migration uses the latest complete v59 source under the VMCC v2 type-update
work tree. Its game modules carry the September 23, 2026 fixes and optimization
work, while the earlier packaged v59 release copies do not. Temporary benchmark
variants, including the `noentities` tree, were not used as the branch base.

## Deliberate changes

1. `tinycraft_v59.tc` includes `narm.tch` and `narm_compat.tch` before the
   original ordered modules.
2. The legacy VMCC timing, random, trigonometric, PS/2-key, and implicit
   array-length SD operations are mapped onto stable NARM APIs.
3. Directory-entry length/type outputs are unsigned to match
   `narm_dir_entry`'s pointer contract.
4. Runtime files use `/Tinycraft/...` paths. NARM storage services normalize
   relative names against the SD root and do not track the launched image's
   parent directory.
5. The output format changes from VMCC bytecode (`.bin`) to a relocatable native
   NARM image (`.narm`). The VM variable-base option is no longer applicable.

## NARM rendering follow-up

The first native build retained the VM-era terrain scheduler. That scheduler
could stop visible section generation below 20 FPS, mixed 16-block user render
distance with 8-block mesh coordinates, and discarded the complete cache when
the player crossed its margin. The NARM development branch now deliberately:

- warms the exact complete visible mesh window during world loading;
- guarantees time-spaced streaming progress even at low FPS;
- performs one-ring, FPS-gated background prefetch;
- preserves overlapping meshes and procedural columns across aligned rolling
  cache shifts and render-distance changes;
- caches natural column generation results instead of recomputing terrain and
  tree noise for every vertical voxel;
- detects mesh allocation failure, retries incomplete sections, and doubles the
  packed terrain capacity from 12,288 to 24,576 rectangles;
- recycles edit-split nodes immediately and runs delayed greedy cleanup only
  while the foreground stream is idle; and
- exposes streaming, cache, allocation, and build-time counters on the fourth
  `T` debug-overlay page.

These changes do not alter TCAC records, block IDs, save records, world seeds,
or the deterministic natural terrain rules.

No game systems, TCAC record layouts, block/item/entity IDs, save-record layouts,
or editable asset definitions were intentionally changed.

## Build profile

The branch uses `-O3` so NARMCC performs whole-program unreachable-function
elimination in addition to local folding and propagation. The image requests a
32 KiB native stack, a 4 KiB private heap, and foreground scheduling. TinyCraft
does not currently use dynamic allocation; its large working sets remain global
data/BSS.

## Validation boundary

Completed on the host:

- TCAC v8 pack build and inspection
- byte-for-byte comparison with the selected v59 asset cache
- optimized NARM compilation
- NARM container inspection and deterministic rebuild comparison

Still required on target hardware:

- startup asset loading from `/Tinycraft/assets.tca`
- keyboard controls and focus behavior
- terrain/entity rendering and performance
- world create/list/save/load/delete operations in `/Tinycraft`
- config regeneration and persistence
