"""Production native renderer: full player/armor/helditems, depth, GUI previews."""
from pathlib import Path
import argparse
import ctypes as C
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from test_third_person import HERE,PROJECT,CANDIDATE,Pair,Stack,Face,db,fb,integer
sys.path.insert(0,str(PROJECT/'tests'))
from test_viewmodel_geometry import asset,png,native

def run(renderer_path,previews):
    runtime_search=os.add_dll_directory(str(Path(shutil.which('g++')).parent))if os.name=='nt'else None
    with tempfile.TemporaryDirectory(prefix='mc10-third-pixels-',ignore_cleanup_errors=True)as temp:
        tmp=Path(temp);dll=tmp/'third.dll';build=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(HERE/'third_person_fixture.c'),'-I',str(CANDIDATE/'src'),'-I',str(PROJECT),'-o',str(dll)],capture_output=True,text=True);assert build.returncode==0,build.stderr
        lib=C.CDLL(str(dll));assetpath=(str(PROJECT/'MC10/assets.mc10')+'\0').encode();C.memmove((C.c_char*1024).in_dll(lib,'fixture_asset_path'),assetpath,len(assetpath));C.memmove((C.c_uint32*65536).in_dll(lib,'mc_math_sine'),(PROJECT/'MC10/math10.dat').read_bytes()[16:],262144)
        class Runtime(C.Structure):_fields_=[(n,C.c_int)for n in('health','block_x','block_y','block_z')]+[('yaw_bits',C.c_uint32),('pitch_bits',C.c_uint32)]+[(n,Pair)for n in('pos_x','pos_y','pos_z')]
        runtime=Runtime.in_dll(lib,'mc_runtime_player');camera=(C.c_float*18).in_dll(lib,'mc_render_camera');numbers=(Pair*16384).in_dll(lib,'mc_fp_values');slots=(Stack*40).in_dll(lib,'mc_inventory_slots')
        faces=(Face*256).in_dll(lib,'fixture_faces');lib.mc_third_capture_frame.argtypes=[C.c_float]
        renderer=C.CDLL(str(renderer_path));signatures={'worker':([C.c_bool],None),'scratch':([C.c_bool],None),'diagnostics':([native.WORD],None),'pristine':([native.WORD],None),'nonpristine':([],None),'end':([],C.c_bool),'begin':([native.PTR,native.PTR,native.PTR,native.WORD,native.WORD],C.c_bool),'draw':([native.PTR,native.PTR,native.PTR,native.WORD,C.POINTER(native.Vertex),native.WORD,C.POINTER(native.Transform),native.PTR,C.c_int32,C.c_int32,native.WORD,C.c_bool,native.PTR],C.c_bool)}
        for name,(args,result)in signatures.items():fn=getattr(renderer,'probe_'+name);fn.argtypes=args;fn.restype=result
        item_pixels,item_mask,_,_=asset('gui/items.png')
        def item_icon(tile):
            target=(C.c_uint32*64).in_dll(lib,'fixture_item_pixels');out=(C.c_ubyte*256).from_address(C.addressof(target));mask=(C.c_uint32*8).in_dll(lib,'fixture_item_mask');mask[:]=[0]*8
            for row in range(16):
                for col in range(16):
                    src=((tile>>4)*16+row)*256+(tile&15)*16+col;at=row*16+col;out[at]=item_pixels[src];word=struct.unpack_from('<I',item_mask,(src>>5)*4)[0]
                    if word&(1<<(31-(src&31))):mask[at>>5]|=1<<(31-(at&31))
        def draws():
            result=[];textures=[]
            for face in faces[:integer(lib,'fixture_face_count').value]:
                vertices=(native.Vertex*4)()
                for c in range(4):
                    for a,name in enumerate(('x','y','z','u','v')):setattr(vertices[c],name,face.vertices[c*5+a])
                transform=native.Transform();transform.value[:]=face.transform;w,h=face.width,face.height;index=len(textures);textures.append((w,h,native.Guarded(bytes(face.pixels)[:w*h]),[None,native.Guarded(bytes(face.mask)[:(w*h+31)//32*4])]))
                result.append(native.make_draw(vertices,asset=index,flags=face.flags,mask=1,matrix=transform))
            return result,textures
        previews.mkdir(parents=True,exist_ok=True);background=bytes([0x4f])*native.PIXELS
        cases=[('rear',1,0,0),('front',2,0,0),('rear_swing',1,0,0),('front_iron_armor',2,0,1),('front_held_pickaxe',2,278,0),('front_pumpkin_helmet',2,0,2),('rear_held_grass',1,2,0)]
        for name,mode,item,armor in cases:
            lib.fixture_init();integer(lib,'fixture_block_xyz').value=999;integer(lib,'fixture_face_count').value=0
            for i,value in enumerate((.5,1.62,.5)):
                bits=db(value);numbers[i]=Pair(bits&0xffffffff,bits>>32);setattr(runtime,('pos_x','pos_y','pos_z')[i],Pair(bits&0xffffffff,bits>>32))
                positions=(Pair*3).in_dll(lib,'mc_tp_tick_position');positions[i]=numbers[i];old=(Pair*3).in_dll(lib,'mc_tp_old_position');old[i]=numbers[i]
            camera[12:]=[171.375,171.375,160,120,.05,48];runtime.yaw_bits=runtime.pitch_bits=0;integer(lib,'mc_third_mode').value=mode
            if armor==1:
                for i,id in enumerate((309,308,307,306)):slots[36+i]=Stack(id,1,0,0,0)
            if armor==2:slots[39]=Stack(86,1,0,0,0)
            if item:slots[0]=Stack(item,1,0,0,0);item_icon(99 if item==278 else 53)
            if name=='rear_swing':C.c_float.in_dll(lib,'mc_vm_swing').value=.5;C.c_float.in_dll(lib,'mc_vm_previous_swing').value=1/3
            lib.mc_third_adjust_camera();lib.mc_third_capture_frame(1);lib.mc_third_draw();assert integer(lib,'fixture_clears').value==0
            commands,textures=draws();assert len(commands)>=42,(name,len(commands));initial=(background,bytes(native.PIXELS*4),bytes(native.PIXELS))
            immediate=native.render(renderer,initial,7,[commands],textures,0,pristine=True);tiled=native.render(renderer,initial,7,[commands],textures,2,live_worker=True,pristine=True);native.assert_equal(immediate,tiled,('thirdperson nativepixel parity',name))
            changed=sum(a!=b for a,b in zip(background,tiled[0]));assert changed>200,(name,changed)
            # A nearer full-screen terrain surface must hide every player
            # pixel: body pass may not clear depth like the hand pass does.
            occluded=(background,bytes(native.PIXELS*4),bytes([7])*native.PIXELS);hidden=native.render(renderer,occluded,7,[commands],textures,2,live_worker=True,pristine=False);assert hidden[0]==background,('body renderedthroughwall',name)
            png(previews/(name+'.png'),tiled[0]);print('Native thirdperson',name,'faces',len(commands),'pixels',changed,'SHA256',hashlib.sha256(tiled[0]).hexdigest())
        lib.mc_tp_world_tesr.argtypes=[C.c_int]*5+[C.c_float]
        for name,id,meta,east,south in [('closed_chest',54,2,0,0),('double_chest_east',54,2,1,0),('double_chest_south',54,5,0,1),('sign_post',63,0,0,0),('wall_sign',68,2,0,0)]:
            lib.fixture_init();integer(lib,'fixture_id').value=id;(C.c_int*3).in_dll(lib,'fixture_block_xyz')[:]=[0,0,0];(C.c_uint32*6).in_dll(lib,'fixture_neighbors')[:]=[0,54 if east else 0,0,54 if south else 0,0,0]
            camera[:]=[-1,0,0,.5,0,1,0,-1.2,0,0,1,3.5,171.375,171.375,160,120,.05,48];assert lib.mc_tp_world_tesr(id,meta,0,0,0,.5)==1
            commands,textures=draws();assert len(commands)==(12 if id==63 else 6 if id==68 else 18)
            initial=(background,bytes(native.PIXELS*4),bytes(native.PIXELS));immediate=native.render(renderer,initial,7,[commands],textures,0,pristine=True);tiled=native.render(renderer,initial,7,[commands],textures,2,live_worker=True,pristine=True);native.assert_equal(immediate,tiled,('tilemodel nativepixel parity',name))
            changed=sum(a!=b for a,b in zip(background,tiled[0]));assert changed>100,(name,changed);hidden=native.render(renderer,(background,bytes(native.PIXELS*4),bytes([7])*native.PIXELS),7,[commands],textures,2,live_worker=True,pristine=False);assert hidden[0]==background,('tilemodel renderedthroughwall',name)
            png(previews/(name+'.png'),tiled[0]);print('Native tilemodel',name,'faces',len(commands),'pixels',changed,'SHA256',hashlib.sha256(tiled[0]).hexdigest())
        lib.fixture_init();screen=(C.c_uint32*native.PIXELS).in_dll(lib,'fixture_screen')
        for i in range(native.PIXELS):screen[i]=0x49
        lib.mc_third_inventory_preview(72,37,160,120);pixels=bytes(screen[i]&255 for i in range(native.PIXELS));assert sum(x!=0x49 for x in pixels)>150;png(previews/'inventory_player.png',pixels)
        import _ctypes;_ctypes.FreeLibrary(renderer._handle);_ctypes.FreeLibrary(lib._handle)
    if runtime_search:runtime_search.close()
    print('PASS seven fullplayer + five chest/sign nativepixel frames immediate/tile/core1 parity, allnearwall depthrejection, orthographicinventorypreview')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--renderer-dll',type=Path,required=True);parser.add_argument('--previews',type=Path,default=HERE.parent/'previews');args=parser.parse_args();run(args.renderer_dll.resolve(),args.previews.resolve())

