"""Saved-pose source-facing regression, reusing an existing native renderer DLL.

The old two-sided renderer writes one back-facing top-face edge pixel. Minecraft
1.0 RenderBlocks.renderTopFace gives that face its +Y normal; EntityRenderer
enables GL_CULL_FACE before opaque terrain. Backface culling must remove it, not
preserve the old hash. All assets/world data remain external to this test.
"""
from pathlib import Path
import argparse
import ctypes as C
import os
import shutil
import struct
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--native-dll', type=Path, required=True)
parser.add_argument('--terrain-tests', type=Path)
args = parser.parse_args()
PROJECT = args.project
sys.path.insert(0, str(PROJECT / 'tests'))
from test_render_cache import load, trace, integer, Player, pair
from test_backface_culling import packed_image, rows, subsequence
from test_region import load_level, tagged
TERRAIN = args.terrain_tests or PROJECT.parents[2] / 'tests/terrain'
sys.path.insert(0, str(TERRAIN))
import test_textured_tiles as native

RENDERER = PROJECT / 'src/mc10_render.tc'
NATIVE_DLL = args.native_dll


def run():
    runtime = os.add_dll_directory(str(Path(shutil.which('g++')).parent)) if os.name == 'nt' else None
    with tempfile.TemporaryDirectory(prefix='mc10-saved-pose-', ignore_cleanup_errors=True) as tmp:
        tmp = Path(tmp)
        (tmp / 'renderer_under_test.tc').write_text('#include "' + RENDERER.as_posix() + '"\n')
        game_dll = tmp / 'saved_game.dll'
        built = subprocess.run(['gcc', '-shared', '-O2', '-fno-strict-aliasing', str(PROJECT / 'tests/backface_fixture.c'),
                        '-I', str(tmp), '-I', str(PROJECT / 'tests'), '-I', str(PROJECT), '-o', str(game_dll)], capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        game = C.CDLL(str(game_dll))
        pos = load(game, PROJECT / 'MC10/saves/New World')
        level = load_level(PROJECT / 'MC10/saves/New World/level.dat')
        rotation = tagged(tagged(level, 'Player', 10), 'Rotation', 9)[1]
        p = Player.in_dll(game, 'mc_runtime_player')
        assert tuple(struct.pack('<d', value).hex() for value in pos) == (
            'f21978f4e5db62c0', '00008014ae675240', '92a722f64bea7540'), pos
        print('Saved pose:', pos, 'NBT Rotation:', rotation, flush=True)
        terrain, mask = packed_image('terrain.png')
        grass, _ = packed_image('misc/grasscolor.png')
        pixels = (C.c_uint32 * 16384).in_dll(game, 'mc_asset_pixels')
        masks = (C.c_uint32 * 2048).in_dll(game, 'mc_asset_mask')
        C.memmove(pixels, terrain, len(terrain)); C.memmove(masks, mask, len(mask))
        C.c_uint32.in_dll(game, 'mc_render_grass_color').value = grass[127 * 256 + 127]
        game.mc_render_apply_grass_tint(); game.mc_render_prepare_light_atlas()
        assert game.mc_render_build_cache() == 1
        geometry = (C.c_uint32 * (12288 * 21)).in_dll(game, 'mc_render_geometry')
        geometry_bytes = bytes(geometry)
        geometry_count = integer(game, 'mc_render_geometry_count').value
        renderer = C.CDLL(str(NATIVE_DLL))
        signatures = {
            'worker': ([C.c_bool], None), 'scratch': ([C.c_bool], None),
            'diagnostics': ([native.WORD], None), 'pristine': ([native.WORD], None),
            'nonpristine': ([], None), 'end': ([], C.c_bool),
            'begin': ([native.PTR, native.PTR, native.PTR, native.WORD, native.WORD], C.c_bool),
            'draw': ([native.PTR, native.PTR, native.PTR, native.WORD, C.POINTER(native.Vertex), native.WORD,
                      C.POINTER(native.Transform), native.PTR, C.c_int32, C.c_int32, native.WORD, C.c_bool, native.PTR], C.c_bool),
            'cull_invalidate': ([], None),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(renderer, 'probe_' + name); fn.argtypes = args; fn.restype = result
        atlas = bytes((C.c_uint32 * (16 * 16384)).in_dll(game, 'mc_render_light_atlas'))
        mask_guard = native.Guarded(bytes(masks))
        textures = [(256, 256, native.Guarded(atlas[i * 65536:(i + 1) * 65536]), [None, mask_guard]) for i in range(16)]
        initial = (bytes(native.PIXELS), bytes(native.PIXELS * 4), bytes(native.PIXELS))
        yaw = struct.unpack('<f', bytes.fromhex('808db3c3'))[0]
        terrain_pitch = struct.unpack('<f', bytes.fromhex('a8998d41'))[0]
        for pitch in (terrain_pitch, -90.):
            captured = []
            for cull in (0, 1, 3):
                integer(game, 'mc_render_cull_mode').value = cull
                stream = trace(game, yaw, pitch, 0., 0)[0]
                camera = bytes((C.c_float * 18).in_dll(game, 'mc_render_camera'))
                captured.append((stream, camera))
                print('View', pitch, 'mode', cull, 'faces', len(stream) // 88,
                      'frame_cull_allowed', integer(game, 'mc_render_cull_frame_allowed').value,
                      'camera', list(struct.unpack('<18f', camera)), flush=True)
            subsequence(captured[0][0], captured[1][0])
            assert captured[1] == captured[2]
            integer(game, 'mc_render_cull_mode').value = 1
            integer(game, 'mc_render_cache_ready').value = 0
            assert trace(game, yaw, pitch, 0., 0)[0] == captured[1][0], 'cache/direct facing mismatch'
            integer(game, 'mc_render_cache_ready').value = 1

            def render_stream(stream, camera, mode):
                matrix = native.Transform(); C.memmove(C.addressof(matrix), camera, 72)
                draws = []
                for row in rows(stream):
                    values = struct.unpack('<20f2I', row); vertices = (native.Vertex * 4)()
                    for corner in range(4):
                        for axis, field in enumerate(('x', 'y', 'z', 'u', 'v')):
                            setattr(vertices[corner], field, values[corner * 5 + axis])
                    draws.append(native.make_draw(vertices, asset=values[20], mask=values[21], matrix=matrix))
                renderer.probe_cull_invalidate()
                # Preserve actual native API capacity boundaries, not one huge list.
                batches = [draws[i:i + 256] for i in range(0, len(draws), 256)]
                return native.render(renderer, initial, 7, batches, textures,
                    mode=mode, live_worker=True, pristine=True, diagnostics=1025)

            outputs = [render_stream(stream, camera, 258 if cull == 3 else 2)
                       for cull, (stream, camera) in zip((0, 1, 3), captured)]
            native.assert_equal(outputs[1], outputs[2], ('source-facing BF vs BF+occlusion', pitch))
            if pitch == terrain_pitch:
                assert [len(stream) // 88 for stream, _ in captured] == [2232, 1831, 1831]
                pixel = 48 * 320 + 250
                assert [i for i, (a, b) in enumerate(zip(outputs[0][0], outputs[1][0])) if a != b] == [pixel]
                assert (outputs[0][0][pixel], outputs[1][0][pixel]) == (0x92, 0x49)
                assert (struct.unpack_from('<I', outputs[0][1], pixel * 4)[0],
                        struct.unpack_from('<I', outputs[1][1], pixel * 4)[0]) == (4246590208, 4246590464)
                assert sum(a != b for a, b in zip(outputs[0][1], outputs[1][1])) == 1
                assert outputs[0][2] == outputs[1][2]
                original_rows = rows(captured[0][0]); top = original_rows[1152]
                values = struct.unpack('<20f2I', top)
                xyz = [values[c * 5:c * 5 + 3] for c in range(4)]
                assert xyz == [(7., 74., 20.), (7., 74., 19.), (6., 74., 19.), (6., 74., 20.)]
                # Cross(edge01, edge02)=(0,+1,0): this is source side 1/top,
                # viewed from BELOW y=74. Removing it is source-faithful.
                assert pos[1] < 74. and top not in rows(captured[1][0])
                styles = [geometry[q * 21 + 20] for q in range(geometry_count)
                          if geometry_bytes[q * 84:q * 84 + 80] == top[:80]]
                assert styles and all((style & (512 | 7)) == (512 | 1) for style in styles), styles
                # Removing this one proven backface alone explains the entire
                # observed image/depth difference; no other lost surface does.
                without_top = b''.join(original_rows[:1152] + original_rows[1153:])
                native.assert_equal(outputs[1], render_stream(without_top, captured[0][1], 2),
                                    'only the known backface explains the old seam pixel')
                print('PASS source-correct top-face removal: sole old seam pixel (250,48), 0x92 -> 0x49; depth delta 256', flush=True)
            else:
                native.assert_equal(outputs[0], outputs[1], 'saved straight-up sky pose')
        print('PASS exact saved terrain+sky: cache/direct facing; native BF/BF+occlusion color/depth/stamp parity')
        import _ctypes
        _ctypes.FreeLibrary(renderer._handle); _ctypes.FreeLibrary(game._handle)
    if runtime:
        runtime.close()


if __name__ == '__main__':
    run()
