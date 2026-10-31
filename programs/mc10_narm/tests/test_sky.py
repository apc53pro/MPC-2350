"""Owned Java sky/HSB values, converted resources, blend seams and clipping."""
import ctypes as C, json, os, random, struct, subprocess, tempfile, zlib
from pathlib import Path
from test_world_clock import ROOT,SOURCE,method,bits32
def run():
    rng=random.Random(2903)
    with tempfile.TemporaryDirectory(prefix='mc10-sky-test-') as tmp:
        tmp=Path(tmp);dll=tmp/'sky.dll'
        built=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing','-ffp-contract=off',str(ROOT/'tests/sky_fixture.c'),'-o',str(dll)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        lib=C.CDLL(str(dll));lib.fixture_init.argtypes=[C.c_uint32,C.c_uint32,C.c_float,C.c_float,C.c_float]
        lib.mc_sky_colors.argtypes=[C.c_float];lib.mc_sky_biome_color.argtypes=[C.c_float];lib.mc_sky_biome_color.restype=C.c_uint32
        lib.fixture_triangle.argtypes=[C.POINTER(C.c_float),C.c_uint32,C.c_int]
        table=(C.c_uint32*65536).in_dll(lib,'mc_math_sine');C.memmove(table,(ROOT/'MC10/math10.dat').read_bytes()[16:],262144)
        raw=(ROOT/'MC10/sky10.dat').read_bytes();header=struct.unpack_from('<8I',raw)
        assert header[:4]==(0x4b53434d,1,780,32) and header[4]==36864 and header[5]==len(raw)-32 and zlib.crc32(raw[32:])==header[6]
        name=(str(ROOT/'MC10/sky10.dat')+'\0').encode();C.memmove((C.c_char*1024).in_dll(lib,'fixture_sky_path'),name,len(name));assert lib.mc_sky_init()==1
        assert bytes((C.c_uint32*9216).in_dll(lib,'mc_sky_texture'))==raw[32:36896]
        assert bytes((C.c_float*(1500*12)).in_dll(lib,'mc_sky_stars'))[:780*48]==raw[36896:]
        # Extract the real methods at test time. Only input adapters are ours.
        sky=method((SOURCE/'World.java').read_text(),'getSkyColor')
        biome=method((SOURCE/'BiomeGenBase.java').read_text(),'getSkyColorByTemp')
        provider=method((SOURCE/'WorldProvider.java').read_text(),'calculateCelestialAngle')
        sky=sky.replace('this.worldObj','this')
        java='''import java.io.*;import java.util.*;import java.awt.Color;
class Vec3D {double xCoord,yCoord,zCoord;static Vec3D createVector(double x,double y,double z){Vec3D v=new Vec3D();v.xCoord=x;v.yCoord=y;v.zCoord=z;return v;}}
class Entity {double posX,posZ;}
class MathHelper {static float[] sine=new float[65536];static float cos(float v){return sine[(int)(v*10430.378F+16384.0F)&65535];}static int floor_double(double v){int n=(int)v;return v<n?n-1:n;}}
public class SkyOracle {
long time;float rain,thunder,temperature;int lightningFlash;
float getCelestialAngle(float p){return calculateCelestialAngle(time,p);}float getRainStrength(float p){return rain;}float getWeightedThunderStrength(float p){return rain*thunder;}
SkyOracle getWorldChunkManager(){return this;}SkyOracle getBiomeGenAt(int x,int z){return this;}float func_35554_b(int x,int y,int z){return temperature;}
METHODS
public static void main(String[] args)throws Exception {DataInputStream in=new DataInputStream(new FileInputStream(args[0]));in.skipBytes(16);for(int i=0;i<65536;i++)MathHelper.sine[i]=Float.intBitsToFloat(Integer.reverseBytes(in.readInt()));
Scanner s=new Scanner(new File(args[1]));s.useLocale(Locale.ROOT);SkyOracle o=new SkyOracle();while(s.hasNextLong()){o.time=s.nextLong();o.temperature=s.nextFloat();o.rain=s.nextFloat();o.thunder=s.nextFloat();Vec3D v=o.getSkyColor(new Entity(),0);System.out.println(Integer.toUnsignedString(o.getSkyColorByTemp(o.temperature))+","+Integer.toUnsignedString(Float.floatToRawIntBits((float)v.xCoord))+","+Integer.toUnsignedString(Float.floatToRawIntBits((float)v.yCoord))+","+Integer.toUnsignedString(Float.floatToRawIntBits((float)v.zCoord)));}}}
'''.replace('METHODS',provider+'\n'+biome+'\n'+sky)
        rows=[(t,temperature,rain,thunder) for t in (0,6000,12000,18000,23999,-1) for temperature in (-3.,-.5,0.,.5,1.,3.) for rain,thunder in ((0.,0.),(1.,1.),(.3,.7))]
        rows += [(rng.randrange(-1<<50,1<<50),rng.uniform(-5,5),rng.random(),rng.random()) for _ in range(128)]
        (tmp/'SkyOracle.java').write_text(java);(tmp/'input.txt').write_text(''.join(f'{t} {v} {r} {h}\n' for t,v,r,h in rows))
        env=os.environ.copy();env['JAVA_TOOL_OPTIONS']='-Xms8m -Xmx64m -XX:ReservedCodeCacheSize=16m -XX:CompressedClassSpaceSize=32m -Xss256k'
        result=subprocess.run(['javac',str(tmp/'SkyOracle.java')],capture_output=True,text=True,env=env);assert result.returncode==0,result.stderr
        out=subprocess.run(['java','-cp',str(tmp),'SkyOracle',str(ROOT/'MC10/math10.dat'),str(tmp/'input.txt')],check=True,capture_output=True,text=True,env=env).stdout.splitlines()
        for row,line in zip(rows,out):
            t,temp,rain,thunder=row;lib.fixture_init(t&0xffffffff,(t>>32)&0xffffffff,temp,rain,thunder);lib.mc_sky_colors(0)
            expected=list(map(int,line.split(',')));rgb=lib.mc_sky_biome_color(temp)
            reordered=0xff000000|((rgb&255)<<16)|(rgb&0xff00)|((rgb>>16)&255)
            got=[reordered]+[bits32(C.c_float.in_dll(lib,'mc_sky_'+n).value)for n in ('red','green','blue')]
            assert got==expected,(row,got,expected)
        assert len(out)==len(rows)
        canvas=(C.c_uint32*76800).in_dll(lib,'mc_sky_canvas');C.memset(canvas,0,C.sizeof(canvas))
        # Shared diagonal is owned by exactly one triangle (no additive seam).
        lib.fixture_triangle((C.c_float*9)(0,0,1,10,0,1,10,10,1),0x202020,1)
        lib.fixture_triangle((C.c_float*9)(0,0,1,10,10,1,0,10,1),0x202020,1)
        assert all(canvas[y*320+x]==0x202020 for y in range(10)for x in range(10))
        assert sum(v!=0 for v in canvas)==100
        lib.fixture_triangle((C.c_float*9)(-10000,-10000,1,20000,-10000,1,0,20000,1),0x808080,0)
        assert all(v==0x808080 for v in canvas)
        import _ctypes;_ctypes.FreeLibrary(lib._handle)
    print(f'Sky: PASS {len(rows)} exact owned-Java colors; original780star quads, CRC/payload, additive triangle seam and offscreen clipping')
if __name__=='__main__':run()
