"""Owned RenderBlocks/ModelBox UV/winding oracle and native pixel previews."""
from pathlib import Path
import argparse
import ctypes as C
import hashlib
import io
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
import zipfile
from test_viewmodel import HERE,ROOT,PROJECT,ORIGINAL,Face,Stack,Player,Settings,scalar,integer,method

TERRAIN=PROJECT.parents[2]/'tests/terrain';sys.path.insert(0,str(TERRAIN))
import test_textured_tiles as native


def asset(name):
    data=(PROJECT/'MC10/assets.mc10').read_bytes();header=struct.unpack_from('<8I',data);hash=2166136261
    for b in name.encode():hash=((hash^b)*16777619)&0xffffffff
    for i in range(header[2]):
        record=struct.unpack_from('<10I',data,header[4]+i*40)
        if record[0]==hash:return data[record[5]:record[5]+record[6]],data[record[7]:record[7]+record[8]],record[2],record[3]
    raise AssertionError(('missing owned asset',name))


def png(path,pixels):
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    rows=[]
    for y in range(240):
        row=bytearray([0])
        for color in pixels[y*320:(y+1)*320]:row.extend(((color>>5)*255//7,((color>>2)&7)*255//7,(color&3)*255//3))
        rows.append(bytes(row))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',320,240,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b''))


def block_oracle(tmp,bounds):
    names=('renderBottomFace','renderTopFace','renderEastFace','renderWestFace','renderNorthFace','renderSouthFace')
    source=(ORIGINAL/'RenderBlocks.java').read_text();bodies='\n'.join(method(source,name)for name in names)
    members=set(re.findall(r'this\.(\w+)',bodies));members-=set(names)
    fields=[]
    for name in sorted(members):
        kind='boolean'if name in ('enableAO','flipTexture')else 'float'if name.startswith('color')else 'int'
        fields.append(kind+' '+name+('=-1'if name=='overrideBlockTexture'else '')+';')
    code='''import java.util.*;
class Block{double minX,minY,minZ,maxX,maxY,maxZ;}
class Tessellator{static Tessellator instance=new Tessellator();ArrayList<double[]> points=new ArrayList<double[]>();void setColorOpaque_F(float r,float g,float b){}void setBrightness(int x){}void addVertexWithUV(double x,double y,double z,double u,double v){points.add(new double[]{x,y,z,u*256,v*256});}}
class BoxOracle{
'''+ '\n'.join(fields)+bodies+'''
public static void main(String[] args){Scanner in=new Scanner(System.in);while(in.hasNextDouble()){
Block b=new Block();b.minX=in.nextDouble();b.minY=in.nextDouble();b.minZ=in.nextDouble();b.maxX=in.nextDouble();b.maxY=in.nextDouble();b.maxZ=in.nextDouble();BoxOracle r=new BoxOracle();
r.renderBottomFace(b,-.5,-.5,-.5,0);r.renderTopFace(b,-.5,-.5,-.5,0);r.renderEastFace(b,-.5,-.5,-.5,0);r.renderWestFace(b,-.5,-.5,-.5,0);r.renderNorthFace(b,-.5,-.5,-.5,0);r.renderSouthFace(b,-.5,-.5,-.5,0);
for(double[] p:Tessellator.instance.points){for(double n:p)System.out.print(n+" ");}System.out.println();Tessellator.instance.points.clear();}}
}
'''
    src=tmp/'BoxOracle.java';src.write_text(code)
    result=subprocess.run(['javac',str(src)],capture_output=True,text=True);assert result.returncode==0,result.stderr
    result=subprocess.run(['java','-cp',str(tmp),'BoxOracle'],input='\n'.join(' '.join(str(x)for x in row)for row in bounds)+'\n',capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    return [[float(x)for x in row.split()]for row in result.stdout.splitlines()]


def run(renderer_path,previews):
    runtime=os.add_dll_directory(str(Path(shutil.which('g++')).parent))if os.name=='nt'else None
    with tempfile.TemporaryDirectory(prefix='mc10-viewmodel-geometry-',ignore_cleanup_errors=True)as temp:
        tmp=Path(temp);dll=tmp/'host.dll'
        result=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(HERE/'viewmodel_fixture.c'),'-I',str(PROJECT),'-I',str(ROOT/'src'),'-o',str(dll)],capture_output=True,text=True)
        assert result.returncode==0,result.stderr
        lib=C.CDLL(str(dll));lib.mc_vm_box.argtypes=[C.c_int]+[C.c_float]*6;lib.fixture_draw.argtypes=[C.c_float]
        path=(str(PROJECT/'MC10/assets.mc10')+'\0').encode();C.memmove((C.c_char*1024).in_dll(lib,'fixture_asset_path'),path,len(path))
        C.memmove((C.c_uint32*65536).in_dll(lib,'mc_math_sine'),(PROJECT/'MC10/math10.dat').read_bytes()[16:],262144)
        lib.fixture_init();faces=(Face*128).in_dll(lib,'fixture_faces')
        bounds=[(0,0,0,1,1,1),(0,.375,0,1,.625,1),(.375,0,0,.625,1,.25),(.4375,.8125,-.125,.5625,.9375,1.125),(0,0,0,1,1,.5),(0,0,.5,1,.5,1),(0,0,0,1,.5,1)]
        expected=block_oracle(tmp,bounds)
        scalar(lib,'mc_vm_hand_fov').value=70;scalar(lib,'mc_vm_hand_far').value=512
        for shape,(box,original)in enumerate(zip(bounds,expected)):
            integer(lib,'fixture_face_count').value=0;lib.mc_vm_identity();lib.mc_vm_box(1,*box)
            actual=[x for face in faces[:6]for x in face.vertices]
            assert len(original)==120 and max(abs(a-b)for a,b in zip(actual,original))<3e-6,('owned box geometry/UV',shape,next((i,a,b)for i,(a,b)in enumerate(zip(actual,original))if abs(a-b)>=3e-6))
        # ModelBox's actual six constructor records define the right-arm
        # vertex order and TexturedQuad's original UV corner assignment.
        source=(ORIGINAL/'ModelBox.java').read_text();records=re.findall(r'new TexturedQuad\(new PositionTextureVertex\[\]\{([^}]+)\}, ([^;]+)\);',source)
        assert len(records)==6
        point_names={'var23':0,'var15':1,'var16':2,'var17':3,'var18':4,'var19':5,'var20':6,'var21':7}
        points=[(-3,-2,-2),(1,-2,-2),(1,10,-2),(-3,10,-2),(-3,-2,2),(1,-2,2),(1,10,2),(-3,10,2)]
        integer(lib,'fixture_face_count').value=0;lib.mc_vm_arm()
        for side,(corners,arguments)in enumerate(records):
            values=[]
            for expr in arguments.split(',')[:4]:
                for key,value in {'var2':40,'var3':16,'var7':4,'var8':12,'var9':4}.items():expr=expr.replace(key,str(value))
                assert re.fullmatch(r'[0-9 +()-]+',expr);values.append(eval(expr,{'__builtins__':{}}))
            u0,v0,u1,v1=values;uv=[(u1,v0),(u0,v0),(u0,v1),(u1,v1)]
            expected=[]
            for name,(u,v)in zip(corners.split(','),uv):expected.extend((*points[point_names[name.strip()]],u,v))
            assert list(faces[side].vertices)==expected,('owned arm ModelBox',side)
            xyz=[expected[i:i+3]for i in range(0,20,5)];edge0=[xyz[1][a]-xyz[0][a]for a in range(3)];edge1=[xyz[2][a]-xyz[0][a]for a in range(3)]
            cross=(edge0[1]*edge1[2]-edge0[2]*edge1[1],edge0[2]*edge1[0]-edge0[0]*edge1[2],edge0[0]*edge1[1]-edge0[1]*edge1[0])
            normal=((1,0,0),(-1,0,0),(0,-1,0),(0,1,0),(0,0,-1),(0,0,1))[side]
            assert sum(a*b for a,b in zip(cross,normal))>0,('arm winding',side)
        terrain,terrain_mask,_,_=asset('terrain.png')
        for id,damage in ((2,0),(81,0),(35,1),(50,0)):
            assert lib.mc_vm_load_block(id,damage)==1
            pixels=bytes((C.c_uint32*(6*64)).in_dll(lib,'mc_vm_block_pixels'));mask=(C.c_uint32*(6*8)).in_dll(lib,'mc_vm_block_masks')
            for side in range(6):
                tile=lib.mc_vm_block_texture(id,damage,side)
                for row in range(16):
                    source=((tile>>4)*16+row)*256+(tile&15)*16
                    assert pixels[side*256+row*16:side*256+(row+1)*16]==terrain[source:source+16]
                    for column in range(16):
                        bit=source+column;word=struct.unpack_from('<I',terrain_mask,(bit>>5)*4)[0]
                        expected=(word>>(31-(bit&31)))&1;local=row*16+column
                        assert ((mask[side*8+(local>>5)]>>(31-(local&31)))&1)==expected
        # Actual production native renderer, not a sprite mock or an emulator.
        renderer=C.CDLL(str(renderer_path))
        signatures={'worker':([C.c_bool],None),'scratch':([C.c_bool],None),'diagnostics':([native.WORD],None),'pristine':([native.WORD],None),'nonpristine':([],None),'end':([],C.c_bool),'begin':([native.PTR,native.PTR,native.PTR,native.WORD,native.WORD],C.c_bool),'draw':([native.PTR,native.PTR,native.PTR,native.WORD,C.POINTER(native.Vertex),native.WORD,C.POINTER(native.Transform),native.PTR,C.c_int32,C.c_int32,native.WORD,C.c_bool,native.PTR],C.c_bool)}
        for name,(args,result)in signatures.items():fn=getattr(renderer,'probe_'+name);fn.argtypes=args;fn.restype=result
        item_pixels,item_mask,_,_=asset('gui/items.png')
        def item_icon(tile):
            target=(C.c_uint32*64).in_dll(lib,'fixture_item_pixels');out=(C.c_ubyte*256).from_address(C.addressof(target));mask=(C.c_uint32*8).in_dll(lib,'fixture_item_mask')
            for i in range(8):mask[i]=0
            for row in range(16):
                for column in range(16):
                    source=((tile>>4)*16+row)*256+(tile&15)*16+column;index=row*16+column;out[index]=item_pixels[source]
                    word=struct.unpack_from('<I',item_mask,(source>>5)*4)[0]
                    if word&(1<<(31-(source&31))):mask[index>>5]|=1<<(31-(index&31))
        def actual_frame():
            draws=[];textures=[]
            for index in range(integer(lib,'fixture_face_count').value):
                face=faces[index];vertices=(native.Vertex*4)()
                for c in range(4):
                    for a,name in enumerate(('x','y','z','u','v')):setattr(vertices[c],name,face.vertices[c*5+a])
                matrix=native.Transform();matrix.value[:]=face.transform
                w,h=face.width,face.height;textures.append((w,h,native.Guarded(bytes(face.pixels)[:w*h]),[None,native.Guarded(bytes(face.mask)[:(w*h+31)//32*4])]))
                draws.append(native.make_draw(vertices,asset=index,flags=face.flags,mask=1,matrix=matrix))
            background=bytes(0x4f if y<145 else 0x49 for y in range(240)for x in range(320))
            initial=(background,bytes(native.PIXELS*4),bytes(native.PIXELS))
            out=native.render(renderer,initial,7,[draws],textures,2,live_worker=True,pristine=True)
            changed=sum(a!=b for a,b in zip(background,out[0]));assert changed>100,('missing viewmodel pixels',changed)
            return out[0],changed
        previews.mkdir(parents=True,exist_ok=True)
        skin,skin_mask,sw,sh=asset('mob/char.png');assert (sw,sh)==(64,32)
        sys.path.insert(0,str(PROJECT/'tools'));import mc10_asset_pack
        from PIL import Image
        manifest=json.loads((PROJECT/'MC10/assets_manifest.json').read_text())
        with zipfile.ZipFile(manifest['source_jar'])as jar:
            original_skin=Image.open(io.BytesIO(jar.read('mob/char.png'))).convert('RGBA')
            converted_skin,converted_mask=mc10_asset_pack.pack_image(original_skin)
        assert converted_skin==skin and converted_mask==skin_mask,'owned JAR skin/asset-pack mismatch'
        print('PASS owned JAR mob/char.png -> converter -> runtime skin byte-for-byte, including alpha mask',flush=True)
        skin_preview=bytearray([0x49])*native.PIXELS
        for y in range(32):
            for x in range(64):
                for dy in range(4):
                    for dx in range(4):skin_preview[(40+y*4+dy)*320+32+x*4+dx]=skin[y*64+x]
        png(previews/'owned_skin_atlas.png',bytes(skin_preview))
        cases=(('arm_idle',0,0,0,0),('arm_swing',0,0,3/6,2/6),('stick',280,53,0,0),('diamond_pickaxe',278,99,0,0),('stone_block',1,0,0,0),('grass_block',2,0,0,0),('wooden_stairs',53,0,0,0),('wooden_fence',85,0,0,0))
        for name,id,tile,swing,previous in cases:
            lib.fixture_init();item_icon(tile);player=Player.in_dll(lib,'mc_player');player.yaw=player.pitch=0
            stack=Stack.in_dll(lib,'mc_vm_stack');stack.id=id;stack.count=int(id!=0)
            scalar(lib,'mc_vm_equipped').value=scalar(lib,'mc_vm_previous_equipped').value=1
            scalar(lib,'mc_vm_swing').value=swing;scalar(lib,'mc_vm_previous_swing').value=previous
            lib.fixture_draw(.5);assert integer(lib,'mc_viewmodel_status').value==0,(name,integer(lib,'mc_viewmodel_status').value)
            pixels,changed=actual_frame();output=previews/(name+'.png');png(output,pixels)
            print('Native preview',name,'pixels',changed,'SHA256',hashlib.sha256(pixels).hexdigest(),flush=True)
        import _ctypes;_ctypes.FreeLibrary(renderer._handle);_ctypes.FreeLibrary(lib._handle)
    if runtime:runtime.close()
    print('PASS owned-source geometry: 42 RenderBlocks faces/UV, six ModelBox arm faces/winding, all6 raw tile/mask crops for4variants; eight actual-native preview frames')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer-dll',type=Path,required=True)
    parser.add_argument('--previews',type=Path,default=ROOT/'previews')
    args=parser.parse_args();run(args.renderer_dll.resolve(),args.previews.resolve())
