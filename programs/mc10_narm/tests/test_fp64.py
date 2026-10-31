"""Execute the shared integer binary64 adapter against IEEE-754 host vectors."""
import ctypes,math,random,struct,subprocess,tempfile
from pathlib import Path
PROJECT=Path(__file__).resolve().parents[1]
def bits(value):return struct.unpack('<Q',struct.pack('<d',value))[0]
class Pair(ctypes.Structure):_fields_=[('lo',ctypes.c_uint32),('hi',ctypes.c_uint32)]
def run():
 with tempfile.TemporaryDirectory(prefix='mc10-fp64-') as tmp:
  tmp=Path(tmp);src=tmp/'fp.c';dll=tmp/'fp.dll'
  src.write_text('typedef unsigned int uint;\n'+''.join(f'#include "{(PROJECT/p).as_posix()}"\n' for p in ['src/mc10_u64.tc','src/mc10_java_double.tc','src/mc10_fp64.tc']),encoding='utf-8')
  subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(src),'-o',str(dll)],check=True,capture_output=True)
  lib=ctypes.CDLL(str(dll));values=(Pair*16384).in_dll(lib,'mc_fp_values')
  lib.mc_fp_f32.argtypes=[ctypes.c_float];lib.mc_fp_to_f32.restype=ctypes.c_float
  def operand(v):b=bits(v);return lib.mc_fp_bits(ctypes.c_uint32(b),ctypes.c_uint32(b>>32))
  def result(i):return values[i].lo|(values[i].hi<<32)
  rng=random.Random(100);vectors=[0.,-0.,.08,.42,.98,-150.8718206735353,73.62000000476837,350.6435452798552,1e-300,1e300]
  vectors += [math.ldexp(rng.uniform(-2,2),rng.randrange(-500,501)) for _ in range(250)]
  count=0
  for a in vectors:
   for b in rng.sample(vectors,12):
    for name,expected in [('add',a+b),('sub',a-b),('mul',a*b)]+([('div',a/b)] if b else []):
     if not math.isfinite(expected):continue
     lib.mc_fp_init();aa=operand(a);bb=operand(b);out=getattr(lib,'mc_fp_'+name)(aa,bb)
     assert result(out)==bits(expected),(name,a,b,hex(result(out)),hex(bits(expected)))
     count+=1
   if a>=0:
    lib.mc_fp_init();out=lib.mc_fp_sqrt(operand(a))
    assert result(out)==bits(math.sqrt(a)),('sqrt',a,hex(result(out)),hex(bits(math.sqrt(a))))
    count+=1
  for v in [-2147483648,-30000000,-1,0,1,30000000,2147483647]:
   lib.mc_fp_init();assert result(lib.mc_fp_i32(v))==bits(float(v))
  for v in [0.,-0.,.3,.6,1.62,1.8,.98,65535.,-30000000.,1e-35]:
   fv=struct.unpack('<f',struct.pack('<f',v))[0];lib.mc_fp_init()
   assert result(lib.mc_fp_f32(fv))==bits(fv)
   assert struct.pack('<f',lib.mc_fp_to_f32(operand(v)))==struct.pack('<f',v)
  print(f'Binary64 integer adapter: PASS {count} exact add/sub/mul/div/sqrt vectors')
  import _ctypes
  _ctypes.FreeLibrary(lib._handle)
if __name__=='__main__':run()
