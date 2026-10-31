"""Combined Build29 mesh/daylight/cache/workspace regression, owned save read-only."""
import ctypes as C, hashlib, os, struct, subprocess, tempfile
from pathlib import Path
from test_render_cache import PROJECT,integer,load,trace,Options
def snapshot(lib):
    lib.fixture_geometry.restype=C.c_void_p
    count=integer(lib,'mc_render_geometry_count').value
    return C.string_at(lib.fixture_geometry(),count*84)
def draw(lib,yaw,pitch,gamma,fancy):
    stream=trace(lib,yaw,pitch,gamma,fancy)[0];n=len(stream)//88
    return stream,bytes((C.c_uint32*65536).in_dll(lib,'fixture_tint_trace'))[:n*4]
def run():
    with tempfile.TemporaryDirectory(prefix='mc10-world-render-',ignore_cleanup_errors=True)as tmp:
        tmp=Path(tmp);(tmp/'renderer_under_test.tc').write_text('#include "'+(PROJECT/'src/mc10_render.tc').as_posix()+'"\n');dll=tmp/'render.dll'
        built=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing','-ffp-contract=off',str(PROJECT/'tests/world_render_fixture.c'),'-I',str(tmp),'-I',str(PROJECT),'-o',str(dll)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        lib=C.CDLL(str(dll));root=(str(PROJECT/'MC10')+'\0').encode();C.memmove((C.c_char*1024).in_dll(lib,'fixture_mc10_root'),root,len(root))
        load(lib,PROJECT/'MC10/saves/New World');lib.mc_fp_init();assert lib.mc_block_mesh_open()==1
        lib.fixture_geometry.restype=lib.fixture_workspace.restype=C.c_void_p
        assert lib.fixture_geometry()==lib.fixture_workspace(),'NBT/decode workspace and geometry not overlaid'
        assert integer(lib,'mc_render_geometry_limit').value*84<=1048576
        assert lib.fixture_rebuild()==0,'ordinary source slab/special block rejected in owned world'
        geometry=snapshot(lib);cases=0
        for subtract in (0,4,11):
            integer(lib,'mc_world_skylight').value=subtract
            for yaw,pitch in ((0,0),(90,0),(-39.125,17.7)):
                for gamma,fancy in ((0,0),(.5,1),(1,1)):
                    integer(lib,'mc_render_cache_ready').value=0;direct=draw(lib,yaw,pitch,gamma,fancy)
                    integer(lib,'mc_render_cache_ready').value=1;cached=draw(lib,yaw,pitch,gamma,fancy)
                    assert cached==direct,('daylight/cache stream',subtract,yaw,pitch,gamma,fancy)
                    assert snapshot(lib)==geometry,'day/night/gamma mutated geometry'
                    cases+=1
        # Exhaustively preserve source sky/block dominance, minima and sides.
        lib.mc_render_geometry_light_variant.argtypes=[C.c_uint32]
        for subtract in range(12):
            integer(lib,'mc_world_skylight').value=subtract;lib.mc_render_prepare_daylight()
            lookup=(C.c_uint32*128).in_dll(lib,'mc_render_light_lookup')
            for sky in range(16):
                for block in range(16):
                    level=max(0,sky-subtract,block)
                    for side in range(6):
                        style=((sky*16+block)<<10)|side
                        assert lib.mc_render_geometry_light_variant(style)==lookup[level*8+side]
        # All source static shapes accepted; moving piston has a real tile-state
        # dependency, and End portal advertises its missing animated layer.
        assert all(lib.fixture_tile_status(i)>0 for i in range(1,123))
        assert lib.fixture_tile_status(36)==4 and lib.fixture_tile_status(119)==3
        assert lib.fixture_tile_status(44)==1
        assert lib.fixture_tile_status(123)==0
        # The source mesh pack supplies real geometry, with tint slots retained
        # in cache style rather than thrown away by the cached path.
        lib.mc_render_block.argtypes=[C.c_uint32,C.c_uint32,C.c_int,C.c_int,C.c_int]
        integer(lib,'mc_render_geometry_count').value=0;integer(lib,'mc_render_capture').value=1
        x,z=integer(lib,'mc_runtime_center_x').value*16+8,integer(lib,'mc_runtime_center_z').value*16+8
        blocks=(C.c_ubyte*(9*32768)).in_dll(lib,'fixture_blocks')
        blocks[4*32768+8*2048+8*128+89]=5 # source-valid supporting fire block
        for id,meta in ((44,2),(26,0),(27,2),(29,1),(51,0),(55,15),(69,1),(81,0),(93,4),(104,7),(106,15),(107,4),(111,0),(117,7),(118,3),(120,4),(122,0)):
            before=integer(lib,'mc_render_geometry_count').value;lib.mc_render_block(id,meta,x,90,z)
            assert integer(lib,'mc_block_mesh_error').value==0,(id,meta,'missing source mesh')
            assert integer(lib,'mc_render_geometry_count').value>before,(id,meta,'blank static geometry')
        captured=snapshot(lib);styles=[struct.unpack_from('<I',captured,i+80)[0]for i in range(0,len(captured),84)]
        assert any((s>>18)&63 for s in styles),'source colors discarded'
        integer(lib,'mc_render_capture').value=0
        import _ctypes;_ctypes.FreeLibrary(lib._handle)
    print(f'World renderer: PASS {cases} direct/cache day+gamma streams; 18432 light combinations; shared1MiB arena; all122status records and17 static shape/tint captures')
if __name__=='__main__':run()
