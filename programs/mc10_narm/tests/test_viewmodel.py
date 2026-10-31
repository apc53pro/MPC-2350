"""Owned-source first-person transforms, finite atan and animation regressions.

The ItemRenderer/EntityRenderer method bodies are read locally into a temporary
Java GL recorder, not bundled as proprietary source. The native-pixel oracle is
an existing compiled production renderer; this test never builds firmware.
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
ROOT=HERE.parent
PROJECT=Path(os.environ.get('MC10_PROJECT',str(ROOT)))
ORIGINAL=Path('C:/Users/enzob/AppData/Roaming/.minecraft/versions/1.0/minecraft/src/net/minecraft/src')
def fbits(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def dbits(x):return struct.unpack('<Q',struct.pack('<d',x))[0]
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
class Pair(C.Structure):_fields_=[('lo',C.c_uint32),('hi',C.c_uint32)]
class Stack(C.Structure):_fields_=[(n,C.c_int)for n in ('id','count','damage','tag_offset','tag_bytes')]
class Player(C.Structure):_fields_=[('yaw',C.c_float),('pitch',C.c_float),('grounded',C.c_int),('sneak',C.c_int),('water',C.c_int)]
class Settings(C.Structure):_fields_=[(n,C.c_int)for n in ('view_bobbing','render_distance','fancy')]
class Face(C.Structure):_fields_=[('vertices',C.c_float*20),('transform',C.c_float*18),('width',C.c_uint32),('height',C.c_uint32),('flags',C.c_uint32),('pixels',C.c_uint32*512),('mask',C.c_uint32*64)]
def scalar(lib,name):return C.c_float.in_dll(lib,name)
def integer(lib,name):return C.c_int.in_dll(lib,name)
def method(text,name):
    match=re.search(r'(?m)^[ \t]*(?:public|private|protected)\s+[^\n]*\b'+re.escape(name)+r'\(',text)
    assert match,('missing source method',name)
    start=match.start()
    brace=text.index('{',start);level=1;end=brace+1
    while level:level+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]


def run():
    with tempfile.TemporaryDirectory(prefix='mc10-viewmodel-',ignore_cleanup_errors=True) as temp:
        tmp=Path(temp);dll=tmp/'viewmodel.dll'
        built=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(HERE/'viewmodel_fixture.c'),
            '-I',str(PROJECT),'-I',str(ROOT/'src'),'-o',str(dll)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        lib=C.CDLL(str(dll));lib.mc_fp_to_f32.restype=C.c_float
        lib.mc_fp_f32.argtypes=[C.c_float];lib.mc_vm_item_matrix.argtypes=[C.c_float,C.c_int]
        lib.mc_vm_swing_progress.argtypes=[C.c_float];lib.mc_vm_swing_progress.restype=C.c_float
        lib.mc_viewmodel_capture_frame.argtypes=[C.c_float];lib.fixture_draw.argtypes=[C.c_float]
        asset_path=(str(PROJECT/'MC10/assets.mc10')+'\0').encode();C.memmove((C.c_char*1024).in_dll(lib,'fixture_asset_path'),asset_path,len(asset_path))
        sine=(PROJECT/'MC10/math10.dat').read_bytes()[16:];assert len(sine)==262144
        C.memmove((C.c_uint32*65536).in_dll(lib,'mc_math_sine'),sine,len(sine))
        java=(HERE/'viewmodel_oracle.java.in').read_text()
        java=java.replace('__SOURCE_ITEM_METHOD__',method((ORIGINAL/'ItemRenderer.java').read_text(),'renderItemInFirstPerson'))
        java=java.replace('__SOURCE_BOB_METHOD__',method((ORIGINAL/'EntityRenderer.java').read_text(),'setupViewBobbing'))
        (tmp/'ViewmodelOracle.java').write_text(java)
        compiled=subprocess.run(['javac',str(tmp/'ViewmodelOracle.java')],capture_output=True,text=True)
        assert compiled.returncode==0,compiled.stderr
        rng=random.Random(2801)
        atan_values=[0.,-0.,1e-300,-1e-300,1e300,-1e300]
        for edge in (2**-29,7/16,11/16,19/16,39/16,2**66):
            for x in (math.nextafter(edge,0),edge,math.nextafter(edge,math.inf)):atan_values.extend((x,-x))
        atan_values.extend(math.ldexp(rng.uniform(-2,2),rng.randrange(-1000,1001))for _ in range(400))
        cases=[]
        for i in range(72):
            cases.append([rng.uniform(-360,360),rng.uniform(-89,89),rng.uniform(-360,360),rng.uniform(-89,89),
                rng.uniform(-360,360),rng.uniform(-89,89),rng.randrange(6)/6,rng.randrange(6)/6,
                rng.uniform(0,1),rng.uniform(0,1),(i%5)/4,(0,280,346)[i%3],rng.uniform(0,100),rng.uniform(0,100),
                rng.uniform(0,.1),rng.uniform(0,.1),rng.uniform(-10,10),rng.uniform(-10,10),i&1])
        # Include source default, halfway/last swing wrap and equip endpoints.
        cases.extend([[0.,0.,0.,0.,0.,0.,s,p,e,e,partial,id,0.,0.,0.,0.,0.,0.,0.]
                      for id in (0,280)for s,p in ((0.,0.),(0.,5/6),(3/6,2/6))for e in (0.,1.)for partial in (0.,.5,1.)])
        input_lines=['atan '+format(dbits(x),'016x')for x in atan_values]
        input_lines+=['matrix '+' '.join(format(fbits(x),'08x')for x in values)for values in cases]
        oracle=subprocess.run(['java','-cp',str(tmp),'ViewmodelOracle',str(PROJECT/'MC10/math10.dat')],input='\n'.join(input_lines)+'\n',capture_output=True,text=True)
        assert oracle.returncode==0,oracle.stderr
        output=oracle.stdout.splitlines();assert len(output)==len(input_lines)
        numbers=(Pair*16384).in_dll(lib,'mc_fp_values')
        for x,line in zip(atan_values,output):
            expected,camera=line.split();lib.mc_fp_init();bits=dbits(x)
            arg=lib.mc_fp_bits(C.c_uint32(bits),C.c_uint32(bits>>32));result=lib.mc_fp_atan(arg)
            actual=numbers[result].lo|(numbers[result].hi<<32)
            assert actual==int(expected,16),('atan fdlibm',x,hex(actual),expected)
            camera_actual=f32(lib.mc_fp_to_f32(result)*15.)
            assert fbits(camera_actual)==int(camera,16),('Math.atan camera float',x)
        lib.fixture_init();assert integer(lib,'mc_vm_skin_ready').value==1
        player=Player.in_dll(lib,'mc_player');options=Settings.in_dll(lib,'mc_options')
        names=('mc_vm_arm_yaw','mc_vm_arm_pitch','mc_vm_previous_arm_yaw','mc_vm_previous_arm_pitch',
               'mc_vm_swing','mc_vm_previous_swing','mc_vm_equipped','mc_vm_previous_equipped')
        for index,(values,line)in enumerate(zip(cases,output[len(atan_values):])):
            values=[f32(x)for x in values];player.yaw,player.pitch=values[:2]
            for name,value in zip(names,values[2:10]):scalar(lib,name).value=value
            for name,value in zip(('mc_vm_walk','mc_vm_previous_walk','mc_vm_camera_yaw','mc_vm_previous_camera_yaw','mc_vm_camera_pitch','mc_vm_previous_camera_pitch'),values[12:18]):scalar(lib,name).value=value
            options.view_bobbing=int(values[18]);stack=Stack.in_dll(lib,'mc_vm_stack');stack.id=int(values[11]);stack.count=int(stack.id!=0)
            lib.mc_fp_reset();lib.mc_vm_item_matrix(values[10],int(stack.id==0))
            actual=list((C.c_float*16).in_dll(lib,'mc_vm_matrix'))
            expected=[struct.unpack('<f',int(word,16).to_bytes(4,'little'))[0]for word in line.split()]
            assert max(abs(a-b)for a,b in zip(actual,expected))<2e-5,('owned GL matrix',index,max(abs(a-b)for a,b in zip(actual,expected)),actual,expected)
        # Equip state, identity-equivalent replacements and lowering lifetime.
        lib.fixture_init();slots=(Stack*40).in_dll(lib,'mc_inventory_slots');slots[0]=Stack(280,10,0,0,0)
        equipped=[]
        for _ in range(5):lib.mc_viewmodel_begin_tick();lib.mc_viewmodel_tick();equipped.append(scalar(lib,'mc_vm_equipped').value)
        assert [fbits(x)for x in equipped]==[fbits(x)for x in (0.,.4,.8,1.,1.)]
        slots[1]=Stack(280,1,0,0,0);integer(lib,'mc_inventory_selected').value=1;lib.mc_viewmodel_tick();assert scalar(lib,'mc_vm_equipped').value==1.
        slots[2]=Stack(346,1,0,0,0);integer(lib,'mc_inventory_selected').value=2
        held=[]
        for _ in range(3):lib.mc_viewmodel_tick();held.append((scalar(lib,'mc_vm_equipped').value,Stack.in_dll(lib,'mc_vm_stack').id))
        assert [id for _,id in held]==[280,280,346],held
        # Source swing restart gating and 5/6 -> 0 interpolation continuity.
        lib.fixture_init();lib.mc_viewmodel_swing();swing=[]
        for tick in range(7):
            lib.mc_viewmodel_tick();swing.append(scalar(lib,'mc_vm_swing').value)
            if tick==2:before=integer(lib,'mc_vm_swing_ticks').value;lib.mc_viewmodel_swing();assert integer(lib,'mc_vm_swing_ticks').value==before
        assert [fbits(x)for x in swing]==[fbits(x/6)for x in (0,1,2,3,4,5,0)]
        assert fbits(lib.mc_vm_swing_progress(.5))==fbits(f32(f32(5/6)+f32(f32(1-f32(5/6))*.5)))
        lib.mc_viewmodel_swing();assert integer(lib,'mc_vm_swing_ticks').value==-1
        # No simulation hook while paused: repeated draws must not tick equip,
        # swing, walk distance, camera pitch/yaw or arm smoothing.
        lib.fixture_init();player.yaw=0;player.pitch=0;lib.mc_viewmodel_tick();lib.mc_viewmodel_tick()
        before=tuple(scalar(lib,name).value for name in names)
        lib.fixture_draw(.5);assert integer(lib,'fixture_face_count').value==6 and integer(lib,'fixture_clears').value==1
        faces=(Face*128).in_dll(lib,'fixture_faces');assert all(faces[i].flags==0x100 for i in range(6))
        first=bytes(faces[0]);lib.fixture_draw(.5);assert bytes(faces[0])==first
        assert before==tuple(scalar(lib,name).value for name in names)
        # A completed slow terrain frame must use its captured pose/stack even
        # if input and successful ticks change the live state in the meantime.
        lib.mc_viewmodel_capture_frame(.25);matrix=bytes((C.c_float*16).in_dll(lib,'mc_vm_frame_matrix'))
        player.yaw=120;player.pitch=70;slots[0]=Stack(280,1,0,0,0)
        for _ in range(4):lib.mc_viewmodel_tick()
        assert matrix==bytes((C.c_float*16).in_dll(lib,'mc_vm_frame_matrix'))
        lib.fixture_draw(.75);assert integer(lib,'fixture_face_count').value==6
        assert Stack.in_dll(lib,'mc_vm_stack').id==280,'drawing mutated live equipped state'
        lib.fixture_draw(.5);assert integer(lib,'fixture_face_count').value==66
        # Missing depth capability / missing skin / special dependencies fail
        # explicitly before touching the frame, not with a painter substitute.
        integer(lib,'mc_vm_depth_ready').value=0;lib.fixture_draw(.5)
        assert integer(lib,'mc_viewmodel_status').value==1 and integer(lib,'fixture_clears').value==0
        import _ctypes;_ctypes.FreeLibrary(lib._handle)
    print(f'PASS viewmodel: {len(atan_values)} exact fdlibm/Java camera vectors, {len(cases)} owned-source GL matrices; equip/swing/wrap/pause/frame capture, six-face arm, 66-quad extruded item, capability gating')


if __name__=='__main__':run()
