"""Execute zero-argument graphics call lowering in Thumb, not an MCU emulator."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def execute(path,result,interface=True):
    from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
    from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,
        UC_ARM_REG_R3,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_SP,UC_ARM_REG_LR,
        UC_ARM_REG_PC,UC_ARM_REG_FPEXC)
    image=path.read_bytes();u32=lambda off:struct.unpack_from('<I',image,off)[0]
    align=lambda n,a:(n+a-1)&~(a-1)
    text,arena,context,stack,stop,submit=0x100000,0x2000000,0x3000000,0x4000000,0x3001000,0x3001100
    uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB)
    uc.mem_map(text,align(max(u32(0x20),4096),4096));uc.mem_map(arena,0x800000)
    uc.mem_map(context,0x2000);uc.mem_map(stack,0x10000)
    uc.mem_write(text,image[u32(0x1c):u32(0x1c)+u32(0x20)])
    robytes=u32(0x2c);dataoff=align(robytes,u32(0x3c))
    if robytes:uc.mem_write(arena,image[u32(0x28):u32(0x28)+robytes])
    if u32(0x38):uc.mem_write(arena+dataoff,image[u32(0x34):u32(0x34)+u32(0x38)])
    for i in range(u32(0x54)):
        patch,target,kind,_=struct.unpack_from('<IIHH',image,u32(0x50)+i*12)
        if not kind:continue
        value=arena+target if kind==1 else (text+target)|(kind==3) if kind in (2,3) else context
        uc.mem_write(arena+patch,struct.pack('<I',value))
    uc.mem_write(stop,b'\x70\x47');uc.mem_write(submit,b'\x70\x47')
    if interface:
        uc.mem_write(context+56,struct.pack('<I',context+0x100))
        uc.mem_write(context+0x100+28,struct.pack('<I',submit|1))
    uc.reg_write(UC_ARM_REG_R9,arena);uc.reg_write(UC_ARM_REG_R10,context)
    uc.reg_write(UC_ARM_REG_SP,stack+0xf000);uc.reg_write(UC_ARM_REG_LR,stop|1)
    uc.reg_write(UC_ARM_REG_FPEXC,1<<30);calls=[]
    def hook(cpu,address,size,data):
        if address==stop:cpu.emu_stop();return
        if address!=submit:return
        assert cpu.reg_read(UC_ARM_REG_R0)==context
        assert cpu.reg_read(UC_ARM_REG_R1)==0x0009
        assert cpu.reg_read(UC_ARM_REG_R3)==0
        calls.append(address)
        cpu.reg_write(UC_ARM_REG_R0,result&0xffffffff)
        cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_CODE,hook)
    uc.emu_start((text+u32(0x18))|1,stop,count=2000000)
    assert uc.reg_read(UC_ARM_REG_PC)==stop
    assert len(calls)==int(interface)
    assert uc.reg_read(UC_ARM_REG_R0)==(result if interface else -1)&0xffffffff


def run(compiler,sdk):
    total=0
    with tempfile.TemporaryDirectory(prefix='narm-depth-clear-') as tmp:
        tmp=Path(tmp);source=tmp/'clear.tc'
        source.write_text('#include "narm.tch"\nint main(void){int pad[96];pad[95]=17;'
                          'int result=narm_half_clear_depth();return result+pad[95]-17;}\n')
        for level in range(4):
            output=tmp/f'clear-{level}.narm'
            built=subprocess.run([str(compiler),str(source),'-I',str(sdk),f'-O{level}','-o',str(output)],capture_output=True,text=True)
            assert built.returncode==0,built.stderr
            for result in (0,-2,-3,-4):execute(output,result);total+=1
            execute(output,0,False);total+=1
        source.write_text('#include "narm.tch"\nint main(void){return narm_half_clear_depth(3);}\n')
        built=subprocess.run([str(compiler),str(source),'-I',str(sdk),'-O3','-o',str(tmp/'invalid.narm')],capture_output=True,text=True)
        assert built.returncode!=0 and ('argument' in built.stderr.lower() or 'arity' in built.stderr.lower()),built.stderr
    print(f'PASS depth-clear compiler: {total} executed Thumb probes, zero argument command9, errors/interface fallback, O0..O3; wrong arity rejected')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--sdk',type=Path,default=ROOT)
    args=parser.parse_args();run(args.compiler.resolve(),args.sdk.resolve())
