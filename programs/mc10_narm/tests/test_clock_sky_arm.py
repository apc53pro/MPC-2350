"""Production clock/sky code executed as NARM Thumb versus validated host code.

The existing host fixtures are separately tested against owned Java methods.
This probe targets compiler/codegen parity; it is not an RP2350 emulator.
"""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,math,random,struct,subprocess,sys,time
GAME=Path('C:/Users/enzob/OneDrive/Documentos/RP2350_GP_Computer/Firmware/resource_development/programs/mc10_narm')
HERE=Path(__file__).resolve().parent
sys.path.insert(0,'C:/Users/enzob/Documents/Codex/2026-10-13/co/work/arm_tools')
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def fbits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def dbits(v):return struct.unpack('<Q',struct.pack('<d',v))[0]
def words(values):return struct.pack('<'+'I'*len(values),*[v&0xffffffff for v in values])
class Pair(C.Structure):_fields_=[('lo',C.c_uint32),('hi',C.c_uint32)]
class Vertex(C.Structure):_fields_=[(x,C.c_float)for x in ('x','y','z','u','v','a')]
class Probe:
 def __init__(self,path,table):
  from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
  from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPEXC
  self.regs=UC_ARM_REG_R0,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPEXC
  raw=path.read_bytes();u32=lambda o:struct.unpack_from('<I',raw,o)[0];align=lambda n,a:(n+a-1)&~(a-1)
  self.text,self.arena,self.context,self.stack,self.stop=0x100000,0x2000000,0x3000000,0x4000000,0x3001000
  uc=self.uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB);self.entry=self.text+u32(0x18)
  uc.mem_map(self.text,align(max(u32(0x20),4096),4096));uc.mem_map(self.arena,0x800000)
  uc.mem_map(self.context,0x2000);uc.mem_map(self.stack,0x10000)
  uc.mem_write(self.text,raw[u32(0x1c):u32(0x1c)+u32(0x20)]);size=u32(0x2c);data=align(size,u32(0x3c))
  if size:uc.mem_write(self.arena,raw[u32(0x28):u32(0x28)+size])
  if u32(0x38):uc.mem_write(self.arena+data,raw[u32(0x34):u32(0x34)+u32(0x38)])
  for i in range(u32(0x54)):
   patch,target,kind,_=struct.unpack_from('<IIHH',raw,u32(0x50)+12*i)
   if not kind:continue
   value=self.arena+target if kind==1 else (self.text+target)|(kind==3)if kind in(2,3)else self.context
   uc.mem_write(self.arena+patch,struct.pack('<I',value))
  invoke=self.context+0x1200;video=self.context+0x1300;self.math_calls=0;self.blit_calls=0
  for at in (self.stop,invoke,video):uc.mem_write(at,b'\x70\x47')
  uc.mem_write(self.stack,b'\x5a'*64)
  uc.mem_write(self.context+8,words([self.context+0x100]));uc.mem_write(self.context+0x110,words([invoke|1]))
  uc.mem_write(self.context+56,words([self.context+0x200]));uc.mem_write(self.context+0x21c,words([video|1]))
  def hook(cpu,address,size,data):
   if address==self.stop:cpu.emu_stop();return
   command=cpu.reg_read(UC_ARM_REG_R1);args=cpu.reg_read(UC_ARM_REG_R2);count=cpu.reg_read(UC_ARM_REG_R3)
   if address==invoke:
    assert command in(6,7)and count==1,(command,count)
    value=struct.unpack('<f',bytes(cpu.mem_read(args,4)))[0]
    result=fbits(math.sin(value)if command==6 else math.cos(value));self.math_calls+=1
   else:
    assert address==video and command==6 and count==5,(hex(address),command,count)
    values=struct.unpack('<5I',bytes(cpu.mem_read(args,20)))
    assert values==(0,0,320,240,self.packed),('RGB332 blit ABI',values);self.blit_calls+=1;result=0
   cpu.reg_write(UC_ARM_REG_R0,result);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
  uc.hook_add(UC_HOOK_CODE,hook,begin=self.stop,end=video+4)
  self.control=self.call();packet=struct.unpack('<16I',bytes(uc.mem_read(self.control,64)))
  self.input,self.output,self.sine,self.canvas,self.texture,self.vertices,self.clip,self.projected,self.packed,self.camera=packet[1:11]
  uc.mem_write(self.sine,table);self.calls=0
 def call(self):
  r0,r9,r10,sp,lr,pc,fp=self.regs;u=self.uc
  u.reg_write(r9,self.arena);u.reg_write(r10,self.context);u.reg_write(sp,self.stack+32752)
  u.reg_write(lr,self.stop|1);u.reg_write(fp,1<<30)
  u.emu_start(self.entry|1,self.stop,count=150000000)
  assert u.reg_read(pc)==self.stop,('Thumb instruction budget exhausted',hex(u.reg_read(pc)))
  assert bytes(u.mem_read(self.stack,64))==b'\x5a'*64,'32 KiB stack guard damaged'
  return u.reg_read(r0)
 def run(self,mode,data=b''):
  self.uc.mem_write(self.control,words([mode]));self.uc.mem_write(self.input,data+bytes(512-len(data)))
  result=self.call();assert result==0,('NARM fixture status',result);self.calls+=1
  return bytes(self.uc.mem_read(self.output,256))

def validate_cosine_java(values,work):
 """Use the Java StrictMath fdlibm contract, not Python platform libm."""
 source='''import java.io.*;public class CosineOracle {public static void main(String[] args)throws Exception{BufferedReader r=new BufferedReader(new InputStreamReader(System.in));String s;while((s=r.readLine())!=null){long b=Long.parseUnsignedLong(s,16);System.out.println(Long.toHexString(Double.doubleToRawLongBits(StrictMath.cos(Double.longBitsToDouble(b)))));}}}'''
 path=work/'CosineOracle.java';path.write_text(source)
 result=subprocess.run(['javac','-J-Xms8m','-J-Xmx64m',str(path)],capture_output=True,text=True);assert result.returncode==0,result.stderr
 result=subprocess.run(['java','-Xms8m','-Xmx64m','-cp',str(work),'CosineOracle'],input=''.join(f'{dbits(v):016x}\n'for v in values),capture_output=True,text=True)
 assert result.returncode==0,result.stderr
 return [int(v,16)for v in result.stdout.splitlines()]

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--game',type=Path,default=GAME)
 ap.add_argument('--compiler',type=Path,default=GAME.parents[3]/'Compiler_For_NARM/narmcc.exe')
 ap.add_argument('--sky',type=Path,default=GAME/'src/mc10_sky.tc')
 ap.add_argument('--work',type=Path,default=HERE/'artifacts');a=ap.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
 started=time.perf_counter()
 def build(command):
  result=subprocess.run([str(x)for x in command],capture_output=True,text=True)
  assert result.returncode==0,result.stdout+result.stderr
  return result.stdout
 for name in ('world_clock','sky'):
  fixture=(a.game/'tests'/f'{name}_fixture.c').read_text()
  for module in ('mc10_u64.tc','mc10_java_double.tc','mc10_fp64.tc','mc10_paths.tc','mc10_math.tc','mc10_cos_celestial.tc','mc10_world_clock.tc','mc10_sky.tc'):
   path=a.sky if module=='mc10_sky.tc'else a.game/'src'/module
   fixture=fixture.replace('../src/'+module,path.as_posix())
  if name=='sky':
   fixture=fixture.replace('int mc_third_interpolated_eye_y(float partial){return mc_fp_bits(mc_runtime_player.pos_y.lo,mc_runtime_player.pos_y.hi);}','')
   fixture=fixture.replace('#include "'+(a.game/'src/mc10_cos_celestial.tc').as_posix()+'"',
     'struct MCJDouble mc_tp_old_position[3],mc_tp_tick_position[3];\n#include "'+(HERE/'third_eye_getter.tc').as_posix()+'"\n#include "'+(a.game/'src/mc10_cos_celestial.tc').as_posix()+'"')
  path=work/(name+'_fixture.c');path.write_text(fixture)
  build(['C:/msys64/mingw64/bin/gcc.exe','-shared','-O2','-fno-strict-aliasing','-ffp-contract=off',path,'-o',work/f'{name}.dll'])
 image=work/'clock_sky_arm.narm'
 arm_fixture=(HERE/'clock_sky_arm.tc').read_text().replace('#include "mc10_sky.tc"','#include "'+a.sky.resolve().as_posix()+'"')
 arm_path=work/'clock_sky_arm.tc';arm_path.write_text(arm_fixture)
 print(build([a.compiler,arm_path,'-I',HERE,'-I',a.game/'src','-O3','--stack-bytes','32768','-o',image]).strip(),flush=True)
 table=(a.game/'MC10/math10.dat').read_bytes()[16:];probe=Probe(image,table);rng=random.Random(290617)
 clock=C.CDLL(str(work/'world_clock.dll'));sky=C.CDLL(str(work/'sky.dll'))
 for lib in (clock,sky):C.memmove((C.c_uint32*65536).in_dll(lib,'mc_math_sine'),table,len(table))
 clock.clock_set.argtypes=[C.c_uint32,C.c_uint32,C.c_float,C.c_float];clock.mc_fp_bits.argtypes=[C.c_uint32,C.c_uint32]
 clock.mc_world_celestial_angle.argtypes=[C.c_float];clock.mc_world_celestial_angle.restype=C.c_float
 clock.mc_world_star_brightness.argtypes=[C.c_float];clock.mc_world_star_brightness.restype=C.c_float
 clock.mc_world_skylight_subtracted.argtypes=[C.c_float]
 clock.clock_low.restype=clock.clock_high.restype=C.c_uint32
 numeric=(Pair*16384).in_dll(clock,'mc_fp_values')
 sky.fixture_init.argtypes=[C.c_uint32,C.c_uint32,C.c_float,C.c_float,C.c_float]
 sky.mc_sky_colors.argtypes=[C.c_float];sky.mc_sky_biome_color.argtypes=[C.c_float];sky.mc_sky_biome_color.restype=C.c_uint32
 sky.mc_sky_vertex.argtypes=[C.c_int]+[C.c_float]*6+[C.c_int]
 sky.mc_sky_triangle.argtypes=[C.c_int]*4+[C.c_uint32,C.c_int]
 sky.mc_sky_polygon.argtypes=[C.c_int,C.c_int,C.c_uint32,C.c_int]
 sky.mc_sky_rgb.argtypes=[C.c_float]*3;sky.mc_sky_rgb.restype=C.c_uint32;sky.mc_sky_draw.argtypes=[C.c_float]
 canvas=(C.c_uint32*76800).in_dll(sky,'mc_sky_canvas');texture=(C.c_uint32*9216).in_dll(sky,'mc_sky_texture')
 projected=(Vertex*8).in_dll(sky,'mc_sky_projected');clipped=(Vertex*8).in_dll(sky,'mc_sky_clip');vertices=(Vertex*4).in_dll(sky,'mc_sky_input')
 camera=(C.c_float*18).in_dll(sky,'mc_render_camera')
 texture_bytes=words([((i*17)&255)|(((i*53)&255)<<8)|(((i*97)&255)<<16)|((0,64,128,255)[i&3]<<24)for i in range(9216)])
 C.memmove(texture,texture_bytes,len(texture_bytes));probe.uc.mem_write(probe.texture,texture_bytes)
 cosine=[]
 for edge in (0.,2**-27,.3,math.pi/4,math.pi/2,3*math.pi/4,math.pi):
  for value in (math.nextafter(edge,-math.inf),edge,math.nextafter(edge,math.inf)):
   if abs(value)<=math.pi:cosine.extend((value,-value))
 cosine += [rng.uniform(-math.pi,math.pi)for _ in range(80)]
 java_cosine=validate_cosine_java(cosine,work);assert len(java_cosine)==len(cosine)
 for value,java_expected in zip(cosine,java_cosine):
  raw=dbits(value);clock.clock_set(0,0,0,0);result=clock.mc_fp_cos_celestial(clock.mc_fp_bits(raw&0xffffffff,raw>>32))
  assert numeric[result].lo|(numeric[result].hi<<32)==java_expected,('Owned Java fdlibm cosine',value)
  expected=words([numeric[result].lo,numeric[result].hi,0]);assert probe.run(1,words([raw&0xffffffff,raw>>32]))[:12]==expected,('Thumb cosine',value)
 outside=dbits(math.nextafter(math.pi,math.inf));assert struct.unpack('<3I',probe.run(1,words([outside&0xffffffff,outside>>32]))[:12])==(0,0,1)
 times=[0,1,5999,6000,11999,12000,17999,18000,23999,24000,-1,-24000,-24001,(1<<63)-1,-(1<<63),0xffffffff,0xffffffffffffffff]
 times += [rng.randrange(-(1<<63),1<<63)for _ in range(31)]
 count_clock=0
 for t in times:
  for partial in (0.,.5,1.):
   rain=f32(rng.choice((0.,1.,.4)));thunder=f32(rng.choice((0.,1.,.7)));temp=f32(rng.uniform(-4,4));anaglyph=rng.randrange(2)
   lo=t&0xffffffff;hi=(t>>32)&0xffffffff;clock.clock_set(lo,hi,rain,thunder);sky.fixture_init(lo,hi,temp,rain,thunder)
   angle=clock.mc_world_celestial_angle(partial);C.c_float.in_dll(sky,'mc_sky_angle').value=angle
   C.c_int.in_dll(sky,'mc_options').value=anaglyph;sky.mc_sky_colors(partial)
   expected=[fbits(angle),clock.mc_world_moon_phase(),clock.mc_world_skylight_subtracted(partial),fbits(clock.mc_world_star_brightness(partial)),sky.mc_sky_biome_color(temp)]
   expected += [fbits(C.c_float.in_dll(sky,'mc_sky_'+n).value)for n in ('red','green','blue')]
   clock.mc_world_clock_tick();expected += [clock.clock_low(),clock.clock_high(),C.c_int.in_dll(clock,'mc_world_skylight').value]
   actual=probe.run(2,words([lo,hi,fbits(partial),fbits(rain),fbits(thunder),fbits(temp),anaglyph]))[:44]
   assert actual==words(expected),('Thumb clock/color/tick',t,partial,list(struct.unpack('<11I',actual)),expected)
   count_clock+=1
 def reset_canvas(color=0x204060):
  background=struct.pack('<I',color)*76800;C.memmove(canvas,background,len(background));probe.uc.mem_write(probe.canvas,background)
  C.memset(projected,0,C.sizeof(projected));C.memset(clipped,0,C.sizeof(clipped));C.memset(vertices,0,C.sizeof(vertices))
  probe.uc.mem_write(probe.projected,bytes(C.sizeof(projected)));probe.uc.mem_write(probe.clip,bytes(C.sizeof(clipped)));probe.uc.mem_write(probe.vertices,bytes(C.sizeof(vertices)))
 def same_canvas(label):assert bytes(probe.uc.mem_read(probe.canvas,307200))==bytes(canvas),label
 # Top-left additive ownership shared diagonal, reversed winding and clipping.
 reset_canvas(0)
 for data in ((0,0,1,0,0,1,10,0,1,0,0,1,10,10,1,0,0,1),(0,0,1,0,0,1,10,10,1,0,0,1,0,10,1,0,0,1)):
  raw=struct.pack('<18f',*data);C.memmove(projected,raw,len(raw));probe.uc.mem_write(probe.projected,raw)
  sky.mc_sky_triangle(0,1,2,-1,0x202020,1);probe.run(3,words([-1,0x202020,1]));same_canvas('Thumb additive top-left seam')
 assert sum(v!=0 for v in canvas)==100 and all(canvas[y*320+x]==0x202020 for y in range(10)for x in range(10))
 triangles=0
 for i in range(72):
  reset_canvas(rng.randrange(1<<24));base_x=rng.choice((-6,3,150,315));base_y=rng.choice((-5,2,110,237));verts=[]
  for j in range(3):
   x=base_x+rng.uniform(-3,15);y=base_y+rng.uniform(-3,15);inv=rng.choice((.5,1.,2.,4.));u=rng.uniform(-150,160);v=rng.uniform(-80,90);alpha=rng.choice((0.,.25,.5,1.))
   verts.extend((x,y,inv,u*inv,v*inv,alpha*inv))
  raw=struct.pack('<18f',*verts);C.memmove(projected,raw,len(raw));probe.uc.mem_write(probe.projected,raw)
  tex=(-1,0,1)[i%3];color=rng.randrange(1<<24);add=i&1
  sky.mc_sky_triangle(0,1,2,tex,color,add);probe.run(3,words([tex,color,add]));same_canvas(('Thumb triangle/UV/blend',i));triangles+=1
 camera_values=[1,0,0,0,0,1,0,0,0,0,1,0,20,20,160,120,0,0]
 C.memmove(camera,struct.pack('<18f',*camera_values),72);probe.uc.mem_write(probe.camera,struct.pack('<18f',*camera_values))
 polygons=0
 for i in range(24):
  reset_canvas(rng.randrange(1<<24));quad=[]
  for j,(x,y)in enumerate(((-.04,-.03),(.04,-.03),(.04,.03),(-.04,.03))):
   quad.extend((x,y,(.02,.05,.08,.13)[(j+i)%4] if i%3 else .01,(j&1)*32,(j>>1)*32,rng.choice((.25,1.))))
  raw=struct.pack('<24f',*quad);C.memmove(vertices,raw,len(raw));probe.uc.mem_write(probe.vertices,raw)
  tex=(-1,0,1)[i%3];color=rng.randrange(1<<24);add=i&1
  sky.mc_sky_polygon(4,tex,color,add);probe.run(4,words([4,tex,color,add]));same_canvas(('Thumb near clip/projection/perspective',i))
  assert bytes(probe.uc.mem_read(probe.clip,C.sizeof(clipped)))==bytes(clipped),('Thumb clipped vertices',i)
  assert bytes(probe.uc.mem_read(probe.projected,C.sizeof(projected)))==bytes(projected),('Thumb projected vertices',i)
  polygons+=1
 camera_values=[f32(rng.uniform(-2,2))for _ in range(18)];raw=struct.pack('<18f',*camera_values);C.memmove(camera,raw,72);probe.uc.mem_write(probe.camera,raw)
 vertex_cases=0
 for i in range(36):
  values=[f32(rng.uniform(-150,150))for _ in range(3)]+[f32(rng.uniform(-32,128))for _ in range(2)]+[f32(rng.random())]
  angle=f32(rng.random());sin=f32(rng.uniform(-1,1));cos=f32(rng.uniform(-1,1));rotation=i%3
  for name,v in (('angle',angle),('sin',sin),('cos',cos)):C.c_float.in_dll(sky,'mc_sky_'+name).value=v
  sky.mc_sky_vertex(0,*values,rotation);probe.run(5,words([*[fbits(v)for v in values],rotation,fbits(angle),fbits(sin),fbits(cos)]))
  assert bytes(probe.uc.mem_read(probe.vertices,24))==bytes(vertices)[:24],('Thumb vertex matrix/rotation',i);vertex_cases+=1
 for i in range(24):
  rgb=[f32(rng.uniform(-1,2))for _ in range(3)];actual=struct.unpack('<I',probe.run(7,words([fbits(v)for v in rgb]))[:4])[0]
  assert actual==sky.mc_sky_rgb(*rgb),('Thumb RGB channel rounding/clamp',rgb)
 old_positions=(Pair*3).in_dll(sky,'mc_tp_old_position');tick_positions=(Pair*3).in_dll(sky,'mc_tp_tick_position')
 sky.mc_sky_lower.argtypes=[C.c_float]
 lower_cases=0
 camera_values=[1,0,0,0,0,1,0,0,0,0,1,0,.001,.001,160,120,0,0];raw=struct.pack('<18f',*camera_values);C.memmove(camera,raw,72);probe.uc.mem_write(probe.camera,raw)
 for old,current,partial in ((62.,64.,0.),(62.,64.,.5),(62.,64.,1.),(0.,1.,.5),(-1.,-10.,.25),(74.,80.,.75),(math.nextafter(63.,0.),63.,0.),(63.,math.nextafter(63.,math.inf),1.)):
  reset_canvas(0x765432);ob=dbits(old);cb=dbits(current);old_positions[1]=Pair(ob&0xffffffff,ob>>32);tick_positions[1]=Pair(cb&0xffffffff,cb>>32)
  for name,value in (('red',.3),('green',.6),('blue',.8)):C.c_float.in_dll(sky,'mc_sky_'+name).value=value
  data=words([ob&0xffffffff,ob>>32,cb&0xffffffff,cb>>32,fbits(partial),fbits(.3),fbits(.6),fbits(.8)])
  sky.mc_sky_lower(partial);probe.run(8,data);same_canvas(('Thumb source lower horizon/blackbox',old,current,partial))
  expected=cb if partial==1.0 else dbits(old+(current-old)*partial)
  assert probe.run(9,data)[:8]==struct.pack('<Q',expected),('Thumb binary64 source interpolated eye',old,current,partial)
  lower_cases+=1
 # Exercise the actual production draw->packing path, with geometry outside
 # the near plane. This isolates channel packing from renderer geometry.
 C.memset(camera,0,72);probe.uc.mem_write(probe.camera,bytes(72));packed=(C.c_uint32*19200).in_dll(sky,'mc_asset_pixels')
 for t,temp,rain,thunder,ana in ((6000,.7,0,0,0),(12000,.2,.4,.7,0),(18000,-.5,0,0,1)):
  sky.fixture_init(t,0,temp,rain,thunder);C.c_int.in_dll(sky,'mc_options').value=ana
  C.c_int.in_dll(sky,'mc_sky_ready').value=1;C.c_int.in_dll(sky,'mc_sky_count').value=0
  sky.mc_sky_draw(0);probe.run(6,words([t,0,fbits(0),fbits(rain),fbits(thunder),fbits(temp),ana]));same_canvas(('Thumb full sky background',t))
  actual=bytes(probe.uc.mem_read(probe.packed,76800));assert actual==bytes(packed),('Thumb final RGB332 packing',t)
  assert actual==bytes(((v&0xe0)|((v>>11)&0x1c)|((v>>22)&3))for v in canvas),('Independent RGB332 packing',t)
 audit={'passed':True,'cosine_binary64_cases':len(cosine)+1,'clock_color_tick_cases':count_clock,'triangle_cases':triangles+2,
  'clipped_polygon_cases':polygons,'vertex_matrix_cases':vertex_cases,'rgb_rounding_cases':24,'lower_horizon_cases':lower_cases,'packed_frame_bytes':76800*3,
  'thumb_calls':probe.calls,'stack_guard_bytes':32768,'firmware_service_calls':0,
  'emulated_math_abi_calls':probe.math_calls,'validated_blit_abi_calls':probe.blit_calls,'elapsed_host_seconds':time.perf_counter()-started,
  'source_sha256':{n:hashlib.sha256((a.sky if n=='mc10_sky.tc'else a.game/'src'/n).read_bytes()).hexdigest()for n in ('mc10_cos_celestial.tc','mc10_world_clock.tc','mc10_sky.tc')}}
 (work/'clock_sky_arm_audit.json').write_text(json.dumps(audit,indent=2)+'\n');print(json.dumps(audit))
if __name__=='__main__':main()

