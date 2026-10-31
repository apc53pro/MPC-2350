"""WorldProvider/World clock values compared with methods from the owned 1.0 source."""
import ctypes, math, os, random, struct, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('APPDATA',''))/'.minecraft/versions/1.0/minecraft/src/net/minecraft/src'
def method(text,name):
    start=text.index('public ',text.index(name)-20); brace=text.index('{',start); depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
def bits32(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def run():
    rng=random.Random(29)
    times=[0,1,5999,6000,11999,12000,17999,18000,23999,24000,-1,-24000,-24001,(1<<63)-1,-(1<<63)]
    times += [rng.randrange(-(1<<63),1<<63) for _ in range(160)]
    rows=[(t,p,r,h) for t in times for p in (0.,.5,1.) for r,h in ((0.,0.),(1.,0.),(.4,.7),(1.,1.))]
    with tempfile.TemporaryDirectory(prefix='mc10-clock-') as folder:
        folder=Path(folder);dll=folder/'clock.dll'
        subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing','-ffp-contract=off',str(ROOT/'tests/world_clock_fixture.c'),'-o',str(dll)],check=True,capture_output=True)
        lib=ctypes.CDLL(str(dll));lib.clock_set.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.c_float,ctypes.c_float]
        lib.mc_world_celestial_angle.argtypes=[ctypes.c_float];lib.mc_world_celestial_angle.restype=ctypes.c_float
        lib.mc_world_star_brightness.argtypes=[ctypes.c_float];lib.mc_world_star_brightness.restype=ctypes.c_float
        lib.mc_world_skylight_subtracted.argtypes=[ctypes.c_float]
        lib.clock_cos_low.argtypes=[ctypes.c_uint32,ctypes.c_uint32];lib.clock_cos_low.restype=ctypes.c_uint32
        lib.clock_cos_high.restype=ctypes.c_uint32
        lib.clock_low.restype=lib.clock_high.restype=ctypes.c_uint32
        table=(ctypes.c_uint32*65536).in_dll(lib,'mc_math_sine')
        table_data=(ROOT/'MC10/math10.dat').read_bytes()[16:];ctypes.memmove(table,table_data,len(table_data))
        provider=(SOURCE/'WorldProvider.java').read_text();world=(SOURCE/'World.java').read_text()
        body='\n'.join(method(provider,n) for n in ('calculateCelestialAngle','func_40470_b'))
        body+='\n'+'\n'.join(method(world,n) for n in ('calculateSkylightSubtracted','getStarBrightness'))
        java='''import java.io.*;import java.util.*;
class MathHelper {static float[] sine=new float[65536]; static float cos(float v){return sine[(int)(v*10430.378F+16384.0F)&65535];}}
public class ClockOracle {
long time;float rain,thunder;float getCelestialAngle(float p){return calculateCelestialAngle(time,p);}
float getRainStrength(float p){return rain;}float getWeightedThunderStrength(float p){return rain*thunder;}
BODY
public static void main(String[] args)throws Exception {
DataInputStream in=new DataInputStream(new FileInputStream(args[0]));in.skipBytes(16);
for(int i=0;i<65536;i++)MathHelper.sine[i]=Float.intBitsToFloat(Integer.reverseBytes(in.readInt()));
ClockOracle o=new ClockOracle();Scanner s=new Scanner(new File(args[1]));s.useLocale(Locale.ROOT);
while(s.hasNextLong()){o.time=s.nextLong();float p=s.nextFloat();o.rain=s.nextFloat();o.thunder=s.nextFloat();
System.out.println(Integer.toUnsignedString(Float.floatToRawIntBits(o.getCelestialAngle(p)))+","+o.func_40470_b(o.time,p)+","+o.calculateSkylightSubtracted(p)+","+Integer.toUnsignedString(Float.floatToRawIntBits(o.getStarBrightness(p))));}
}}
'''.replace('BODY',body)
        (folder/'ClockOracle.java').write_text(java)
        (folder/'input.txt').write_text(''.join(f'{t} {p} {r} {h}\n' for t,p,r,h in rows))
        env=os.environ.copy();env['JAVA_TOOL_OPTIONS']='-Xms8m -Xmx64m -XX:ReservedCodeCacheSize=16m -XX:CompressedClassSpaceSize=32m -Xss256k'
        subprocess.run(['javac',str(folder/'ClockOracle.java')],check=True,capture_output=True,env=env)
        lines=subprocess.run(['java','-cp',str(folder),'ClockOracle',str(ROOT/'MC10/math10.dat'),str(folder/'input.txt')],check=True,capture_output=True,text=True,env=env).stdout.splitlines()
        for row,line in zip(rows,lines):
            t,p,r,h=row;lib.clock_set(t&0xffffffff,(t>>32)&0xffffffff,r,h)
            got=[bits32(lib.mc_world_celestial_angle(p)),lib.mc_world_moon_phase(),lib.mc_world_skylight_subtracted(p),bits32(lib.mc_world_star_brightness(p))]
            assert got==list(map(int,line.split(','))),(row,got,line)
            assert ctypes.c_int.in_dll(lib,'mc_fp_error').value==0
        assert len(lines)==len(rows)
        for t in (0,0xffffffff,0xffffffffffffffff,0x7fffffffffffffff,0x8000000000000000):
            lib.clock_set(t&0xffffffff,t>>32,0,0);lib.mc_world_clock_tick()
            assert (lib.clock_low()|(lib.clock_high()<<32))==(t+1)&0xffffffffffffffff
        # fdlibm is the Java StrictMath contract; Python cos may differ by 1ulp.
        values=[0.,math.pi/4,math.pi/2,math.pi,math.nextafter(math.pi/2,0),math.nextafter(math.pi/2,math.inf)]
        values += [rng.uniform(-math.pi,math.pi) for _ in range(1000)]
        for value in values:
            raw=struct.unpack('<Q',struct.pack('<d',value))[0]
            lib.clock_set(0,0,0,0);lo=lib.clock_cos_low(raw&0xffffffff,raw>>32);hi=lib.clock_cos_high()
            got=struct.unpack('<d',struct.pack('<Q',lo|(hi<<32)))[0]
            assert abs(got-math.cos(value))<=math.ulp(math.cos(value)),(value,got,math.cos(value))
        import _ctypes;_ctypes.FreeLibrary(lib._handle)
    print(f'World clock: PASS {len(rows)} owned-Java celestial/moon/skylight/star vectors; tick carry and finite cosine boundaries')
if __name__=='__main__':run()
