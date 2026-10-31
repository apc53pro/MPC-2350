"""Execute owned 1.0 block/render methods without distributing Mojang source.

Generated Java remains in a temporary work directory; only numeric results are
exported. Gameplay callbacks are deliberately not part of this static oracle.
"""
from pathlib import Path
import argparse
import re
import subprocess
import json

METHODS = {
    'getBlockTexture','getBlockTextureFromSide','getBlockTextureFromSideAndMetadata',
    'setBlockBounds','setBlockBoundsBasedOnState','setBlockBoundsForItemRender',
    'isOpaqueCube','renderAsNormalBlock','getRenderType','shouldSideBeRendered',
    'getIsBlockSolid','getCollisionBoundingBoxFromPool','getCollidingBoundingBoxes',
    'isCollidable','canCollideCheck','getDirectionFromMetadata','isBlockFootOfBed',
    'getOrientation','isExtended','func_31050_c','isFenceAt','canThisPaneConnectToThisBlockID',
    'setBounds','setGraphicsLevel','setBlockBoundsForBlockRender','canBePlacedOn',
    'isFenceGateOpen','func_35290_f','func_40212_d','func_35296_f',
    'getRenderColor','getBlockColor','colorMultiplier','func_31040_i',
    'getState','setDoorRotation','func_35298_d','isTrapdoorOpen',
    'func_35299_s','isPowerProviderOrWire','canProvidePower','canBlockCatchFire','initializeBlock','setBurnRate',
    'getMixedBrightnessForBlock','getBlockBrightness','getAmbientOcclusionLightValue',
    'getIsPowered','func_293_a','getFlowVector','getEffectiveFlowDecay','getFluidHeightPercent',
}

def methods(text):
    for match in re.finditer(r'(?:public|protected|private)\s+(?:static\s+)?[^;\r\n{}]+?\([^;\r\n]*\)\s*\{',text):
        start=match.start(); end=text.index('{',start)+1; depth=1
        while depth:
            depth += (text[end]=='{')-(text[end]=='}');end+=1
        signature=text[start:text.index('{',start)]
        name=re.search(r'(\w+)\s*\(',signature).group(1)
        yield name,text[start:end]

def compile_oracle(source:Path,dest:Path,render=False,extra_java=None):
    dest.mkdir(parents=True,exist_ok=True)
    block=(source/'Block.java').read_text()
    registration='\n'.join(line for line in block.splitlines() if re.match(r'\s*public static final (?:Block\w*) \w+ =',line))
    classes=set(re.findall(r'new (Block\w+)\(',registration))
    pending=list(classes)
    while pending:
        cls=pending.pop();txt=(source/(cls+'.java')).read_text()
        base=re.search(r'class \w+ extends (\w+)',txt).group(1)
        if base!='Block' and base not in classes:classes.add(base);pending.append(base)
    static_fields='''
static Block[] blocksList=new Block[256];static boolean[] opaqueCubeLookup=new boolean[256];
static int[] lightOpacity=new int[256],lightValue=new int[256];
static boolean[] isBlockContainer=new boolean[256],canBlockGrass=new boolean[256];
static Object soundPowderFootstep=new Object(),soundWoodFootstep=new Object(),soundGravelFootstep=new Object(),soundGrassFootstep=new Object(),soundStoneFootstep=new Object(),soundMetalFootstep=new Object(),soundGlassFootstep=new Object(),soundClothFootstep=new Object(),soundSandFootstep=new Object();
int blockIndexInTexture,blockID;Material blockMaterial;double minX,minY,minZ,maxX,maxY,maxZ;float slipperiness=.6f,blockHardness,blockResistance;Object stepSound;
Block(int id,Material m){blockID=id;blockMaterial=m;blocksList[id]=this;setBlockBounds(0,0,0,1,1,1);opaqueCubeLookup[id]=isOpaqueCube();lightOpacity[id]=isOpaqueCube()?255:0;}
Block(int id,int tex,Material m){this(id,m);blockIndexInTexture=tex;}
Block setHardness(float f){return this;}Block setResistance(float f){return this;}Block setStepSound(Object o){return this;}Block setBlockName(String s){return this;}Block disableStats(){return this;}Block setRequiresSelfNotify(){return this;}Block setTickOnLoad(boolean b){return this;}Block setBlockUnbreakable(){return this;}
Block setLightOpacity(int n){lightOpacity[blockID]=n;return this;}Block setLightValue(float f){lightValue[blockID]=(int)(15f*f);return this;}
'''
    base_methods='\n'.join(body for name,body in methods(block) if name in METHODS)
    # Material methods are original, preserving subclass semantics and constants.
    for cls in ('Material','MaterialLogic','MaterialLiquid','MaterialTransparent','MaterialPortal','MaterialWeb','MapColor','Direction','Facing'):
        txt=(source/(cls+'.java')).read_text();txt=re.sub(r'^package .*?;\s*','',txt)
        (dest/(cls+'.java')).write_text(txt)
    (dest/'Block.java').write_text('import java.util.*;\nclass Block {\n'+static_fields+registration+'\n'+base_methods+'\nstatic{for(Block b:blocksList)if(b!=null)b.initializeBlock();}\n}')
    for cls in sorted(classes):
        txt=(source/(cls+'.java')).read_text()
        base=re.search(r'class \w+ extends (\w+)',txt).group(1)
        first=min(m.start() for m in re.finditer(r'(?:public|protected|private)\s+[^;\r\n{}]+?\([^;\r\n]*\)\s*\{',txt))
        header=txt[txt.index('{')+1:first]
        selected=[]
        for name,body in methods(txt):
            if name==cls or name in METHODS:
                if name in ('getCollisionBoundingBoxFromPool','setBlockBoundsBasedOnState') and cls=='BlockPistonMoving':continue
                selected.append(body)
        # Fields describing tile callback classes are harmless type tokens here.
        (dest/(cls+'.java')).write_text('import java.util.*;\nclass '+cls+' extends '+base+'{\n'+header+'\n'+'\n'.join(selected)+'\n}')
    support=SUPPORT
    for cls in ('MathHelper','Vec3D'):
        local=re.sub(r'^package .*?;\s*','',(source/(cls+'.java')).read_text())
        (dest/(cls+'.java')).write_text(local)
    if render:
        txt=(source/'RenderBlocks.java').read_text()
        header=txt[txt.index('{')+1:txt.index('public RenderBlocks(')]+'\nprivate int predicateDepth;\n'
        selected=[]
        face_names={'renderBottomFace':0,'renderTopFace':1,'renderEastFace':2,'renderWestFace':3,'renderNorthFace':4,'renderSouthFace':5}
        for name,body in methods(txt):
            if name in {'renderBlockOnInventory','renderItemIn3d','renderBlockFallingSand','renderCorrectedLightOnBlock','renderLightOnBlock'}:continue
            if name in face_names:
                b=body.index('{')+1
                body=body[:b]+'Tessellator.instance.beginFace('+str(face_names[name])+',var1,predicateDepth>0);'+body[b:]
                body=body[:-1]+'Tessellator.instance.endFace();}'
            if name in {'renderStandardBlockWithColorMultiplier','renderStandardBlockWithAmbientOcclusion','renderBlockBed','renderBlockCactusImpl'}:
                b=body.index('{')+1;body=body[:b]+'predicateDepth++;'+body[b:]
                body=body.replace('return ','predicateDepth--;return ')
            selected.append(body)
        (dest/'RenderBlocks.java').write_text('import java.util.*;\nclass RenderBlocks{'+header+'\n'+'\n'.join(selected)+'\n}')
        support+=RENDER_SUPPORT
    (dest/'Support.java').write_text(support)
    if extra_java:
        for name,body in extra_java.items():(dest/name).write_text(body)
    names={'Block','Support','MathHelper','Vec3D','Material','MaterialLogic','MaterialLiquid','MaterialTransparent','MaterialPortal','MaterialWeb','MapColor','Direction','Facing'}|classes
    files=[str(dest/(name+'.java'))for name in sorted(names)]
    if render:files.append(str(dest/'RenderBlocks.java'))
    if extra_java:files.extend(str(dest/name)for name in extra_java)
    result=subprocess.run(['javac','-J-Xmx96m','-encoding','UTF-8',*files],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stderr)
    return dest

SUPPORT='''
import java.util.*;
interface IBlockAccess {
 int getBlockId(int x,int y,int z);int getBlockMetadata(int x,int y,int z);
 boolean isBlockOpaqueCube(int x,int y,int z);boolean isBlockNormalCube(int x,int y,int z);
 Material getBlockMaterial(int x,int y,int z);WorldChunkManager getWorldChunkManager();
 int getLightBrightnessForSkyBlocks(int x,int y,int z,int min);float getBrightness(int x,int y,int z,int min);float getLightBrightness(int x,int y,int z);boolean isAirBlock(int x,int y,int z);int func_35452_b();
}
class World implements IBlockAccess {
 public int id,metadata; public int[] neighbors=new int[27];
 public int getBlockId(int x,int y,int z){if(x==0&&y==0&&z==0)return id;if(Math.abs(x)>1||Math.abs(y)>1||Math.abs(z)>1)return 0;return neighbors[(x+1)*9+(y+1)*3+z+1];}
 public int getBlockMetadata(int x,int y,int z){return x==0&&y==0&&z==0?metadata:0;}
 public boolean isBlockOpaqueCube(int x,int y,int z){Block b=Block.blocksList[getBlockId(x,y,z)];return b!=null&&b.isOpaqueCube();}
 public boolean isBlockNormalCube(int x,int y,int z){Block b=Block.blocksList[getBlockId(x,y,z)];return b!=null&&b.renderAsNormalBlock()&&b.blockMaterial.getIsOpaque();}
 public Material getBlockMaterial(int x,int y,int z){Block b=Block.blocksList[getBlockId(x,y,z)];return b==null?Material.air:b.blockMaterial;}
 public WorldChunkManager getWorldChunkManager(){return new WorldChunkManager();}
 public int getLightBrightnessForSkyBlocks(int x,int y,int z,int min){return ((x+2)&7)|(((y+2)&7)<<3)|(((z+2)&7)<<6)|(min<<9);}
 public float getBrightness(int x,int y,int z,int min){return 1;}
 public float getLightBrightness(int x,int y,int z){return 1;}
 public boolean isAirBlock(int x,int y,int z){return getBlockId(x,y,z)==0;}
 public int func_35452_b(){return 128;}
}
class AxisAlignedBB {
 double minX,minY,minZ,maxX,maxY,maxZ;
 static AxisAlignedBB getBoundingBoxFromPool(double a,double b,double c,double d,double e,double f){AxisAlignedBB q=new AxisAlignedBB();q.minX=a;q.minY=b;q.minZ=c;q.maxX=d;q.maxY=e;q.maxZ=f;return q;}
 boolean intersectsWith(AxisAlignedBB b){return b.maxX>minX&&b.minX<maxX&&b.maxY>minY&&b.minY<maxY&&b.maxZ>minZ&&b.minZ<maxZ;}
}
class WorldChunkManager {BiomeGenBase getBiomeGenAt(int x,int z){return new BiomeGenBase();}}
class BiomeGenBase {int field_40256_A=0xffffff;int func_40254_a(IBlockAccess a,int x,int y,int z){return 0xffffff;}int func_40255_b(IBlockAccess a,int x,int y,int z){return 0xffffff;}}
class ColorizerGrass {static int getGrassColor(double a,double b){return 0xffffff;}}
class ColorizerFoliage {static int getFoliageColor(double a,double b){return 0xffffff;}static int getFoliageColorPine(){return 0x619961;}static int getFoliageColorBirch(){return 0x80a755;}static int getFoliageColorBasic(){return 0x48b518;}}
class TileEntitySign {}
enum EnumMobType {mobs,everything,players}
class Oracle {
 public static void main(String[] args){World w=new World();for(int id=1;id<256;id++){Block b=Block.blocksList[id];if(b==null)continue;w.id=id;System.out.println("R,"+id+","+b.blockIndexInTexture+","+b.getRenderType()+","+(b.isOpaqueCube()?1:0)+","+(b.renderAsNormalBlock()?1:0)+","+Block.lightOpacity[id]+","+Block.lightValue[id]);System.out.println("S,"+id+","+Float.floatToRawIntBits(b.slipperiness));for(int m=0;m<16;m++){if(id==34&&(m&7)>5)continue;w.metadata=m;b.setBlockBoundsBasedOnState(w,0,0,0);for(int side=0;side<6;side++)System.out.println("T,"+id+","+m+","+side+","+b.getBlockTexture(w,0,0,0,side));System.out.println("B,"+id+","+m+","+b.minX+","+b.minY+","+b.minZ+","+b.maxX+","+b.maxY+","+b.maxZ);}}
 }
}
'''

RENDER_SUPPORT='''
class EntityRenderer {static boolean anaglyphEnable=false;}
class Minecraft {static boolean isAmbientOcclusionEnabled(){return false;}}
class Tessellator {
 static Tessellator instance=new Tessellator();int brightness;float red=1,green=1,blue=1;double tx,ty,tz;int face;
 ArrayList<double[]> vertices=new ArrayList<>();
 void setBrightness(int b){brightness=b;}void setColorOpaque_F(float r,float g,float b){red=r;green=g;blue=b;}void setColorOpaque_I(int n){red=(float)((n>>16)&255)/255;green=(float)((n>>8)&255)/255;blue=(float)(n&255)/255;}
 void setTranslationF(float x,float y,float z){tx+=x;ty+=y;tz+=z;}
 void beginFace(int side,Block b,boolean conditional){boolean inset=side==0?b.minY>0:side==1?b.maxY<1:side==2?b.minZ>0:side==3?b.maxZ<1:side==4?b.minX>0:b.maxX<1;face=conditional?((side+1)<<13|(inset?65536:0)):0;}
 void endFace(){face=0;}
 void addVertexWithUV(double x,double y,double z,double u,double v){vertices.add(new double[]{x+tx,y+ty,z+tz,u*256,v*256,brightness,red,green,blue,face});}
}
'''

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--render',action='store_true');args=ap.parse_args()
    out=compile_oracle(args.source,args.output,args.render)
    r=subprocess.run(['java','-Xms8m','-Xmx96m','-XX:ReservedCodeCacheSize=16m','-XX:CompressedClassSpaceSize=32m','-Xss256k','-cp',str(out),'Oracle'],capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stderr)
    (out/'vectors.txt').write_text(r.stdout)
    print('Owned source block oracle:',len(r.stdout.splitlines()),'vectors')
if __name__=='__main__':main()

