"""Owned RenderGlobal lower-sky geometry and EntityLiving Y interpolation."""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,math,random,re,struct,subprocess
HERE=Path(__file__).resolve().parent
GAME=Path('C:/Users/enzob/OneDrive/Documentos/RP2350_GP_Computer/Firmware/resource_development/programs/mc10_narm')
ORIGINAL=Path('C:/Users/enzob/AppData/Roaming/.minecraft/versions/1.0/minecraft/src/net/minecraft/src')
def method(text,name):
 m=re.search(r'(?m)^\s*(?:public |private |protected )?(?:void|int|uint|float|Vec3D)\s+'+name+r'\(',text);assert m,name
 start=m.start();end=text.index('{',start)+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
def dbits(x):return struct.unpack('<Q',struct.pack('<d',x))[0]
def fbits(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--game',type=Path,default=GAME);ap.add_argument('--oracle',type=Path,default=ORIGINAL)
 ap.add_argument('--sky',type=Path,default=GAME/'src/mc10_sky.tc');ap.add_argument('--work',type=Path,default=HERE/'lower_artifacts');a=ap.parse_args()
 work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
 def run(args,**kw):
  r=subprocess.run([str(x)for x in args],capture_output=True,text=True,**kw);assert r.returncode==0,r.stdout+r.stderr;return r.stdout
 src=(a.oracle/'RenderGlobal.java').read_text();start=src.index('double var23 = this.mc.thePlayer.getPosition(var1).yCoord - 63.0D;')
 end=src.index('GL11.glEnable(GL11.GL_TEXTURE_2D);',start);body=src[start:end]
 position=method((a.oracle/'EntityLiving.java').read_text(),'getPosition')
 java=r'''
import java.io.*;import java.util.*;
class Vec3D {double yCoord;static Vec3D createVector(double x,double y,double z){Vec3D v=new Vec3D();v.yCoord=y;return v;}}
class Player {double prevPosX,prevPosY,prevPosZ,posX,posY,posZ; __POSITION__ }
class MC {Player thePlayer=new Player();}
class Provider {boolean func_28112_c(){return true;}}
class World {Provider worldProvider=new Provider();}
class GL11 {
 static final int GL_TEXTURE_2D=1;static float translation;static int color;static float r,g,b;
 static void glPushMatrix(){}static void glPopMatrix(){translation=0;}
 static void glTranslatef(float x,float y,float z){translation=y;}
 static void glColor3f(float a,float c,float d){r=a;g=c;b=d;}
 static int rgb(float a,float b,float c){return ((int)(a*255F+.5F))|((int)(b*255F+.5F)<<8)|((int)(c*255F+.5F)<<16);}
 static void glCallList(int list){LowerOracle.out.append("P,").append(Integer.toUnsignedString(Float.floatToRawIntBits(-16F+translation))).append(',').append(Integer.toUnsignedString(rgb(r,g,b))).append(';');}
}
class Tessellator {
 int count;float[] v=new float[12];void startDrawingQuads(){count=0;}void setColorRGBA_I(int color,int alpha){GL11.r=GL11.g=GL11.b=0;}
 void addVertex(double x,double y,double z){v[count++]=(float)x;v[count++]=(float)y;v[count++]=(float)z;if(count==12){LowerOracle.out.append("Q");for(float a:v)LowerOracle.out.append(',').append(Integer.toUnsignedString(Float.floatToRawIntBits(a)));LowerOracle.out.append(',').append(Integer.toUnsignedString(GL11.rgb(GL11.r,GL11.g,GL11.b))).append(';');count=0;}}
 void draw(){}
}
public class LowerOracle {
 static StringBuilder out;MC mc=new MC();World worldObj=new World();int glSkyList2=1;
 void lower(float var1,float var3,float var4,float var5){Tessellator var21=new Tessellator();float var9,var10,var11;GL11.glColor3f(0,0,0);__LOWER__}
 public static void main(String[] args)throws Exception{Scanner in=new Scanner(System.in);LowerOracle o=new LowerOracle();while(in.hasNext()){
 o.mc.thePlayer.prevPosY=Double.longBitsToDouble(Long.parseUnsignedLong(in.next(),16));o.mc.thePlayer.posY=Double.longBitsToDouble(Long.parseUnsignedLong(in.next(),16));float p=Float.intBitsToFloat(Integer.parseUnsignedInt(in.next(),16));float r=Float.intBitsToFloat(Integer.parseUnsignedInt(in.next(),16)),g=Float.intBitsToFloat(Integer.parseUnsignedInt(in.next(),16)),b=Float.intBitsToFloat(Integer.parseUnsignedInt(in.next(),16));
 out=new StringBuilder();o.lower(p,r,g,b);System.out.println(Long.toHexString(Double.doubleToRawLongBits(o.mc.thePlayer.getPosition(p).yCoord))+":"+out);}}
}
'''.replace('__POSITION__',position).replace('__LOWER__',body)
 (work/'LowerOracle.java').write_text(java);run(['javac','-J-Xms8m','-J-Xmx64m',work/'LowerOracle.java'])
 code='typedef unsigned int uint;\n'
 for n in ('mc10_u64.tc','mc10_java_double.tc','mc10_fp64.tc'):code+=(a.game/'src'/n).read_text()+'\n'
 code+='''struct MCJDouble mc_tp_old_position[3],mc_tp_tick_position[3];
 float mc_sky_red,mc_sky_green,mc_sky_blue;float captured[8*12];uint captured_color[8];int captured_type[8],captured_count;float pending[12];
 void mc_sky_plane(float y,uint color){int i;int at=captured_count++;captured_type[at]=1;captured_color[at]=color;for(i=0;i<12;i++)captured[at*12+i]=0;captured[at*12]=y;}
 void mc_sky_vertex(int i,float x,float y,float z,float u,float v,float a,int rotation){pending[i*3]=x;pending[i*3+1]=y;pending[i*3+2]=z;}
 void mc_sky_polygon(int n,int texture,uint color,int additive){int i;int at=captured_count++;captured_type[at]=2;captured_color[at]=color;for(i=0;i<12;i++)captured[at*12+i]=pending[i];}
 '''
 code+=(HERE/'third_eye_getter.tc').read_text()+'\n'+method(a.sky.read_text(),'mc_sky_rgb')+'\n'+method(a.sky.read_text(),'mc_sky_lower')+'''
 void fixture(uint oldlo,uint oldhi,uint lo,uint hi,float p,float r,float g,float b){mc_fp_init();mc_tp_old_position[1].lo=oldlo;mc_tp_old_position[1].hi=oldhi;mc_tp_tick_position[1].lo=lo;mc_tp_tick_position[1].hi=hi;mc_sky_red=r;mc_sky_green=g;mc_sky_blue=b;captured_count=0;mc_sky_lower(p);}
 uint eye(uint *high,float partial){mc_fp_reset();int v=mc_third_interpolated_eye_y(partial);*high=mc_fp_values[v].hi;return mc_fp_values[v].lo;}
 '''
 (work/'lower.c').write_text(code);run(['gcc','-shared','-O2','-fno-strict-aliasing','-ffp-contract=off',work/'lower.c','-o',work/'lower.dll'])
 lib=C.CDLL(str(work/'lower.dll'));lib.fixture.argtypes=[C.c_uint32]*4+[C.c_float]*4;lib.eye.argtypes=[C.POINTER(C.c_uint32),C.c_float];lib.eye.restype=C.c_uint32
 rng=random.Random(2910);rows=[]
 values=[-100.,0.,1.,62.,math.nextafter(63.,0.),63.,math.nextafter(63.,math.inf),64.,74.,128.,1e6]
 for old in values:
  for current in values:
   for p in (0.,.5,1.):rows.append((old,current,p,.3,.6,.8))
 rows += [(rng.uniform(-100,150),rng.uniform(-100,150),rng.random(),rng.random(),rng.random(),rng.random())for _ in range(64)]
 inputs=''.join(f'{dbits(old):016x} {dbits(cur):016x} {fbits(p):08x} {fbits(r):08x} {fbits(g):08x} {fbits(b):08x}\n'for old,cur,p,r,g,b in rows)
 output=run(['java','-Xms8m','-Xmx64m','-cp',work,'LowerOracle'],input=inputs).splitlines();assert len(output)==len(rows)
 coordinates=(C.c_uint32*(8*12)).in_dll(lib,'captured');colors=(C.c_uint32*8).in_dll(lib,'captured_color');types=(C.c_int*8).in_dll(lib,'captured_type')
 records=0
 for row,line in zip(rows,output):
  old,cur,p,r,g,b=row;ob=dbits(old);cb=dbits(cur);lib.fixture(ob&0xffffffff,ob>>32,cb&0xffffffff,cb>>32,p,r,g,b)
  eye,geometry=line.split(':');hi=C.c_uint32();lo=lib.eye(C.byref(hi),p);assert lo|(hi.value<<32)==int(eye,16),('interpolated Y',row)
  expected=geometry.split(';')[:-1];count=C.c_int.in_dll(lib,'captured_count').value;assert count==len(expected),(row,count,expected)
  for i,record in enumerate(expected):
   fields=record.split(',');kind=fields[0];values=list(map(int,fields[1:]));actual=[coordinates[i*12]]if kind=='P'else list(coordinates[i*12:i*12+12]);actual.append(colors[i])
   assert types[i]==(1 if kind=='P'else 2)and actual==values,('source lower sky record',row,i,actual,values)
   records+=1
 audit={'passed':True,'owned_source_cases':len(rows),'geometry_records':records,'binary64_interpolation_exact':True,'new_static_bytes':0,
  'owned_oracle_sha256':{n:hashlib.sha256((a.oracle/n).read_bytes()).hexdigest()for n in ('RenderGlobal.java','EntityLiving.java')},
  'candidate_sha256':{a.sky.name:hashlib.sha256(a.sky.read_bytes()).hexdigest(),'third_eye_getter.tc':hashlib.sha256((HERE/'third_eye_getter.tc').read_bytes()).hexdigest()}}
 (work/'lower_sky_audit.json').write_text(json.dumps(audit,indent=2)+'\n');print(json.dumps(audit))
if __name__=='__main__':main()

