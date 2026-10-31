"""Convert the user's owned RenderBlocks outputs to an indexed static mesh pack.

No Minecraft source is shipped: source methods execute in a local temporary
Java oracle; the output is geometry and texture coordinates derived from it.
See BLOCK_MESH_FORMAT.md for the bounded runtime decoding contract.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
from PIL import Image
from source_block_oracle import compile_oracle

JAVA=r'''
import java.io.*;import java.util.*;
class TemplateWorld extends World {
 int ox,oy=64,oz,variant,kind;
 @Override public int getBlockId(int x,int y,int z){int dx=x-ox,dy=y-oy,dz=z-oz;if(dx==0&&dy==0&&dz==0)return id;
  int d=dx==-1&&dz==0?0:dx==1&&dz==0?1:dx==0&&dz==-1?2:dx==0&&dz==1?3:-1;
  if(kind==18){if(dy==0&&d>=0)return (variant&(1<<d))!=0?id:0;if(dx==0&&dz==0&&Math.abs(dy)==1)return (variant&(1<<(dy==1?4:5)))!=0?0:1;if(d>=0&&Math.abs(dy)==1)return (variant&(1<<(6+d+(dy==1?0:4))))!=0?0:1;}
  if(kind==11&&dy==0&&d>=0)return (variant&(1<<d))!=0?id:0;
  if(kind==20&&dx==0&&dz==0&&dy==1)return variant!=0?1:0;
  if(kind==19&&dy==0&&d==variant-1)return id==104?86:103;
  if(kind==3){if(dx==0&&dz==0&&dy==-1)return (variant&1)!=0?1:0;if(dy==0&&d>=0)return(variant&(1<<(d+1)))!=0?5:0;if(dx==0&&dz==0&&dy==1)return(variant&32)!=0?5:0;}
  if(kind==5&&d>=0){if(dy==0)return(variant&(1<<(d+4)))!=0?1:(variant&(1<<d))!=0?55:0;if(dy==1)return(variant&(1<<(d+4)))!=0?55:0;}
  return 0;
 }
 @Override public int getBlockMetadata(int x,int y,int z){return x==ox&&y==oy&&z==oz?metadata:0;}
 @Override public int getLightBrightnessForSkyBlocks(int x,int y,int z,int min){return ((x-ox+2)&7)|(((y-oy+2)&7)<<3)|(((z-oz+2)&7)<<6)|(min<<9);}
}
class MeshExporter {
 static DataOutputStream out;static int count;
 static void i32(int n)throws Exception{out.writeByte(n);out.writeByte(n>>>8);out.writeByte(n>>>16);out.writeByte(n>>>24);}
 static void f32(double f)throws Exception{i32(Float.floatToRawIntBits((float)f));}
 static int lilyOrientation(int x,int y,int z){long h=(long)(x*3129871)^(long)z*116129781L^(long)y;h=h*h*42317861L+h*11L;return(int)(h>>>16)&3;}
 static void emit(int id,int meta,int variant)throws Exception{
  Block b=Block.blocksList[id];TemplateWorld w=new TemplateWorld();w.id=id;w.metadata=meta;w.variant=variant;w.kind=b.getRenderType();
  if(w.kind==3)w.ox=((variant>>>6)&3);
  if(w.kind==23){boolean found=false;for(int x=0;x<16&&!found;x++)for(int z=0;z<16&&!found;z++)if(lilyOrientation(x,64,z)==variant){w.ox=x;w.oz=z;found=true;}if(!found)throw new Error("lily rotation");}
  Tessellator t=Tessellator.instance;t.vertices.clear();t.tx=t.ty=t.tz=0;t.face=0;t.red=t.green=t.blue=1;t.brightness=w.getLightBrightnessForSkyBlocks(w.ox,w.oy,w.oz,Block.lightValue[id]);
  RenderBlocks r=new RenderBlocks(w);r.renderBlockAllFaces(b,w.ox,w.oy,w.oz);
  if(t.vertices.size()%4!=0)throw new Error("nonquad");
  int key=id<<24|meta<<20|variant;i32(key);i32(t.vertices.size()/4);
  for(int j=0;j<t.vertices.size();j+=4){double[]a=t.vertices.get(j);for(int k=0;k<4;k++){double[]v=t.vertices.get(j+k);if(v[5]!=a[5]||v[6]!=a[6]||v[7]!=a[7]||v[8]!=a[8]||v[9]!=a[9])throw new Error("pervertex color/light unsupported");f32(v[0]-w.ox);f32(v[1]-w.oy);f32(v[2]-w.oz);f32(v[3]);f32(v[4]);}f32(a[6]);f32(a[7]);f32(a[8]);i32((int)a[5]|(int)a[9]);}
  count++;
 }
 public static void main(String[]args)throws Exception{
  out=new DataOutputStream(new BufferedOutputStream(new FileOutputStream(args[0])));
  int[]ids={26,27,28,29,33,34,51,55,66,69,75,76,81,85,93,94,101,102,104,105,106,107,111,113,117,118,120,122};
  for(int id:ids){int type=Block.blocksList[id].getRenderType();int metas=(type==3||type==11||type==18||type==23||type==27)?1:16;int variants=type==3?256:type==5?256:type==11?16:type==18?16384:type==19?5:type==20?2:type==23?4:1;
   for(int m=0;m<metas;m++){if((type==16||type==17)&&(m&7)>5)continue;for(int v=0;v<variants;v++){if(type==5&&((v&15)|(v>>>4))!=(v&15))continue;emit(id,m,v);}}
  }
  out.close();System.out.println("Source RenderBlocks exported "+count+" keys");
 }
}
'''

def convert(source:Path,temp:Path,output:Path):
    compile_oracle(source,temp,True,{'MeshExporter.java':JAVA})
    raw=temp/'meshes.raw'
    run=subprocess.run(['java','-Xms8m','-Xmx128m','-XX:ReservedCodeCacheSize=16m','-XX:CompressedClassSpaceSize=32m','-Xss256k','-cp',str(temp),'MeshExporter',str(raw)],capture_output=True,text=True)
    if run.returncode:raise RuntimeError(run.stderr)
    print(run.stdout.strip())
    index=[];unique={};payload=bytearray();total=0;tints={}
    f32=lambda n:struct.unpack('<f',struct.pack('<f',n))[0]
    shades={f32(.5):0,f32(1):1,f32(.8):2,f32(.6):4}
    with raw.open('rb')as f:
        while header:=f.read(8):
            key,count=struct.unpack('<II',header);record=f.read(count*96)
            if len(record)!=count*96:raise ValueError('truncated oracle mesh')
            record=bytearray(record)
            for q in range(count):
                v=list(struct.unpack_from('<23fI',record,q*96));rgb=tuple(v[20:23]);descriptor=v[23]
                if key>>24==34 and ((descriptor>>13)&7)==0:descriptor|=131072
                if rgb[0]==rgb[1]==rgb[2] and rgb[0] in shades:
                    descriptor|=shades[rgb[0]]<<24
                else:
                    umin=min(v[i+3]for i in range(0,20,5));vmin=min(v[i+4]for i in range(0,20,5))
                    tile=int((umin+.0001)//16)+16*int((vmin+.0001)//16)
                    tintkey=(tile,rgb)
                    if tintkey not in tints:tints[tintkey]=len(tints)+1
                    if len(tints)>63:raise ValueError('source tint bank exceeds style field')
                    descriptor|=tints[tintkey]<<18|1<<24
                    for i in range(0,20,5):v[i+3]-=(tile&15)*16;v[i+4]-=(tile>>4)*16
                v[23]=descriptor;struct.pack_into('<23fI',record,q*96,*v)
            digest=hashlib.sha256(record).digest()
            if digest not in unique:unique[digest]=len(payload);payload.extend(record)
            index.append((key,unique[digest],count,0));total+=count
    index.sort();payload_offset=32+len(index)*12
    header=struct.pack('<8s6I',b'MCBM10\0\0',1,len(index),32,payload_offset,96,0)
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('wb')as f:
        f.write(header)
        for key,offset,count,flags in index:f.write(struct.pack('<IIHH',key,payload_offset+offset,count,flags))
        f.write(payload)
    audit={'version':1,'source_renderblocks_sha256':hashlib.sha256((source/'RenderBlocks.java').read_bytes()).hexdigest(),'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'bytes':output.stat().st_size,'keys':len(index),'unique_meshes':len(unique),'unshared_quad_count':total,'record_bytes':96,'ids':sorted({key>>24 for key,_,_,_ in index}),'excluded_types':[0,1,4,6,7,8,10,22,-1]}
    output.with_suffix('.json').write_text(json.dumps(audit,indent=2)+'\n')
    terrain=Image.open(source.parents[2]/'terrain.png').convert('RGBA')
    tintpixels=bytearray();tintmasks=bytearray();tintaudit=[]
    for (tile,rgb),slot in tints.items():
        opaque=[];base=[]
        for y in range(16):
            for x in range(16):
                r,g,b,a=terrain.getpixel(((tile&15)*16+x,(tile>>4)*16+y))
                r=max(0,min(255,int(f32(r*rgb[0]))));g=max(0,min(255,int(f32(g*rgb[1]))));b=max(0,min(255,int(f32(b*rgb[2]))))
                base.append((r&224)|((g>>5)<<2)|(b>>6));opaque.append(a>=128)
        for level in range(16):
            for color in base:
                r=((color>>5)*level+7)//15;g=(((color>>2)&7)*level+7)//15;b=((color&3)*level+7)//15
                tintpixels.append((r<<5)|(g<<2)|b)
        mask=[0]*8
        for i,a in enumerate(opaque):
            if a:mask[i>>5]|=1<<(31-(i&31))
        tintmasks.extend(struct.pack('<8I',*mask));tintaudit.append({'slot':slot,'tile':tile,'rgb':rgb})
    tintpath=output.with_name('blocktint10.dat');maskoffset=32+len(tintpixels)
    tintpath.write_bytes(struct.pack('<8s6I',b'MCBT10\0\0',1,len(tints),256,16,32,maskoffset)+tintpixels+tintmasks)
    tintpath.with_suffix('.json').write_text(json.dumps({'sha256':hashlib.sha256(tintpath.read_bytes()).hexdigest(),'bytes':tintpath.stat().st_size,'slots':tintaudit},indent=2)+'\n')
    audit['tint_slots']=len(tints);audit['tint_bytes']=tintpath.stat().st_size
    output.with_suffix('.json').write_text(json.dumps(audit,indent=2)+'\n')
    print(json.dumps(audit,indent=2))
    return audit

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--temp',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();convert(a.source,a.temp,a.output)
if __name__=='__main__':main()

