"""Compare every float/UV/mask/light/order emitted by original and list paths."""
import ctypes
import os
import random
import subprocess
import tempfile
from pathlib import Path
from test_render_cache import load,trace,integer,Player,pair
PROJECT=Path(os.environ.get('MC10_TEST_PROJECT',str(Path(__file__).resolve().parents[1])))
RENDERER=Path(os.environ.get('MC10_TEST_RENDERER',str(PROJECT/'src/mc10_render.tc')))

def run():
    with tempfile.TemporaryDirectory(prefix='mc10-list-',ignore_cleanup_errors=True) as tmp:
        root=Path(tmp);dll=root/'list.dll'
        (root/'renderer_under_test.tc').write_text('#include "'+RENDERER.as_posix()+'"\n')
        fixture=Path(os.environ.get('MC10_TEST_LIST_FIXTURE',str(PROJECT/'tests/textured_list_fixture.c')))
        built=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(fixture),'-I',str(root),'-I',str(PROJECT/'tests'),'-I',str(PROJECT),'-o',str(dll)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        lib=ctypes.CDLL(str(dll));load(lib,Path(os.environ.get('MC10_TEST_WORLD',str(PROJECT/'MC10/saves/New World'))))
        assert lib.mc_render_build_cache()==1
        integer(lib,'mc_render_cull_mode').value=0
        # Cached replay owns one invariant descriptor template. All production
        # writers (including the dynamic queue) use the same 256x256, zero-flag
        # four-vertex format; the one-time template must survive their reuse.
        commands=(ctypes.c_uint32*(256*7)).in_dll(lib,'mc_render_list_commands')
        def check_template():
            assert integer(lib,'mc_render_list_template_ready').value==1
            for slot in range(256):
                assert tuple(commands[slot*7+3:slot*7+7])==(256,256,0,4),('template',slot)
        ctypes.c_uint32.in_dll(lib,'fixture_clock_increment').value=1
        rng=random.Random(2100);p=Player.in_dll(lib,'mc_runtime_player');cx=integer(lib,'mc_runtime_center_x').value;cz=integer(lib,'mc_runtime_center_z').value
        cases=0
        for cached in (0,1):
            integer(lib,'mc_render_cache_ready').value=cached
            for sample in range(32):
                p.pos_x=pair(cx*16+rng.uniform(-15.5,31.5));p.pos_y=pair(rng.uniform(50,85));p.pos_z=pair(cz*16+rng.uniform(-15.5,31.5))
                yaw,pitch=rng.uniform(-360,360),rng.uniform(-89.9,89.9);gamma,fancy=(sample%3)*.5,sample&1
                integer(lib,'mc_render_submit_optimized').value=0
                integer(lib,'mc_render_list_enabled').value=0;original=trace(lib,yaw,pitch,gamma,fancy)[0]
                expected=[integer(lib,n).value for n in ('mc_render_faces','mc_render_blocks_drawn','mc_render_unsupported','mc_render_missing_registry','mc_render_atlas_variant')]
                for optimized,batched in ((0,1),(1,0),(1,1)):
                    integer(lib,'mc_render_submit_optimized').value=optimized
                    integer(lib,'mc_render_list_enabled').value=batched
                    assert original==trace(lib,yaw,pitch,gamma,fancy)[0],('quad stream changed',cached,sample,optimized,batched)
                    assert expected==[integer(lib,n).value for n in ('mc_render_faces','mc_render_blocks_drawn','mc_render_unsupported','mc_render_missing_registry','mc_render_atlas_variant')],('render counters/state changed',cached,sample,optimized,batched)
                    assert integer(lib,'mc_render_list_count').value==integer(lib,'mc_render_list_error').value==0
                cases+=1
        assert integer(lib,'fixture_list_cached').value>0 and integer(lib,'fixture_list_copied').value>0
        check_template()
        # An unbounded host clock drives batches to their capacity, ensuring
        # copied dynamic vertices survive automatic flush-and-reuse boundaries.
        ctypes.c_uint32.in_dll(lib,'fixture_clock_increment').value=0
        integer(lib,'mc_render_cache_ready').value=0
        p.pos_x=pair(cx*16+8);p.pos_y=pair(73);p.pos_z=pair(cz*16+8)
        integer(lib,'mc_render_submit_optimized').value=0;integer(lib,'mc_render_list_enabled').value=0;original=trace(lib,0,15,.5,1)[0]
        for optimized in (0,1):
            integer(lib,'mc_render_submit_optimized').value=optimized
            integer(lib,'mc_render_list_enabled').value=1;assert trace(lib,0,15,.5,1)[0]==original
        assert integer(lib,'fixture_list_max').value==256
        check_template()
        # An already pending dynamic command precedes cached replay. Reusing
        # that queue must preserve the original order and batch fields, even
        # when cached replay fills slot 255 and flushes during the group.
        integer(lib,'mc_render_cache_ready').value=1
        ranges=(ctypes.c_uint32*32768).in_dll(lib,'mc_render_geometry_range')
        geometry=(ctypes.c_uint32*(12288*21)).in_dll(lib,'mc_render_geometry')
        group_range=next(r for r in ranges if r&0x80000000 and ((r&0x7fffffff)>>14)>1)
        first=group_range&16383
        one=0x80000000|first|(1<<14)
        integer(lib,'mc_render_submit_optimized').value=0
        integer(lib,'mc_render_list_enabled').value=0
        trace(lib,0,15,.5,1)
        integer(lib,'fixture_trace_count').value=0
        lib.mc_render_replay_geometry(one)
        lib.mc_render_replay_geometry(group_range)
        trace_array=(ctypes.c_uint32*(65536*22)).in_dll(lib,'fixture_trace')
        expected=bytes(trace_array)[:integer(lib,'fixture_trace_count').value*88]
        integer(lib,'mc_render_submit_optimized').value=1
        integer(lib,'mc_render_list_enabled').value=1
        integer(lib,'fixture_trace_count').value=0
        lib.mc_render_replay_geometry_fast(one)
        # The pending command is exactly the same format as a dynamic queue
        # write; explicitly rewrite its invariant fields through that writer.
        integer(lib,'mc_render_list_count').value=0
        vertex=ctypes.cast(ctypes.addressof(geometry)+first*84,ctypes.POINTER(ctypes.c_float))
        lib.mc_render_list_queue.argtypes=[ctypes.POINTER(ctypes.c_float),ctypes.c_int]
        lib.mc_render_list_queue(vertex,(geometry[first*21+20]>>7)&1)
        lib.mc_render_replay_geometry_fast(group_range)
        lib.mc_render_list_flush()
        assert expected==bytes(trace_array)[:integer(lib,'fixture_trace_count').value*88]
        check_template()
        # Fill 255 pending dynamic commands, then cross capacity inside one
        # cached group. The renderer may flush here but cannot overwrite them.
        integer(lib,'fixture_trace_count').value=0
        integer(lib,'mc_render_list_enabled').value=0
        for pending in range(255):lib.mc_render_replay_geometry(one)
        lib.mc_render_replay_geometry(group_range)
        expected=bytes(trace_array)[:integer(lib,'fixture_trace_count').value*88]
        integer(lib,'fixture_trace_count').value=0
        integer(lib,'mc_render_list_enabled').value=1
        lib.mc_render_replay_geometry_fast(one)
        integer(lib,'mc_render_list_count').value=0
        for pending in range(255):lib.mc_render_list_queue(vertex,(geometry[first*21+20]>>7)&1)
        lib.mc_render_replay_geometry_fast(group_range);lib.mc_render_list_flush()
        assert expected==bytes(trace_array)[:integer(lib,'fixture_trace_count').value*88]
        check_template()
        # Tiny geometry capacities retain whole-block rollback, including a
        # completely empty pool, while the surface cache stays available.
        integer(lib,'mc_render_cache_ready').value=1
        for capacity in (0,1,17):
            integer(lib,'mc_render_geometry_limit').value=capacity;lib.mc_render_build_geometry()
            assert integer(lib,'mc_render_geometry_count').value<=capacity
            for gamma,fancy in ((0,0),(.5,1),(1,1)):
                integer(lib,'mc_render_submit_optimized').value=0;baseline=trace(lib,35,15,gamma,fancy)[0]
                integer(lib,'mc_render_submit_optimized').value=1;assert trace(lib,35,15,gamma,fancy)[0]==baseline,('tiny geometry pool',capacity,gamma,fancy)
        integer(lib,'mc_render_geometry_limit').value=12288;lib.mc_render_build_geometry()
        # A completed zero-quad group is legal, even when its start is exactly
        # the full pool's end. It must not access the center array at that end.
        ranges=(ctypes.c_uint32*32768).in_dll(lib,'mc_render_geometry_range')
        saved_range=ranges[0]
        try:
            for empty_range in (0x80000000,0x80000000|12288):
                ranges[0]=empty_range
                integer(lib,'mc_render_submit_optimized').value=0;baseline=trace(lib,0,15,.5,1)[0]
                expected=integer(lib,'mc_render_blocks_drawn').value
                integer(lib,'mc_render_submit_optimized').value=1;assert trace(lib,0,15,.5,1)[0]==baseline
                assert integer(lib,'mc_render_blocks_drawn').value==expected
        finally:ranges[0]=saved_range
        # If the origin changes independently of a geometry rebuild, cached
        # centers must be bypassed rather than culling using stale positions.
        ox=integer(lib,'mc_render_geometry_origin_x');saved_origin=ox.value
        ox.value=saved_origin+16
        integer(lib,'mc_render_submit_optimized').value=0;baseline=trace(lib,65,5,1,0)[0]
        integer(lib,'mc_render_submit_optimized').value=1;assert trace(lib,65,5,1,0)[0]==baseline
        ox.value=saved_origin
        # Surface overflow must remain the complete direct scan fallback.
        integer(lib,'mc_render_cache_limit').value=1;assert lib.mc_render_build_cache()==0
        integer(lib,'mc_render_submit_optimized').value=0;baseline=trace(lib,35,15,.5,1)[0]
        integer(lib,'mc_render_submit_optimized').value=1;assert trace(lib,35,15,.5,1)[0]==baseline
        integer(lib,'mc_render_cache_limit').value=32768;assert lib.mc_render_build_cache()==1
        original=trace(lib,0,15,.5,1)[0]
        lib.mc_profile_cancel();integer(lib,'mc_profile_enabled').value=0
        lib.mc_render_fps_reset()
        assert trace(lib,0,15,.5,1)[0]==original
        assert integer(lib,'fixture_micros_calls').value==0
        assert integer(lib,'fixture_hud_calls').value==1
        assert integer(lib,'mc_render_frame_batches').value==0
        assert integer(lib,'mc_render_groups_visited').value==0
        assert integer(lib,'mc_render_groups_skipped').value==0
        integer(lib,'mc_profile_enabled').value=1
        for optimized in (0,1):
            integer(lib,'mc_render_submit_optimized').value=optimized
            integer(lib,'mc_render_list_error').value=0;integer(lib,'fixture_list_fail').value=0
            lib.mc_render_begin_frame();assert lib.mc_render_step_frame()==-1
            assert integer(lib,'mc_render_list_error').value==-4 and integer(lib,'mc_render_list_count').value==0
        print(f'Textured list PASS: {cases+1} exact full quad streams across original/optimized and immediate/list paths (all vertex bits, UVs, mask, light, order), counters/state, cached references/dynamic copies, one-time descriptor template reused after dynamic commands and pending-255 capacity crossing, tiny/empty/full geometry groups, stale-origin guard, 256-command flush boundary, FPS-only mode, and explicit service failure')
        import _ctypes;_ctypes.FreeLibrary(lib._handle)

if __name__=='__main__':run()
