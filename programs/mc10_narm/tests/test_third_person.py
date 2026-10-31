"""Owned 1.0 ModelBiped/ModelBox/World/Block/Vec3D compatibility oracles.

Proprietary Java is read only from the user's installation and inserted into a
temporary host oracle. No original source or texture is shipped with this test.
"""
from pathlib import Path
import ctypes as C
import math
import os
import random
import re
import struct
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
PROJECT=Path(os.environ.get('MC10_PROJECT','C:/Users/enzob/OneDrive/Documentos/RP2350_GP_Computer/Firmware/resource_development/programs/mc10_narm'))
CANDIDATE=Path(os.environ.get('MC10_PLAYER_CANDIDATE',str(HERE.parent)))
ORIGINAL=Path('C:/Users/enzob/AppData/Roaming/.minecraft/versions/1.0/minecraft/src/net/minecraft/src')
def fb(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def db(x):return struct.unpack('<Q',struct.pack('<d',x))[0]
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
class Pair(C.Structure):_fields_=[('lo',C.c_uint32),('hi',C.c_uint32)]
class Stack(C.Structure):_fields_=[(n,C.c_int)for n in('id','count','damage','tag_offset','tag_bytes')]
class Face(C.Structure):_fields_=[('vertices',C.c_float*20),('transform',C.c_float*18),('width',C.c_uint32),('height',C.c_uint32),('flags',C.c_uint32),('pixels',C.c_uint32*512),('mask',C.c_uint32*64)]
def method(text,name):
    m=re.search(r'(?m)^\s*(?:public|private|protected)\s+[^\n]*\b'+re.escape(name)+r'\(',text);assert m,name
    brace=text.index('{',m.start());level=1;end=brace+1
    while level:level+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[m.start():end]
def local_class(name):
    text=(ORIGINAL/(name+'.java')).read_text();text=re.sub(r'package net.minecraft.src;\s*','',text);return text.replace('public class '+name,'class '+name)
def integer(lib,name):return C.c_int.in_dll(lib,name)
def run():
    with tempfile.TemporaryDirectory(prefix='mc10-third-',ignore_cleanup_errors=True)as temp:
        tmp=Path(temp);dll=tmp/'third.dll'
        build=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(HERE/'third_person_fixture.c'),'-I',str(CANDIDATE/'src'),'-I',str(PROJECT),'-o',str(dll)],capture_output=True,text=True);assert build.returncode==0,build.stderr
        lib=C.CDLL(str(dll));asset=(str(PROJECT/'MC10/assets.mc10')+'\0').encode();C.memmove((C.c_char*1024).in_dll(lib,'fixture_asset_path'),asset,len(asset))
        C.memmove((C.c_uint32*65536).in_dll(lib,'mc_math_sine'),(PROJECT/'MC10/math10.dat').read_bytes()[16:],262144)
        lib.fixture_init();assert integer(lib,'mc_vm_skin_ready').value==1
        lib.mc_tp_angles.argtypes=[C.c_float]*6+[C.c_int,C.c_int];lib.fixture_box.argtypes=[C.c_int,C.c_float]
        lib.fixture_ray.argtypes=[C.c_double]*6+[C.POINTER(C.c_double)];lib.mc_fp_to_f32.restype=C.c_float
        java=(HERE/'third_person_oracle.java.in').read_text()
        for name,token in [('ModelBiped','MODEL_BIPED'),('ModelBox','MODEL_BOX'),('TexturedQuad','TEXTURED_QUAD'),('PositionTextureVertex','POSITION_VERTEX'),('Vec3D','VEC3D'),('ModelChest','MODEL_CHEST'),('ModelLargeChest','MODEL_LARGE_CHEST'),('SignModel','SIGN_MODEL')]:
            text=local_class(name)
            if name=='TexturedQuad':text=text.replace('class TexturedQuad {','class TexturedQuad { static ArrayList<TexturedQuad> all=new ArrayList<TexturedQuad>();').replace('this.nVertices = var1.length;','this.nVertices = var1.length;all.add(this);')
            text=re.sub(r'^import .*;\s*$','',text,flags=re.M);java=java.replace('__'+token+'__',text)
        block=(ORIGINAL/'Block.java').read_text();world=(ORIGINAL/'World.java').read_text()
        for token,name,text in [('BLOCK_RAY','collisionRayTrace',block),('INSIDE_YZ','isVecInsideYZBounds',block),('INSIDE_XZ','isVecInsideXZBounds',block),('INSIDE_XY','isVecInsideXYBounds',block),('WORLD_RAY','rayTraceBlocks_do_do',world),('SOURCE_CAMERA','orientCamera',(ORIGINAL/'EntityRenderer.java').read_text()),('SOURCE_INVENTORY','drawGuiContainerBackgroundLayer',(ORIGINAL/'GuiInventory.java').read_text()),('SOURCE_CHEST','func_35376_a',(ORIGINAL/'TileEntityChestRenderer.java').read_text()),('SOURCE_SIGN','renderTileEntitySignAt',(ORIGINAL/'TileEntitySignRenderer.java').read_text())]:java=java.replace('__'+token+'__',method(text,name))
        (tmp/'ThirdPersonOracle.java').write_text(java)
        build=subprocess.run(['javac',str(tmp/'ThirdPersonOracle.java')],capture_output=True,text=True);assert build.returncode==0,build.stderr
        rng=random.Random(2905)
        atan=[(y,x)for y in(0.,-0.,1.,-1.,1e-300,-1e-300,1e300,-1e300)for x in(0.,-0.,1.,-1.,1e-300,-1e-300,1e300,-1e300)]
        atan+=[(math.ldexp(rng.uniform(-2,2),rng.randrange(-1000,1001)),math.ldexp(rng.uniform(-2,2),rng.randrange(-1000,1001)))for _ in range(400)]
        angles=[[f32(rng.uniform(0,90)),f32(rng.uniform(0,1.5)),f32(rng.uniform(0,1000)),f32(rng.uniform(-180,180)),f32(rng.uniform(-90,90)),f32(rng.uniform(0,1)),i&1,(i>>1)&1]for i in range(128)]
        angles+=[[0.,0.,0.,0.,0.,0.,s,h]for s in(0,1)for h in(0,1)]
        boxes=[(p,i)for p in range(7)for i in(0.,.5,1.,1.5)]
        rays=[]
        for i in range(256):
            blockpos=[rng.randrange(-4,5),rng.randrange(0,8),rng.randrange(-4,5)]
            bounds=list(((0.,0.,0.,1.,1.,1.),(0.,0.,0.,1.,.5,1.),(0.,0.,.875,1.,1.,1.),(.375,0.,.375,.625,1.,.625))[i%4])
            center=[blockpos[a]+(bounds[a]+bounds[a+3])/2 for a in range(3)]
            direction=[rng.uniform(-5,5)for _ in range(3)];start=[center[a]+direction[a]for a in range(3)];end=[center[a]-direction[a]for a in range(3)]
            if i%5==0:end=[start[a]+rng.uniform(-3,3)for a in range(3)]
            rays.append((start+end,blockpos,bounds))
        # Boundary, equal-axis DDA ties, starting inside, negative coordinates,
        # parallel plane and source Vec3D 1e-7F rejection threshold.
        for ray in[(.5,.5,-2,.5,.5,2),(0.,.5,.5,2.,.5,.5),(.5,.5,.5,2.,2.,2.),(-2.,-2.,-2.,2.,2.,2.),(-.5,.5,.5,.5,.5,.5),(0.,0.,0.,0.,0.,0.),(.5,.5,-.0001,.5,.5,.0001)]:rays.append((list(ray),[0,0,0],[0.,0.,0.,1.,1.,1.]))
        cameras=[(mode,f32(yaw),f32(pitch),[.5,2.62,.5],[0,2,z],[0.,0.,0.,1.,1.,1.])for mode in(1,2)for yaw in(0,45,90,180,-135)for pitch in(0,45,-45,89,-89)for z in(-2,2,999)]
        previews=[(x,y)for x in(0,90,123,160,250,319)for y in(0,62,120,239)]
        objects=[(54,m,e,s)for m in(2,3,4,5)for e,s in((0,0),(1,0),(0,1))]+[(63,m,0,0)for m in range(16)]+[(68,m,0,0)for m in(2,3,4,5)]
        lines=['atan2 %016x %016x'%(db(y),db(x))for y,x in atan]
        lines+=['angles '+' '.join('%08x'%fb(v)for v in row[:6])+' %d %d'%tuple(row[6:])for row in angles]
        lines+=['box %d %08x'%(part,fb(inflation))for part,inflation in boxes]
        lines+=['ray '+' '.join('%016x'%db(x)for x in ray)+' '+' '.join(map(str,pos))+' '+' '.join('%08x'%fb(x)for x in bounds)for ray,pos,bounds in rays]
        lines+=['camera %d %08x %08x '%(mode,fb(yaw),fb(pitch))+' '.join('%016x'%db(x)for x in position)+' '+' '.join(map(str,pos))+' '+' '.join('%08x'%fb(x)for x in bounds)for mode,yaw,pitch,position,pos,bounds in cameras]
        lines+=['preview %d %d'%p for p in previews]
        lines+=['tesr %d %d %d %d'%o for o in objects]
        oracle=subprocess.run(['java','-cp',str(tmp),'ThirdPersonOracle',str(PROJECT/'MC10/math10.dat')],input='\n'.join(lines)+'\n',capture_output=True,text=True);assert oracle.returncode==0,oracle.stderr
        output=iter(oracle.stdout.splitlines());numbers=(Pair*16384).in_dll(lib,'mc_fp_values')
        for y,x in atan:
            expected=int(next(output),16);lib.mc_fp_reset();yb=db(y);xb=db(x);yh=lib.mc_fp_bits(C.c_uint32(yb),C.c_uint32(yb>>32));xh=lib.mc_fp_bits(C.c_uint32(xb),C.c_uint32(xb>>32));result=lib.mc_tp_atan2(yh,xh);actual=numbers[result].lo|(numbers[result].hi<<32);assert actual==expected,('fdlibm atan2',y,x,hex(actual),hex(expected))
        for row in angles:
            expected=[int(x,16)for x in next(output).split()];lib.mc_tp_angles(*row);actual=[fb(x)for x in (C.c_float*21).in_dll(lib,'mc_tp_rot')]+[fb(x)for x in(C.c_float*21).in_dll(lib,'mc_tp_pivot')]
            assert actual==expected,('owned ModelBiped',row,[(i,hex(a),hex(e))for i,(a,e)in enumerate(zip(actual,expected))if a!=e])
        faces=(Face*256).in_dll(lib,'fixture_faces')
        for part,inflation in boxes:
            expected=[int(x,16)for x in next(output).split()];lib.fixture_box(part,inflation);assert integer(lib,'fixture_face_count').value==6
            actual=[fb(x)for face in faces[:6]for x in face.vertices];assert actual==expected,('owned ModelBox UV/winding',part,inflation)
        xyz=(C.c_int*3).in_dll(lib,'fixture_block_xyz');shape=(C.c_float*6).in_dll(lib,'fixture_bounds')
        for ray,pos,bounds in rays:
            expected=next(output).split();xyz[:]=pos;shape[:]=bounds;hit=(C.c_double*3)();found=lib.fixture_ray(*ray,hit)
            assert bool(found)==(expected!=['none']),('owned World ray found',ray,pos,bounds,expected,found)
            if found:assert [db(x)for x in hit]==[int(x,16)for x in expected],('owned World/Block ray',ray,pos,bounds,list(hit),expected)
            assert integer(lib,'mc_fp_error').value==0,'ray pool overflow'
        class Runtime(C.Structure):_fields_=[(n,C.c_int)for n in('health','block_x','block_y','block_z')]+[('yaw_bits',C.c_uint32),('pitch_bits',C.c_uint32)]+[(n,Pair)for n in('pos_x','pos_y','pos_z')]
        runtime=Runtime.in_dll(lib,'mc_runtime_player');camera=(C.c_float*18).in_dll(lib,'mc_render_camera')
        for mode,yaw,pitch,position,pos,bounds in cameras:
            expected=[struct.unpack('<f',int(word,16).to_bytes(4,'little'))[0]for word in next(output).split()];xyz[:]=pos;shape[:]=bounds;integer(lib,'mc_third_mode').value=mode;runtime.yaw_bits=fb(yaw);runtime.pitch_bits=fb(pitch)
            for n,value in zip(('pos_x','pos_y','pos_z'),position):bits=db(value);setattr(runtime,n,Pair(bits&0xffffffff,bits>>32))
            lib.mc_third_adjust_camera();assert max(abs(a-b)for a,b in zip(camera,expected))<3e-5,('owned EntityRenderer camera',mode,yaw,pitch,pos,list(camera)[:12],expected)
        for x,y in previews:
            expected=[struct.unpack('<f',int(word,16).to_bytes(4,'little'))[0]for word in next(output).split()];lib.mc_third_inventory_preview(72,37,x,y);actual=list((C.c_float*16).in_dll(lib,'mc_tp_base'));assert max(abs(a-b)for a,b in zip(actual,expected))<3e-5,('owned GuiInventory transform',x,y,actual,expected)
        sys_path=str(PROJECT/'tests')
        import sys
        sys.path.insert(0,sys_path)
        from test_viewmodel_geometry import asset
        neighbors=(C.c_uint32*6).in_dll(lib,'fixture_neighbors');lib.mc_tp_world_tesr.argtypes=[C.c_int]*5+[C.c_float]
        for id,meta,east,south in objects:
            fields=next(output).split();name=fields.pop(0).lstrip('/');texture,texture_mask,width,height=asset(name)
            expected=[struct.unpack('<f',int(word,16).to_bytes(4,'little'))[0]for word in fields];lib.fixture_init();xyz[:]=[0,0,0];integer(lib,'fixture_id').value=id;neighbors[:]=[0,54 if east else 0,0,54 if south else 0,0,0]
            camera[:]=[1,0,0,0,0,1,0,0,0,0,-1,0,171.375,171.375,160,120,.05,48];assert lib.mc_tp_world_tesr(id,meta,0,0,0,.5)==1;count=integer(lib,'fixture_face_count').value;assert len(expected)==count*20,(id,meta,east,south,len(expected),count)
            for side,face in enumerate(faces[:count]):
                wanted=expected[side*20:side*20+20];wanted_u=[wanted[c*5+3]*width for c in range(4)];wanted_v=[wanted[c*5+4]*height for c in range(4)];crop_u=int(min(wanted_u));crop_v=int(min(wanted_v));matrix=list(face.transform)
                for corner in range(4):
                    raw=list(face.vertices[corner*5:corner*5+5]);actual=[sum(matrix[row*4+a]*raw[a]for a in range(3))+matrix[row*4+3]for row in range(3)];actual[2]=-actual[2]
                    assert max(abs(a-b)for a,b in zip(actual,wanted[corner*5:corner*5+3]))<3e-6,('owned closedchest/sign ModelBox worldvertex',id,meta,east,south,side,corner,actual,wanted[corner*5:corner*5+3])
                    assert raw[3]==wanted_u[corner]-crop_u and raw[4]==wanted_v[corner]-crop_v,('owned closedchest/signUV',id,meta,side,corner)
            assert integer(lib,'fixture_clears').value==0,'tilemodel clearedworlddepth'
        lib.fixture_init()
        # GUI uses independent orthographic scratch, no firmware depth clear,
        # and populates original six-face cube/slab inventory icon caches.
        for id,damage in[(1,0),(44,0),(44,2),(35,14),(53,0),(85,0),(54,0),(122,0),(29,0),(116,0),(77,0)]:
            stack=Stack(id,1,damage,0,0);lib.mc_third_block_icon.argtypes=[C.POINTER(Stack),C.c_int,C.c_int];assert lib.mc_third_block_icon(C.byref(stack),100,100)==1
            index=integer(lib,'mc_ui_icon_used').value-1;mask=(C.c_uint32*(48*8)).in_dll(lib,'mc_ui_icon_masks');assert any(mask[index*8:index*8+8]),('blank GUI blockicon',id,damage)
        assert integer(lib,'fixture_clears').value==0,'GUI should notclear worlddepth'
        # Modecycle and nohand depth clear in body pass; snapshot survives live
        # input/inventory changes while a terrain frame renders in batches.
        lib.fixture_init();assert integer(lib,'mc_third_mode').value==0
        for expected in(1,2,0,1):lib.mc_third_cycle();assert integer(lib,'mc_third_mode').value==expected
        lib.mc_third_capture_frame.argtypes=[C.c_float];lib.mc_third_capture_frame(.5)
        snapshot=bytes((C.c_float*18).in_dll(lib,'mc_tp_frame_camera'));integer(lib,'mc_third_mode').value=2;lib.mc_third_draw();assert integer(lib,'fixture_clears').value==0 and integer(lib,'fixture_face_count').value==42;assert snapshot==bytes((C.c_float*18).in_dll(lib,'mc_tp_frame_camera'))
        import _ctypes;_ctypes.FreeLibrary(lib._handle)
    print('PASS thirdperson: %d exact atan2, %d owned ModelBiped, %d ModelBox UV/winding, %d World/Block/Vec3D binary64 rays, %d EntityRenderer cameras, %d GuiInventory transforms, %d actualclosedchest/sign ModelBox/UV/transforms; elevenGUIicons, F5cycle, body-depth and frame snapshot'%(len(atan),len(angles),len(boxes),len(rays),len(cameras),len(previews),len(objects)))
if __name__=='__main__':run()

