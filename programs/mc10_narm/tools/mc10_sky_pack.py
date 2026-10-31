"""Convert owned sky textures and execute the owned RenderGlobal star builder.

No runtime PNG, trigonometric star construction, or replacement artwork.
"""
import argparse, hashlib, io, json, os, struct, subprocess, tempfile, zlib
from pathlib import Path
from zipfile import ZipFile
from PIL import Image
def extract(text,name):
    start=text.index('private void '+name);brace=text.index('{',start);end=brace+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
def run(source,jar,output):
    original=source.read_bytes();body=extract(original.decode(),'renderStars()')
    with tempfile.TemporaryDirectory(prefix='mc10-sky-') as tmp:
        tmp=Path(tmp)
        code='''import java.io.*;import java.util.Random;
class Tessellator {static Tessellator instance=new Tessellator();static DataOutputStream out;static int count;
void startDrawingQuads(){}void draw(){}void addVertex(double x,double y,double z)throws RuntimeException {
try{out.writeInt(Integer.reverseBytes(Float.floatToRawIntBits((float)x)));out.writeInt(Integer.reverseBytes(Float.floatToRawIntBits((float)y)));out.writeInt(Integer.reverseBytes(Float.floatToRawIntBits((float)z)));count++;}catch(IOException e){throw new RuntimeException(e);}}}
public class SkyExport { BODY
public static void main(String[] args)throws Exception {Tessellator.out=new DataOutputStream(new FileOutputStream(args[0]));new SkyExport().renderStars();Tessellator.out.close();System.out.println(Tessellator.count);}}
'''.replace('BODY',body)
        (tmp/'SkyExport.java').write_text(code)
        env=os.environ.copy();env['JAVA_TOOL_OPTIONS']='-Xms8m -Xmx64m -XX:ReservedCodeCacheSize=16m -XX:CompressedClassSpaceSize=32m -Xss256k'
        subprocess.run(['javac',str(tmp/'SkyExport.java')],check=True,capture_output=True,env=env)
        subprocess.run(['java','-cp',str(tmp),'SkyExport',str(tmp/'stars.bin')],check=True,capture_output=True,env=env)
        stars=(tmp/'stars.bin').read_bytes();assert len(stars)%48==0 and len(stars)//48<=1500
    manifest={'format':'MCSK v1','source_sha256':hashlib.sha256(original).hexdigest(),'star_method_sha256':hashlib.sha256(body.encode()).hexdigest(),'star_quads':len(stars)//48,'adaptations':['Original RGB888 sky textures retained; final composited frame quantized to RGB332.','Original deterministic binary64 star vertices narrowed to binary32 after host execution.'],'resources':{}}
    textures=bytearray()
    with ZipFile(jar) as archive:
        for name,size in [('terrain/sun.png',(32,32)),('terrain/moon_phases.png',(128,64))]:
            raw=archive.read(name);image=Image.open(io.BytesIO(raw)).convert('RGBA');assert image.size==size
            for r,g,b,a in image.getdata():textures+=struct.pack('<I',r|(g<<8)|(b<<16)|(a<<24))
            manifest['resources'][name]=hashlib.sha256(raw).hexdigest()
    payload=bytes(textures)+stars
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_bytes(struct.pack('<8I',0x4b53434d,1,len(stars)//48,32,len(textures),len(payload),zlib.crc32(payload),0)+payload)
    output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Owned sky conversion: PASS {len(stars)//48} exact source star quads, {len(textures)} texture bytes, {len(payload)} payload bytes')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',required=True,type=Path);p.add_argument('--jar',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();run(a.source,a.jar,a.output)
