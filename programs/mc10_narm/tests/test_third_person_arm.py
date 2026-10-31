"""Run source-validated third-person math/camera/ray code as actual NARM Thumb."""
from pathlib import Path
import argparse
import ctypes as C
import os
import random
import struct
import subprocess
import sys
import tempfile
from test_third_person import HERE,PROJECT,CANDIDATE,Pair,Face,db,fb
sys.path.insert(0,str(PROJECT/'tests'))
from test_viewmodel_arm import Probe

def run(compiler):
    with tempfile.TemporaryDirectory(prefix='mc10-third-thumb-',ignore_cleanup_errors=True)as temp:
        tmp=Path(temp);image=tmp/'third.narm';dll=tmp/'third.dll'
        build=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(HERE/'third_person_fixture.c'),'-I',str(CANDIDATE/'src'),'-I',str(PROJECT),'-o',str(dll)],capture_output=True,text=True);assert build.returncode==0,build.stderr
        build=subprocess.run([str(compiler),str(HERE/'third_person_arm.tc'),'-I',str(CANDIDATE/'src'),'-I',str(PROJECT),'-O3','--stack-bytes','32768','-o',str(image)],capture_output=True,text=True);assert build.returncode==0,build.stdout+build.stderr
        host=C.CDLL(str(dll));C.memmove((C.c_uint32*65536).in_dll(host,'mc_math_sine'),(PROJECT/'MC10/math10.dat').read_bytes()[16:],262144);host.mc_fp_init();host.mc_tp_angles.argtypes=[C.c_float]*6+[C.c_int,C.c_int];probe=Probe(image);rng=random.Random(2906)
        numbers=(Pair*16384).in_dll(host,'mc_fp_values')
        for y,x in[(0.,-0.),(-0.,-0.),(1.,0.),(-1.,-1.),(1e300,1e-300),(1e-300,-1e300)]+[(rng.uniform(-10,10),rng.uniform(-10,10))for _ in range(34)]:
            host.mc_fp_reset();yb=db(y);xb=db(x);yh=host.mc_fp_bits(C.c_uint32(yb),C.c_uint32(yb>>32));xh=host.mc_fp_bits(C.c_uint32(xb),C.c_uint32(xb>>32));r=host.mc_tp_atan2(yh,xh);assert probe.run(2,struct.pack('<QQ',yb,xb))[:8]==struct.pack('<II',numbers[r].lo,numbers[r].hi)
        for case in range(32):
            data=[rng.uniform(0,20),rng.uniform(0,1.3),rng.uniform(0,1000),rng.uniform(-180,180),rng.uniform(-90,90),rng.random()];data=list(struct.unpack('<6f',struct.pack('<6f',*data)));sneak=case&1;held=(case>>1)&1;host.mc_tp_angles(*data,sneak,held);expected=bytes((C.c_float*21).in_dll(host,'mc_tp_rot'))+bytes((C.c_float*21).in_dll(host,'mc_tp_pivot'));assert probe.run(1,struct.pack('<6fII',*data,sneak,held))[:168]==expected,('Thumb ModelBiped',case)
        host.fixture_ray.argtypes=[C.c_double]*6+[C.POINTER(C.c_double)];xyz=(C.c_int*3).in_dll(host,'fixture_block_xyz');bounds=(C.c_float*6).in_dll(host,'fixture_bounds');xyz[:]=[0,2,0];bounds[:]=[0,0,0,1,.5,1]
        for case in range(32):
            ray=[rng.uniform(-4,4),rng.uniform(0,5),rng.uniform(-4,4),rng.uniform(-4,4),rng.uniform(0,5),rng.uniform(-4,4)];hit=(C.c_double*3)();found=host.fixture_ray(*ray,hit);payload=struct.pack('<6d3i6f',*ray,*xyz,*bounds);actual=probe.run(3,payload);assert struct.unpack_from('<I',actual)[0]==found
            if found:assert actual[4:28]==bytes(hit),('Thumb binary64 ray',case)
            assert struct.unpack_from('<I',actual,28)[0]==0,'Thumb ray pool overflow'
        host.fixture_box.argtypes=[C.c_int,C.c_float];C.c_float.in_dll(host,'mc_vm_hand_fov').value=70;C.c_float.in_dll(host,'mc_vm_hand_far').value=48;C.c_float.in_dll(host,'mc_vm_world_light').value=1
        (C.c_float*21).in_dll(host,'mc_tp_rot')[:]=[0]*21;(C.c_float*21).in_dll(host,'mc_tp_pivot')[:]=[0]*21;faces=(Face*256).in_dll(host,'fixture_faces')
        for part in range(7):
            inflation=.5 if part==6 else 0.;host.fixture_box(part,inflation)
            for side in range(6):
                expected=bytes(faces[side].vertices)+bytes(faces[side].transform)+struct.pack('<III',64,32,6);actual=probe.run(5,struct.pack('<IfI',part,inflation,side));assert actual[:164]==expected,('Thumb full ModelBox geometry/transform',part,side)
        host.mc_tp_object_box.argtypes=[C.c_int,C.c_int]+[C.c_float]*3+[C.c_int]*3+[C.c_float]*3
        for case,args in enumerate([(0,0,0.,-5.,-14.,14,5,14,1.,7.,15.),(0,0,-1.,-2.,-15.,2,4,1,8.,7.,15.),(0,19,0.,0.,0.,14,10,14,1.,6.,1.),(0,0,-12.,-14.,-1.,24,12,2,0.,0.,0.)]):
            C.c_int.in_dll(host,'fixture_face_count').value=0;C.c_int.in_dll(host,'mc_tp_object_width').value=64;C.c_int.in_dll(host,'mc_tp_object_height').value=64;(C.c_uint32*256).in_dll(host,'mc_tp_object_mask')[:]=[0xffffffff]*256;host.mc_vm_identity();(C.c_float*16).in_dll(host,'mc_tp_object_base')[:]=list((C.c_float*16).in_dll(host,'mc_vm_matrix'));host.mc_tp_object_box(*args)
            for side in range(6):
                expected=bytes(faces[side].vertices)+bytes(faces[side].transform)+struct.pack('<III',faces[side].width,faces[side].height,6);payload=struct.pack('<II3fIII3fI',*args,side);actual=probe.run(6,payload);assert actual[:164]==expected,('Thumb 13arg objectModelBox crop/UV/transform',case,side)
        import _ctypes;_ctypes.FreeLibrary(host._handle)
    print('PASS actual NARM Thumb:40 exactfdlibmatan2,32 exactModelBipedstates,32 binary64rays,42bodyModelBoxfaces,24croppedchest/sign13argModelBoxfaces;32KiBstackguard')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler',type=Path,required=True);args=parser.parse_args();run(args.compiler.resolve())

