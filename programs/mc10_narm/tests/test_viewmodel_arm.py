"""Execute production viewmodel/atan as NARMCC Thumb, isolated CPU only."""
from pathlib import Path
import argparse
import ctypes as C
import math
import random
import struct
import subprocess
import sys
import tempfile
from test_viewmodel import PROJECT,ROOT,HERE,Player,Settings,Stack,Pair,fbits,dbits,scalar,integer
sys.path.insert(0,'C:/Users/enzob/Documents/Codex/2026-10-13/co/work/arm_tools')


class Probe:
    def __init__(self,path):
        from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
        from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPEXC)
        self.regs=(UC_ARM_REG_R0,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPEXC)
        image=path.read_bytes();u32=lambda o:struct.unpack_from('<I',image,o)[0];align=lambda n,a:(n+a-1)&~(a-1)
        self.text,self.arena,self.ctx,self.stack,self.stop,self.invoke,self.video=0x100000,0x2000000,0x3000000,0x4000000,0x3001000,0x3001200,0x3001300
        uc=self.uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB);self.entry=self.text+u32(0x18)
        uc.mem_map(self.text,align(max(u32(0x20),4096),4096));uc.mem_map(self.arena,0x800000)
        uc.mem_map(self.ctx,0x3000);uc.mem_map(self.stack,0x10000)
        uc.mem_write(self.text,image[u32(0x1c):u32(0x1c)+u32(0x20)])
        size=u32(0x2c);data=align(size,u32(0x3c))
        if size:uc.mem_write(self.arena,image[u32(0x28):u32(0x28)+size])
        if u32(0x38):uc.mem_write(self.arena+data,image[u32(0x34):u32(0x34)+u32(0x38)])
        for i in range(u32(0x54)):
            patch,target,kind,_=struct.unpack_from('<IIHH',image,u32(0x50)+12*i)
            if not kind:continue
            value=self.arena+target if kind==1 else (self.text+target)|(kind==3) if kind in (2,3) else self.ctx
            uc.mem_write(self.arena+patch,struct.pack('<I',value))
        for p in (self.stop,self.invoke,self.video):uc.mem_write(p,b'\x70\x47')
        uc.mem_write(self.ctx+8,struct.pack('<I',self.ctx+0x100));uc.mem_write(self.ctx+0x110,struct.pack('<I',self.invoke|1))
        uc.mem_write(self.ctx+56,struct.pack('<I',self.ctx+0x200));uc.mem_write(self.ctx+0x21c,struct.pack('<I',self.video|1))
        uc.mem_write(self.stack,bytes([0x5a])*64)
        def hook(cpu,address,size,data):
            if address==self.stop:cpu.emu_stop();return
            assert address in (self.invoke,self.video),hex(address)
            command=cpu.reg_read(UC_ARM_REG_R1);arguments=cpu.reg_read(UC_ARM_REG_R2);count=cpu.reg_read(UC_ARM_REG_R3)
            if address==self.invoke:
                assert command in (6,7) and count==1,(command,count)
                value=struct.unpack('<f',bytes(cpu.mem_read(arguments,4)))[0]
                result=fbits(math.sin(value)if command==6 else math.cos(value))
            else:
                assert command==0x301 and count==1,(hex(command),count)
                index=struct.unpack('<I',bytes(cpu.mem_read(arguments,4)))[0]
                assert index==76;result=0x44430001
            cpu.reg_write(UC_ARM_REG_R0,result);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        uc.hook_add(UC_HOOK_CODE,hook,begin=self.stop,end=self.video+4)
        self.control=self.call()
        packet=struct.unpack('<16I',bytes(uc.mem_read(self.control,64)))
        self.input,self.output=packet[2:4]
        uc.mem_write(packet[1],(PROJECT/'MC10/math10.dat').read_bytes()[16:])

    def call(self):
        r0,r9,r10,sp,lr,pc,fpexc=self.regs;uc=self.uc
        uc.reg_write(r9,self.arena);uc.reg_write(r10,self.ctx);uc.reg_write(sp,self.stack+32752)
        uc.reg_write(lr,self.stop|1);uc.reg_write(fpexc,1<<30)
        uc.emu_start(self.entry|1,self.stop,count=100000000)
        assert uc.reg_read(pc)==self.stop,'Thumb fixture exhausted instruction budget'
        assert bytes(uc.mem_read(self.stack,64))==bytes([0x5a])*64,'private stack guard corrupted'
        return uc.reg_read(r0)

    def run(self,mode,data,initial=False):
        self.uc.mem_write(self.control,struct.pack('<I',mode));self.uc.mem_write(self.control+16,struct.pack('<I',int(initial)))
        self.uc.mem_write(self.input,data+bytes(256-len(data)));assert self.call()==0
        return bytes(self.uc.mem_read(self.output,256))


def run(compiler):
    with tempfile.TemporaryDirectory(prefix='viewmodel-thumb-',ignore_cleanup_errors=True)as temp:
        tmp=Path(temp);dll=tmp/'host.dll'
        built=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(HERE/'viewmodel_fixture.c'),'-I',str(PROJECT),'-I',str(ROOT/'src'),'-o',str(dll)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        host=C.CDLL(str(dll));host.mc_vm_item_matrix.argtypes=[C.c_float,C.c_int]
        asset=(str(PROJECT/'MC10/assets.mc10')+'\0').encode();C.memmove((C.c_char*1024).in_dll(host,'fixture_asset_path'),asset,len(asset))
        C.memmove((C.c_uint32*65536).in_dll(host,'mc_math_sine'),(PROJECT/'MC10/math10.dat').read_bytes()[16:],262144)
        output=tmp/'viewmodel.narm'
        built=subprocess.run([str(compiler),str(HERE/'viewmodel_arm.tc'),'-I',str(PROJECT),'-I',str(ROOT/'src'),'-O3','--stack-bytes','32768','-o',str(output)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr+built.stdout
        probe=Probe(output);rng=random.Random(2802)
        numbers=(Pair*16384).in_dll(host,'mc_fp_values')
        atan_cases=[0.,-0.,1e-300,-1e-300,1e300,-1e300,.4375,-.6875,1.1875,-2.4375]
        atan_cases.extend(rng.uniform(-25,25)for _ in range(30))
        for value in atan_cases:
            host.mc_fp_init();bits=dbits(value);arg=host.mc_fp_bits(C.c_uint32(bits),C.c_uint32(bits>>32));result=host.mc_fp_atan(arg)
            expected=struct.pack('<II',numbers[result].lo,numbers[result].hi)
            assert probe.run(2,struct.pack('<Q',bits))[:8]==expected,('Thumb atan',value)
        host.fixture_init();player=Player.in_dll(host,'mc_player');options=Settings.in_dll(host,'mc_options')
        names=('mc_vm_arm_yaw','mc_vm_arm_pitch','mc_vm_previous_arm_yaw','mc_vm_previous_arm_pitch','mc_vm_swing','mc_vm_previous_swing','mc_vm_equipped','mc_vm_previous_equipped')
        for case in range(36):
            values=[rng.uniform(-360,360),rng.uniform(-89,89),rng.uniform(-360,360),rng.uniform(-89,89),rng.uniform(-360,360),rng.uniform(-89,89),rng.randrange(6)/6,rng.randrange(6)/6,rng.random(),rng.random(),(case%5)/4,(0,280,346)[case%3],rng.uniform(0,50),rng.uniform(0,50),rng.random()/10,rng.random()/10,rng.uniform(-10,10),rng.uniform(-10,10),case&1]
            values=list(struct.unpack('<19f',struct.pack('<19f',*values)))
            player.yaw,player.pitch=values[:2]
            for name,value in zip(names,values[2:10]):scalar(host,name).value=value
            for name,value in zip(('mc_vm_walk','mc_vm_previous_walk','mc_vm_camera_yaw','mc_vm_previous_camera_yaw','mc_vm_camera_pitch','mc_vm_previous_camera_pitch'),values[12:18]):scalar(host,name).value=value
            options.view_bobbing=int(values[18]);stack=Stack.in_dll(host,'mc_vm_stack');stack.id=int(values[11]);stack.count=int(stack.id!=0)
            host.mc_fp_init();host.mc_vm_item_matrix(values[10],int(not stack.count));expected=list((C.c_float*16).in_dll(host,'mc_vm_matrix'))
            actual=struct.unpack('<16f',probe.run(1,struct.pack('<19f',*values))[:64])
            assert max(abs(a-b)for a,b in zip(actual,expected))<2e-5,('Thumb matrix',case,actual,expected)
        host.fixture_init();slots=(Stack*40).in_dll(host,'mc_inventory_slots')
        floats=('mc_vm_equipped','mc_vm_previous_equipped','mc_vm_swing','mc_vm_previous_swing','mc_vm_arm_yaw','mc_vm_arm_pitch','mc_vm_walk','mc_vm_camera_yaw','mc_vm_camera_pitch')
        for tick in range(24):
            selected=0 if tick<7 else 1 if tick<14 else 2
            id=280 if selected!=2 else 346;count=3 if selected==0 else 1
            yaw,pitch=13.*tick,math.sin(tick)*35;grounded=int(tick%8<5);sneak=int(tick%7==0)
            motion=[.1,0 if grounded else -.42,.075];pos=[tick*.1,72.+(0 if grounded else .25),tick*.075]
            words=[id,count,0,selected,fbits(yaw),fbits(pitch),int(tick in (2,4,6,16)),grounded,sneak,20]
            for value in pos+motion:words.extend(struct.unpack('<II',struct.pack('<d',value)))
            host.mc_viewmodel_begin_tick();integer(host,'mc_inventory_selected').value=selected;slots[selected]=Stack(id,count,0,0,0)
            player.yaw,player.pitch=yaw,pitch
            if words[6]:host.mc_viewmodel_swing()
            player.grounded,player.sneak=grounded,sneak
            for i,value in enumerate(pos+motion):bits=dbits(value);numbers[i].lo=bits&0xffffffff;numbers[i].hi=bits>>32
            host.mc_viewmodel_tick()
            expected=[fbits(scalar(host,name).value)for name in floats]
            expected.extend([integer(host,'mc_vm_swing_ticks').value&0xffffffff,integer(host,'mc_vm_swinging').value,Stack.in_dll(host,'mc_vm_stack').id,Stack.in_dll(host,'mc_vm_stack').count])
            actual=list(struct.unpack('<13I',probe.run(3,struct.pack('<'+'I'*len(words),*words),initial=tick==0)[:52]))
            assert actual==expected,('Thumb animation/movement tick',tick,actual,expected)
        import _ctypes;_ctypes.FreeLibrary(host._handle)
    print('PASS NARM Thumb: 40 exact atan, 36 source-validated matrices, 24 exact equip/swing/arm/actual-motion bob ticks; 32KiB stack guard')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler',type=Path,required=True)
    args=parser.parse_args();run(args.compiler.resolve())
