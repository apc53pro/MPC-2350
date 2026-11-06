"""Attach the production numeric-service logic to isolated Thumb CPU tests.

The DCP device itself is not emulated. This checks the compiler/service ABI and
numeric results using the portable production header on the host.
"""
import atexit
import ctypes as C
import os
from pathlib import Path
import struct
import subprocess
import tempfile

_library=None
def library():
 global _library
 if _library is not None:return _library
 explicit=os.environ.get('MC10_NUMERIC_RUNTIME_DLL')
 if explicit:lib=C.CDLL(explicit)
 else:
  root=Path(os.environ.get('MC10_NUMERIC_HEADER',str(Path(__file__).resolve().parents[3]/'native_numeric_runtime.h')))
  if not root.is_file():raise RuntimeError('Set MC10_NUMERIC_HEADER to the production native_numeric_runtime.h for API 1.17 CPU tests')
  temporary=tempfile.TemporaryDirectory(prefix='narm-numeric-service-',ignore_cleanup_errors=True)
  source=Path(temporary.name)/'numeric.cpp';dll=source.with_suffix('.dll')
  source.write_text('#include "'+root.as_posix()+'"\nextern "C" int numeric(unsigned op,unsigned *words){return narm_numeric::execute(op,words);}\n')
  built=subprocess.run(['g++','-shared','-O2','-std=c++17','-ffp-contract=off',str(source),'-o',str(dll)],capture_output=True,text=True)
  if built.returncode:raise RuntimeError(built.stderr)
  lib=C.CDLL(str(dll))
  def cleanup():
   if os.name=='nt':
    import _ctypes
    _ctypes.FreeLibrary(lib._handle)
   temporary.cleanup()
  atexit.register(cleanup)
 lib.numeric.argtypes=[C.c_uint32,C.POINTER(C.c_uint32)];lib.numeric.restype=C.c_int
 _library=lib;return lib

def invoke(cpu,arguments,count):
 if count!=2:raise AssertionError(('numeric argument count',count))
 op,pointer=struct.unpack('<2I',bytes(cpu.mem_read(arguments,8)))
 words=(C.c_uint32*6).from_buffer_copy(bytes(cpu.mem_read(pointer,24)))
 if library().numeric(op,words)!=1:raise AssertionError(('invalid numeric operation',op))
 cpu.mem_write(pointer,bytes(words));return 0

def install(cpu,context):
 from unicorn import UC_HOOK_CODE
 from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_LR,UC_ARM_REG_PC
 address=context+0x1800
 cpu.mem_write(address,b'\x70\x47')
 cpu.mem_write(context+8,struct.pack('<I',context+0x100))
 cpu.mem_write(context+0x110,struct.pack('<I',address|1))
 def hook(uc,a,size,data):
  if uc.reg_read(UC_ARM_REG_R1)!=0x12:raise AssertionError('unexpected service in isolated numeric CPU probe')
  result=invoke(uc,uc.reg_read(UC_ARM_REG_R2),uc.reg_read(UC_ARM_REG_R3))
  uc.reg_write(UC_ARM_REG_R0,result);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
 cpu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
