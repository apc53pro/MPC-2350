"""Exact retained/indexed game submission parity, memory and queue contracts."""
import ctypes, os, random, struct, subprocess, sys, tempfile
from pathlib import Path

PROJECT=Path(os.environ.get('MC10_TEST_PROJECT',str(Path(__file__).resolve().parents[1])))
sys.path.insert(0,str(PROJECT/'tests'))
from test_render_cache import load,trace,integer,Player,pair
RENDERER=Path(os.environ.get('MC10_TEST_RENDERER',str(Path(__file__).resolve().parents[1]/'src/mc10_render.tc')))

def run():
    with tempfile.TemporaryDirectory(prefix='mc10-mesh-',ignore_cleanup_errors=True) as tmp:
        root=Path(tmp);dll=root/'mesh.dll'
        (root/'renderer_under_test.tc').write_text('#include "'+RENDERER.as_posix()+'"\n')
        built=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(Path(__file__).with_name('textured_list_fixture.c')),
                              '-I',str(root),'-I',str(PROJECT/'tests'),'-I',str(PROJECT),'-o',str(dll)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        lib=ctypes.CDLL(str(dll));load(lib,PROJECT/'MC10/saves/New World')
        assert lib.mc_render_build_cache()==1
        assert integer(lib,'mc_render_mesh_ready').value==1
        geometry=(ctypes.c_uint32*(12288*21)).in_dll(lib,'mc_render_geometry')
        indices=(ctypes.c_uint32*(12288*2)).in_dll(lib,'mc_render_mesh_indices')
        fancy_counts=(ctypes.c_uint32*12288).in_dll(lib,'mc_render_mesh_fancy_count')
        ranges=(ctypes.c_uint32*32768).in_dll(lib,'mc_render_geometry_range')
        count=integer(lib,'mc_render_geometry_count').value
        def check_indices():
            hits=0
            for q in range(count):
                for c in range(4):
                    ref=(indices[q*2+c//2]>>((c&1)*16))&65535
                    assert ref<count*4
                    actual=tuple(geometry[(ref//4)*21+(ref&3)*5:(ref//4)*21+(ref&3)*5+3])
                    assert actual==tuple(geometry[q*21+c*5:q*21+c*5+3])
                    hits+=ref!=q*4+c
            return hits
        hits=check_indices();assert hits>count
        expected_indices=bytes(indices);lib.mc_render_mesh_build();assert bytes(indices)==expected_indices
        for value in ranges:
            if value&0x80000000 and (value&0x7fffc000):
                first=value&16383;n=(value&0x7fffffff)>>14
                assert fancy_counts[first]&16383==sum(bool(geometry[q*21+20]&256) for q in range(first,first+n))
                assert fancy_counts[first]>>14==n
        # Compare bit keys, not float equality: +0 and -0 must remain distinct.
        saved=list(geometry[:42])
        try:
            geometry[0:3]=(0,0,0);geometry[5:8]=(0x80000000,0,0)
            geometry[21:24]=(0,0,0);geometry[26:29]=(0x80000000,0,0)
            lib.mc_render_mesh_build();check_indices()
            assert indices[0]&65535==0 and indices[0]>>16==1
            assert indices[2]&65535!=indices[2]>>16
        finally:
            geometry[:42]=saved;lib.mc_render_mesh_build()
        ctypes.c_uint32.in_dll(lib,'fixture_clock_increment').value=1
        p=Player.in_dll(lib,'mc_runtime_player');cx=integer(lib,'mc_runtime_center_x').value;cz=integer(lib,'mc_runtime_center_z').value
        rng=random.Random(2500);cases=0
        for cached in (0,1):
            integer(lib,'mc_render_cache_ready').value=cached
            for sample in range(32):
                p.pos_x=pair(cx*16+rng.uniform(-15.5,31.5));p.pos_y=pair(rng.uniform(40,95));p.pos_z=pair(cz*16+rng.uniform(-15.5,31.5))
                yaw,pitch=rng.uniform(-360,360),rng.uniform(-89.9,89.9);gamma,fancy=(sample%3)*.5,sample&1
                integer(lib,'mc_render_submit_optimized').value=0;integer(lib,'mc_render_mesh_enabled').value=0
                integer(lib,'mc_render_list_enabled').value=0;original=trace(lib,yaw,pitch,gamma,fancy)[0]
                names=('mc_render_faces','mc_render_blocks_drawn','mc_render_unsupported','mc_render_missing_registry','mc_render_atlas_variant')
                expected=[integer(lib,n).value for n in names]
                for submit,mesh in ((0,0),(1,0),(1,1)):
                    integer(lib,'mc_render_submit_optimized').value=submit;integer(lib,'mc_render_mesh_enabled').value=mesh
                    integer(lib,'mc_render_list_enabled').value=1
                    assert original==trace(lib,yaw,pitch,gamma,fancy)[0],('quad stream',cached,sample,submit,mesh)
                    assert expected==[integer(lib,n).value for n in names],('counters/state',cached,sample,submit,mesh)
                    assert integer(lib,'mc_render_mesh_range_count').value==integer(lib,'mc_render_list_count').value==integer(lib,'mc_render_list_error').value==0
                cases+=1
        # Material refresh is outside submission and follows exact light lookup,
        # mask presence and Fancy changes, including Gamma from zero to one.
        materials=(ctypes.c_uint32*(512*6)).in_dll(lib,'mc_render_mesh_materials')
        lookup=(ctypes.c_uint32*128).in_dll(lib,'mc_render_light_lookup')
        atlas=ctypes.addressof((ctypes.c_uint32*(16*16384)).in_dll(lib,'mc_render_light_atlas'))&0xffffffff
        mask=ctypes.addressof((ctypes.c_uint32*2048).in_dll(lib,'mc_asset_mask'))&0xffffffff
        mask_bytes=ctypes.c_uint32.in_dll(lib,'mc_asset_loaded_mask_bytes')
        integer(lib,'mc_render_cache_ready').value=1
        for gamma,fancy,masked in ((0,0,0),(1,1,8192),(.5,0,8192),(0,1,0)):
            mask_bytes.value=masked;trace(lib,0,15,gamma,fancy)
            for style in range(512):
                assert materials[style*6]==(atlas+lookup[style&127]*65536)&0xffffffff
                assert materials[style*6+1]==(mask if masked and style&128 else 0)
                assert tuple(materials[style*6+2:style*6+5])==(256,256,0)
                assert materials[style*6+5]==(not(style&256) or bool(fancy))
        mask_bytes.value=8192
        # Unbounded synthetic time reaches raw-face capacity rather than
        # time-budget flushes. Both material modes remain exact.
        ctypes.c_uint32.in_dll(lib,'fixture_clock_increment').value=0
        p.pos_x=pair(cx*16+8);p.pos_y=pair(73);p.pos_z=pair(cz*16+8)
        for fancy in (0,1):
            integer(lib,'mc_render_mesh_enabled').value=0;base=trace(lib,0,15,.5,fancy)[0]
            integer(lib,'mc_render_mesh_enabled').value=1;assert trace(lib,0,15,.5,fancy)[0]==base
        assert integer(lib,'fixture_mesh_max_raw').value<=256
        trace_array=(ctypes.c_uint32*(65536*22)).in_dll(lib,'fixture_trace')
        def captured():return bytes(trace_array)[:integer(lib,'fixture_trace_count').value*88]
        def reset():
            lib.mc_render_list_flush();integer(lib,'fixture_trace_count').value=0
        value=next(r for r in ranges if r&0x80000000 and ((r&0x7fffffff)>>14)>1)
        first=value&16383;one=0x80000000|first|(1<<14)
        vertex=ctypes.cast(ctypes.addressof(geometry)+first*84,ctypes.POINTER(ctypes.c_float))
        lib.mc_render_list_queue.argtypes=[ctypes.POINTER(ctypes.c_float),ctypes.c_int]
        masked=(geometry[first*21+20]>>7)&1
        # Pending dynamic -> retained and retained -> dynamic must flush in
        # submission order, even immediately adjacent to capacity boundaries.
        for direction in (0,1):
            reset();integer(lib,'mc_render_list_enabled').value=0
            if direction==0:
                for _ in range(255):lib.mc_render_replay_geometry(one)
                lib.mc_render_replay_geometry(value)
            else:
                lib.mc_render_replay_geometry(value)
                for _ in range(255):lib.mc_render_replay_geometry(one)
            expected=captured();reset();integer(lib,'mc_render_list_enabled').value=1
            if direction==0:
                integer(lib,'mc_render_atlas_variant').value=lookup[geometry[first*21+20]&127]
                for _ in range(255):lib.mc_render_list_queue(vertex,masked)
                lib.mc_render_replay_geometry_mesh(value)
            else:
                lib.mc_render_replay_geometry_mesh(value)
                integer(lib,'mc_render_atlas_variant').value=lookup[geometry[first*21+20]&127]
                for _ in range(255):lib.mc_render_list_queue(vertex,masked)
            lib.mc_render_list_flush();assert captured()==expected,('mixed queue',direction)
        # Range capacity and coalescing do not reorder duplicate source faces.
        reset();integer(lib,'mc_render_list_enabled').value=0
        for _ in range(257):lib.mc_render_replay_geometry(one)
        expected=captured();reset();integer(lib,'mc_render_list_enabled').value=1
        for _ in range(257):lib.mc_render_replay_geometry_mesh(one)
        lib.mc_render_list_flush();assert captured()==expected
        # Adjacent ranges coalesce only when their source ordinals are truly
        # contiguous; repeated/overlapping ranges above remain distinct.
        reset();integer(lib,'mc_render_list_enabled').value=0
        lib.mc_render_replay_geometry(one);lib.mc_render_replay_geometry(0x80000000|(first+1)|(1<<14))
        expected=captured();reset();integer(lib,'mc_render_list_enabled').value=1
        lib.mc_render_replay_geometry_mesh(one);lib.mc_render_replay_geometry_mesh(0x80000000|(first+1)|(1<<14))
        assert integer(lib,'mc_render_mesh_range_count').value==1
        assert integer(lib,'mc_render_mesh_raw_count').value==2
        lib.mc_render_list_flush();assert captured()==expected
        # A disabled Fancy-only partial range neither changes atlas state nor
        # leaves a zero-work native call pending.
        fancy_quad=next(q for q in range(count) if geometry[q*21+20]&256)
        trace(lib,0,15,.5,0);reset();saved_atlas=integer(lib,'mc_render_atlas_variant').value
        lib.mc_render_replay_geometry_mesh(0x80000000|fancy_quad|(1<<14))
        assert integer(lib,'mc_render_mesh_range_count').value==0 and captured()==b''
        assert integer(lib,'mc_render_atlas_variant').value==saved_atlas
        # Arbitrary large legal source ranges are split to the native raw-face
        # limit while preserving both filtered and unfiltered output streams.
        for fancy in (0,1):
            trace(lib,0,15,.5,fancy);reset();integer(lib,'mc_render_list_enabled').value=0
            wide=0x80000000|(600<<14);lib.mc_render_replay_geometry(wide);expected=captured()
            reset();integer(lib,'mc_render_list_enabled').value=1;lib.mc_render_replay_geometry_mesh(wide)
            lib.mc_render_list_flush();assert captured()==expected
        # Full/empty pools, rollback, stale origin and complete scan fallback.
        for capacity in (0,1,17):
            integer(lib,'mc_render_geometry_limit').value=capacity;lib.mc_render_build_geometry()
            assert integer(lib,'mc_render_geometry_count').value<=capacity
            for gamma,fancy in ((0,0),(.5,1),(1,1)):
                integer(lib,'mc_render_mesh_enabled').value=0;base=trace(lib,35,15,gamma,fancy)[0]
                integer(lib,'mc_render_mesh_enabled').value=1;assert trace(lib,35,15,gamma,fancy)[0]==base
        integer(lib,'mc_render_geometry_limit').value=12288;lib.mc_render_build_geometry()
        saved_range=ranges[0]
        try:
            for empty in (0x80000000,0x80000000|12288):
                ranges[0]=empty;integer(lib,'mc_render_mesh_enabled').value=0;base=trace(lib,0,15,.5,1)[0]
                integer(lib,'mc_render_mesh_enabled').value=1;assert trace(lib,0,15,.5,1)[0]==base
                lib.mc_render_replay_geometry_mesh(empty)
        finally:ranges[0]=saved_range
        origin=integer(lib,'mc_render_geometry_origin_x');saved=origin.value;origin.value+=16
        integer(lib,'mc_render_mesh_enabled').value=0;base=trace(lib,65,5,1,0)[0]
        integer(lib,'mc_render_mesh_enabled').value=1;assert trace(lib,65,5,1,0)[0]==base
        origin.value=saved
        integer(lib,'mc_render_cache_limit').value=1;assert lib.mc_render_build_cache()==0
        assert integer(lib,'mc_render_mesh_ready').value==0
        integer(lib,'mc_render_mesh_enabled').value=0;base=trace(lib,35,15,.5,1)[0]
        integer(lib,'mc_render_mesh_enabled').value=1;assert trace(lib,35,15,.5,1)[0]==base
        integer(lib,'mc_render_cache_limit').value=32768;assert lib.mc_render_build_cache()==1
        original=trace(lib,0,15,.5,1)[0]
        lib.mc_profile_cancel();integer(lib,'mc_profile_enabled').value=0;lib.mc_render_fps_reset()
        assert trace(lib,0,15,.5,1)[0]==original
        assert integer(lib,'fixture_micros_calls').value==0 and integer(lib,'fixture_hud_calls').value==1
        assert integer(lib,'mc_render_frame_batches').value==integer(lib,'mc_render_groups_visited').value==integer(lib,'mc_render_groups_skipped').value==0
        integer(lib,'mc_profile_enabled').value=1
        # Native failures and enabled-count disagreements are explicit errors;
        # neither queue can remain pending when the caller handles the error.
        for wrong in (0,1):
            integer(lib,'mc_render_list_error').value=0
            integer(lib,'fixture_mesh_fail').value=0 if not wrong else -1
            integer(lib,'fixture_mesh_wrong_count').value=wrong
            lib.mc_render_begin_frame();result=0
            while not result:result=lib.mc_render_step_frame()
            assert result==-1 and integer(lib,'mc_render_list_error').value==-4
            assert integer(lib,'mc_render_mesh_range_count').value==integer(lib,'mc_render_list_count').value==0
        print(f'Retained/indexed mesh PASS: {cases} exact full streams; {hits} canonical shared positions; bitwise signed-zero/deterministic indices; Gamma/Fancy/mask materials; counters/atlas; mixed queues/capacity/coalescing; tiny/empty/full pools, stale origin/scan fallback, FPS-only mode, native errors/count mismatch')
        import _ctypes;_ctypes.FreeLibrary(lib._handle)

if __name__=='__main__':run()
