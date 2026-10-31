"""Early full-cube rejection: exact source order, eligibility and pixel parity.

Uses the complete production native renderer, not a software raster substitute.
Native compilation is one optional serialized step; this is not an MCU emulator.
"""
import argparse, ctypes as C, os, random, shutil, struct, subprocess, sys, tempfile
from pathlib import Path
PROJECT=Path(os.environ.get('MC10_TEST_PROJECT',str(Path(__file__).resolve().parents[1])))
sys.path.insert(0,str(PROJECT/'tests'))
from test_render_cache import load,trace,integer,Player,pair,bits,Options
RENDERER=Path(os.environ.get('MC10_TEST_RENDERER',str(Path(__file__).resolve().parents[1]/'src/mc10_render.tc')))
FIRMWARE_TESTS=Path(os.environ.get('MC10_TERRAIN_TESTS',str(PROJECT.parents[2]/'tests/terrain')))
BASE_NATIVE=PROJECT/'mc10_narm_build24_preparation_fixes/firmware_source/graphics_3d_runtime.cpp'

def hash_name(name):
    value=2166136261
    for byte in name.encode():value=((value^byte)*16777619)&0xffffffff
    return value

def packed_image(name):
    data=(PROJECT/'MC10/assets.mc10').read_bytes();header=struct.unpack_from('<8I',data)
    assert data[:4]==b'MCA1'
    for i in range(header[2]):
        r=struct.unpack_from('<10I',data,header[4]+i*40)
        if r[0]==hash_name(name):return data[r[5]:r[5]+r[6]],data[r[7]:r[7]+r[8]]
    raise AssertionError(('missing owned asset',name))

def rows(stream):return [stream[i:i+88] for i in range(0,len(stream),88)]
def subsequence(original,candidate):
    source=iter(rows(original))
    assert all(any(item==wanted for item in source) for wanted in rows(candidate)),'primitive order/attributes changed'

def run(native_source=None,native_dll=None,native_occlusion=False):
    with tempfile.TemporaryDirectory(prefix='mc10-backface-',ignore_cleanup_errors=True) as tmp:
        root=Path(tmp);(root/'renderer_under_test.tc').write_text('#include "'+RENDERER.as_posix()+'"\n')
        dll=root/'game.dll'
        built=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(Path(__file__).with_name('backface_fixture.c')),
                              '-I',str(root),'-I',str(PROJECT/'tests'),'-I',str(PROJECT),'-o',str(dll)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        game=C.CDLL(str(dll));load(game,PROJECT/'MC10/saves/New World')
        terrain,mask=packed_image('terrain.png');grass,_=packed_image('misc/grasscolor.png')
        pixels=(C.c_uint32*16384).in_dll(game,'mc_asset_pixels');mask_words=(C.c_uint32*2048).in_dll(game,'mc_asset_mask')
        assert len(terrain)==65536 and len(mask)==8192
        C.memmove(C.addressof(pixels),terrain,len(terrain));C.memmove(C.addressof(mask_words),mask,len(mask))
        C.c_uint32.in_dll(game,'mc_render_grass_color').value=grass[127*256+127]
        game.mc_render_apply_grass_tint();game.mc_render_prepare_light_atlas()
        assert game.mc_render_build_cache()==1
        geometry=(C.c_uint32*(12288*21)).in_dll(game,'mc_render_geometry')
        n=integer(game,'mc_render_geometry_count').value
        eligible=sum(bool(geometry[q*21+20]&512) for q in range(n));assert eligible>0
        # Every tagged quad is planar in the axis its exact source-side names.
        for q in range(n):
            style=geometry[q*21+20]
            if style&512:
                side=style&7;assert side<6;axis=1 if side<2 else 2 if side<4 else 0
                assert len({geometry[q*21+c*5+axis] for c in range(4)})==1
        p=Player.in_dll(game,'mc_runtime_player');cx=integer(game,'mc_runtime_center_x').value;cz=integer(game,'mc_runtime_center_z').value
        rng=random.Random(2600);cases=[];removed=0
        C.c_uint32.in_dll(game,'fixture_clock_increment').value=1
        for cached in (0,1):
            integer(game,'mc_render_cache_ready').value=cached
            for sample in range(24):
                p.pos_x=pair(cx*16+rng.uniform(-15.5,31.5));p.pos_y=pair(rng.uniform(45,90));p.pos_z=pair(cz*16+rng.uniform(-15.5,31.5))
                yaw,pitch=rng.uniform(-360,360),rng.uniform(-89.9,89.9);gamma,fancy=(sample%3)*.5,sample&1
                integer(game,'mc_render_submit_optimized').value=1;integer(game,'mc_render_list_enabled').value=1
                integer(game,'mc_render_cull_mode').value=0;original=trace(game,yaw,pitch,gamma,fancy)[0]
                camera=bytes((C.c_float*18).in_dll(game,'mc_render_camera'))
                integer(game,'mc_render_cull_mode').value=1;integer(game,'mc_render_cull_diagnostics').value=1
                candidate=trace(game,yaw,pitch,gamma,fancy)[0];subsequence(original,candidate)
                if not integer(game,'mc_render_cull_frame_allowed').value:assert candidate==original,('opaque/unknown eye',cached,sample)
                assert integer(game,'mc_render_cull_backfaces').value==(len(original)-len(candidate))//88
                removed+=(len(original)-len(candidate))//88
                # Native occlusion mode selection alone cannot change this
                # host adapter's emitted command stream.
                integer(game,'mc_render_cull_mode').value=3
                assert trace(game,yaw,pitch,gamma,fancy)[0]==candidate
                integer(game,'mc_render_cull_diagnostics').value=0
                assert trace(game,yaw,pitch,gamma,fancy)[0]==candidate
                assert integer(game,'mc_render_cull_backfaces').value==integer(game,'mc_render_cull_eligible').value==0
                if cached:cases.append((f'owned-{sample}',original,candidate,camera))
        assert removed>0
        # A camera just outside the resident region must retain the complete
        # old stream, even while looking inward at visible loaded terrain.
        p.pos_x=pair(cx*16-16.5);p.pos_y=pair(73);p.pos_z=pair(cz*16+8)
        integer(game,'mc_render_cull_mode').value=0;unknown=trace(game,-90,0,.5,1)[0]
        integer(game,'mc_render_cull_mode').value=1;assert trace(game,-90,0,.5,1)[0]==unknown
        assert integer(game,'mc_render_cull_frame_allowed').value==0
        # Isolated full cubes, adjacent opaque blocks, source grass overlays and
        # every implemented noneligible render type under multiple viewpoints.
        game.fixture_render_init(0,0)
        C.memset(C.addressof((C.c_char*(9*32768)).in_dll(game,'fixture_blocks')),0,9*32768)
        C.c_uint32.in_dll(game,'fixture_clock_increment').value=0
        t=(C.c_uint32*(65536*22)).in_dll(game,'fixture_trace')
        def captured():return bytes(t)[:integer(game,'fixture_trace_count').value*88]
        views=[(.5,64.5,1.,0.,0.),(.5,64.5,8.,180.,0.),(-3.,64.5,4.5,-90.,0.),
               (4.,64.5,4.5,90.,0.),(.5,68.,4.5,0.,89.99),(.5,61.,4.5,0.,-89.99),
               (.5,64.5,4.5,0.,0.),(.5,64.5,3.99,0.,0.),(.5,64.5,4.,0.,0.),
               (.5,64.5,4.001,0.,0.),(0.,66.,1.,0.,20.),(3.,67.,1.,40.,25.)]
        options=Options.in_dll(game,'mc_options');options.gamma=.5;options.fancy=1
        noneligible=(9,10,11,31,37,50,53,59,60,64,65,67,72,85,102)
        seen_noneligible=set()
        for id in (1,2,*noneligible):
            for vi,(x,y,z,yaw,pitch) in enumerate(views):
                p.pos_x=pair(x);p.pos_y=pair(y);p.pos_z=pair(z);p.yaw_bits=bits(yaw);p.pitch_bits=bits(pitch)
                metadata=2 if id==65 else 0
                integer(game,'mc_render_cull_mode').value=0;game.fixture_cull_scene(id,metadata,0);original=captured()
                camera=bytes((C.c_float*18).in_dll(game,'mc_render_camera'))
                integer(game,'mc_render_cull_mode').value=1;game.fixture_cull_scene(id,metadata,0);candidate=captured()
                if id in noneligible:
                    assert candidate==original,('noneligible changed',id,vi)
                    if original:seen_noneligible.add(id)
                else:
                    subsequence(original,candidate)
                    if vi in (6,7,8,9):assert candidate==original,('inside/near changed',id,vi)
                    cases.append((f'cube-{id}-{vi}',original,candidate,camera))
        assert seen_noneligible==set(noneligible),('untested noneligible',set(noneligible)-seen_noneligible)
        game.fixture_cull_neighbors(1)
        for vi in (0,2,4,5,6,10,11):
            x,y,z,yaw,pitch=views[vi]
            p.pos_x=pair(x);p.pos_y=pair(y);p.pos_z=pair(z);p.yaw_bits=bits(yaw);p.pitch_bits=bits(pitch)
            integer(game,'mc_render_cull_mode').value=0;game.fixture_cull_scene(1,0,1);original=captured()
            camera=bytes((C.c_float*18).in_dll(game,'mc_render_camera'))
            integer(game,'mc_render_cull_mode').value=1;game.fixture_cull_scene(1,0,1);candidate=captured()
            subsequence(original,candidate)
            if vi==6:assert candidate==original and integer(game,'mc_render_cull_frame_allowed').value==0
            cases.append((f'opaque-neighbor-{vi}',original,candidate,camera))
        game.fixture_cull_neighbors(0)
        # Direct scan fallback and tiny caches must reproduce the same filtered
        # source stream, independent of which complete groups fit the pool.
        load(game,PROJECT/'MC10/saves/New World');integer(game,'mc_render_cull_mode').value=1
        for capacity in (0,1,17,12288):
            integer(game,'mc_render_geometry_limit').value=capacity;game.mc_render_build_cache()
            integer(game,'mc_render_cache_ready').value=0;original=trace(game,35,15,.5,1)[0]
            integer(game,'mc_render_cache_ready').value=1;assert trace(game,35,15,.5,1)[0]==original
        game.mc_profile_cancel();integer(game,'mc_profile_enabled').value=0;game.mc_render_fps_reset()
        trace(game,0,15,.5,1)
        assert integer(game,'fixture_micros_calls').value==0 and integer(game,'fixture_hud_calls').value==1
        # All native pixel checks use the owned converted atlas and mask. No
        # synthetic rasterizer or all-zero textures can hide an error here.
        pixel_cases=0
        if native_source or native_dll:
            sys.path.insert(0,str(FIRMWARE_TESTS));import test_textured_tiles as native
            if native_dll:
                runtime_search=os.add_dll_directory(str(Path(shutil.which('g++')).parent)) if os.name=='nt' else None
                renderer=C.CDLL(str(native_dll.resolve()))
                signatures={
                    'worker':([C.c_bool],None),'scratch':([C.c_bool],None),
                    'diagnostics':([native.WORD],None),'pristine':([native.WORD],None),
                    'nonpristine':([],None),'end':([],C.c_bool),
                    'begin':([native.PTR,native.PTR,native.PTR,native.WORD,native.WORD],C.c_bool),
                    'draw':([native.PTR,native.PTR,native.PTR,native.WORD,C.POINTER(native.Vertex),native.WORD,C.POINTER(native.Transform),native.PTR,C.c_int32,C.c_int32,native.WORD,C.c_bool,native.PTR],C.c_bool),
                }
                for name,(args,result) in signatures.items():
                    fn=getattr(renderer,'probe_'+name);fn.argtypes=args;fn.restype=result
            else:
                native_dir=root/'native';native_dir.mkdir();renderer=native.build(native_source,native_dir)
            atlas=(C.c_uint32*(16*16384)).in_dll(game,'mc_render_light_atlas')
            atlas_bytes=bytes(atlas);mask_guard=native.Guarded(bytes(mask_words))
            textures=[(256,256,native.Guarded(atlas_bytes[level*65536:(level+1)*65536]),[None,mask_guard]) for level in range(16)]
            initial=(bytes(native.PIXELS),bytes(native.PIXELS*4),bytes(native.PIXELS))
            invalidate=getattr(renderer,'probe_cull_invalidate',None)
            if invalidate:invalidate.argtypes=[];invalidate.restype=None
            for name,original,candidate,camera in cases:
                matrix=native.Transform();C.memmove(C.addressof(matrix),camera,72)
                def draws(stream):
                    result=[]
                    for row in rows(stream):
                        values=struct.unpack('<20f2I',row);vertices=(native.Vertex*4)()
                        for corner in range(4):
                            for axis,field in enumerate(('x','y','z','u','v')):setattr(vertices[corner],field,values[corner*5+axis])
                        result.append(native.make_draw(vertices,asset=values[20],mask=values[21],matrix=matrix))
                    return [result]
                # Host cases replace buffers/frame IDs directly; production
                # half-clear performs this hierarchy invalidation itself.
                if invalidate:invalidate()
                expected=native.render(renderer,initial,7,draws(original),textures,mode=1,pristine=True)
                for mode in ((257,258) if native_occlusion else (1,)):
                    if invalidate:invalidate()
                    actual=native.render(renderer,initial,7,draws(candidate),textures,mode=mode,pristine=True,live_worker=mode==258)
                    native.assert_equal(expected,actual,(name,mode));pixel_cases+=1
            import _ctypes;_ctypes.FreeLibrary(renderer._handle)
        print(f'Backface PASS: 48 owned-world subsequences; {removed} removed faces; full-cube tags/planes; 15 noneligible blocks unchanged; 24 cube + 7 opaque-neighbor near/inside/tie views; opaque/unknown-eye fallback; tiny/cache/scan fallback, counters/FPS-only; {pixel_cases} actual native color/depth/stamp scenes')
        import _ctypes;_ctypes.FreeLibrary(game._handle)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--native-source',type=Path,nargs='?',const=BASE_NATIVE)
    parser.add_argument('--native-dll',type=Path,help='Reuse an already built native fixture for serialized development checks')
    parser.add_argument('--native-occlusion',action='store_true',help='Compare combined backface + native occlusion in one- and two-worker modes')
    args=parser.parse_args();run(args.native_source,args.native_dll,args.native_occlusion)
