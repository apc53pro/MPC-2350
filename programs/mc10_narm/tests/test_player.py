"""Compare production locomotion to methods extracted from owned Java source.

The Java harness executes the original AABB, moveEntity, moveFlying and
moveEntityWithHeading bodies. Unported entity effects are excluded explicitly.
"""
import ctypes,hashlib,os,struct,subprocess,tempfile
from pathlib import Path
PROJECT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ['APPDATA'])/'.minecraft/versions/1.0/minecraft/src/net/minecraft/src'
def method(file,signature):
 text=(SOURCE/file).read_text();start=text.index(signature);brace=text.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
def java_source():
 move=method('Entity.java','public void moveEntity(double')
 cut=move.index('if(var15 != var5)',move.index('this.posX =',move.index('Profiler.endSection();')))
 finish=move.index('}',cut)+1
 move=move[:finish]+'\n}\n}\n'
 heading=method('EntityLiving.java','public void moveEntityWithHeading(float')
 heading=heading[:heading.index('this.field_705_Q')]+ '\n}\n'
 world=method('World.java','public List getCollidingBoundingBoxes(Entity')
 world=world[:world.index('double var14 = 0.25D;')]+'return this.collidingBoundingBoxes;\n}\n'
 sp=method('EntityPlayerSP.java','public void onLivingUpdate()')
 input_step=sp[sp.index('var1 = this.movementInput.jump;'):sp.index('super.onLivingUpdate();')]
 return '''package net.minecraft.src;
import java.util.*;import java.io.*;
class Profiler {static void startSection(String x){}static void endSection(){}}
class MovingObjectPosition {MovingObjectPosition(int x,int y,int z,int side,Vec3D v){}}
class Capabilities {boolean allowFlying,isFlying;}
class Food {int getFoodLevel(){return 20;}}
class Potion {static final Potion potionBlindness=new Potion(),potionJump=new Potion();}
class Effect {int getAmplifier(){return 0;}}
class MovementInput {
 float moveStrafe,moveForward,nextStrafe,nextForward;boolean jump,sneak,nextJump,nextSneak;
 void updatePlayerMoveState(Entity e){moveStrafe=nextStrafe;moveForward=nextForward;jump=nextJump;sneak=nextSneak;
  if(sneak){moveStrafe=(float)((double)moveStrafe*.3D);moveForward=(float)((double)moveForward*.3D);}}
}
class Material {
 static final Material air=new Material(false,false,false),rock=new Material(true,true,false),wood=new Material(true,true,false),plants=new Material(false,false,false),water=new Material(false,false,true),lava=new Material(false,false,true),ice=new Material(true,true,false),pumpkin=new Material(true,true,false),glass=new Material(true,false,false);
 boolean solid,opaque,liquid;Material(boolean s,boolean o,boolean l){solid=s;opaque=o;liquid=l;}
 boolean isSolid(){return solid;}boolean getIsSolid(){return solid;}boolean getIsOpaque(){return opaque;}boolean getIsLiquid(){return liquid;}
}
interface IBlockAccess {int getBlockId(int x,int y,int z);int getBlockMetadata(int x,int y,int z);Material getBlockMaterial(int x,int y,int z);}
class Block {
 static Block[] blocksList=new Block[256];static Block fenceGate,ladder;int blockID;Material blockMaterial;float slipperiness=.6F;double minX,minY,minZ,maxX=1,maxY=1,maxZ=1;
 Block(int id,Material material){blockID=id;blockMaterial=material;blocksList[id]=this;}
 boolean renderAsNormalBlock(){return blockMaterial==Material.rock||blockMaterial==Material.wood;}
 void setBlockBounds(float a,float b,float c,float d,float e,float f){minX=a;minY=b;minZ=c;maxX=d;maxY=e;maxZ=f;}
 '''+method('Block.java','public AxisAlignedBB getCollisionBoundingBoxFromPool(World')+method('Block.java','public void getCollidingBoundingBoxes(World')+'''
 boolean getIsBlockSolid(IBlockAccess w,int x,int y,int z,int side){return w.getBlockMaterial(x,y,z).isSolid();}
 void velocityToAddToEntity(World w,int x,int y,int z,Entity e,Vec3D v){}
}
class NullBlock extends Block {NullBlock(int id,Material m){super(id,m);}public AxisAlignedBB getCollisionBoundingBoxFromPool(World w,int x,int y,int z){return null;}}
class Stairs extends Block {Stairs(int id){super(id,Material.rock);}boolean renderAsNormalBlock(){return false;}
'''+method('BlockStairs.java','public void getCollidingBoundingBoxes(World')+'''
}
class Fence extends Block {Fence(int id){super(id,Material.wood);}boolean renderAsNormalBlock(){return false;}
'''+method('BlockFence.java','public AxisAlignedBB getCollisionBoundingBoxFromPool(World')+method('BlockFence.java','public boolean isFenceAt(IBlockAccess')+'''
}
class Pane extends Block {Pane(int id){super(id,Material.glass);}boolean func_35298_d(int id){return id==102||(id>0&&blocksList[id]!=null&&blocksList[id].renderAsNormalBlock());}
'''+method('BlockPane.java','public void getCollidingBoundingBoxes(World')+'''
}
class Ladder extends Block {Ladder(int id){super(id,Material.plants);}
'''+method('BlockLadder.java','public AxisAlignedBB getCollisionBoundingBoxFromPool(World')+'''
}
class Door extends Block {Door(int id){super(id,Material.wood);}boolean renderAsNormalBlock(){return false;}
'''+method('BlockDoor.java','public int getState(int')+method('BlockDoor.java','public void setDoorRotation(int')+'''
 public AxisAlignedBB getCollisionBoundingBoxFromPool(World w,int x,int y,int z){setDoorRotation(getState(w.getBlockMetadata(x,y,z)));return super.getCollisionBoundingBoxFromPool(w,x,y,z);}
}
class BlockFluid extends NullBlock {BlockFluid(int id,Material m){super(id,m);}
'''+method('BlockFluid.java','public static float getFluidHeightPercent(int')+method('BlockFluid.java','protected int getEffectiveFlowDecay(IBlockAccess')+method('BlockFluid.java','public boolean getIsBlockSolid(IBlockAccess')+method('BlockFluid.java','private Vec3D getFlowVector(IBlockAccess')+method('BlockFluid.java','public void velocityToAddToEntity(World')+'''
}
class World implements IBlockAccess {
 byte[] blocks,metadata;ArrayList collidingBoundingBoxes=new ArrayList();int field_35472_c=128;
 int index(int x,int y,int z){return ((x+24)*48+(z+24))*128+y;}
 public int getBlockId(int x,int y,int z){return y<0||y>=128||x< -24||x>=24||z< -24||z>=24?0:blocks[index(x,y,z)]&255;}
 public int getBlockMetadata(int x,int y,int z){return y<0||y>=128||x< -24||x>=24||z< -24||z>=24?0:metadata[index(x,y,z)]&255;}
 public Material getBlockMaterial(int x,int y,int z){Block b=Block.blocksList[getBlockId(x,y,z)];return b==null?Material.air:b.blockMaterial;}
 boolean blockExists(int x,int y,int z){return x>= -24&&x<24&&z>= -24&&z<24;}
 boolean checkChunksExist(int x,int y,int z,int xx,int yy,int zz){return true;}
 boolean isBlockNormalCube(int x,int y,int z){Block b=Block.blocksList[getBlockId(x,y,z)];return b!=null&&b.blockMaterial.getIsOpaque()&&b.renderAsNormalBlock();}
'''+world+method('World.java','public boolean handleMaterialAcceleration(AxisAlignedBB')+method('World.java','public boolean isMaterialInBB(AxisAlignedBB')+method('World.java','public boolean getIsAnyLiquid(AxisAlignedBB')+'''
}
class Entity {
 World worldObj;double posX,posY,posZ,motionX,motionY,motionZ;float rotationYaw,rotationPitch,yOffset=1.62F,ySize=0,stepHeight=.5F,width=.6F,height=1.8F,fallDistance=0,landMovementFactor=.1F,jumpMovementFactor=.02F;
 boolean onGround=false,isCollidedHorizontally,isCollidedVertically,isCollided,noClip=false,isInWeb=false,field_9293_aM=true,inWater=false,sneak=false,sprint=false;
 AxisAlignedBB boundingBox=AxisAlignedBB.getBoundingBox(0,0,0,0,0,0);
 boolean isSneaking(){return sneak;}boolean isSprinting(){return sprint;}boolean isInWater(){return inWater;}
 void fall(float f){}int delay=0,sprintToggleTimer,sprintingTicksLeft,flyToggleTimer;
 float speedOnGround=.1F,speedInAir=.02F;boolean isAirBorne;Capabilities capabilities=new Capabilities();MovementInput movementInput=new MovementInput();
 boolean isUsingItem(){return false;}boolean isPotionActive(Potion p){return false;}Effect getActivePotionEffect(Potion p){return new Effect();}Food getFoodStats(){return new Food();}
 void setSprinting(boolean s){sprint=s;sprintingTicksLeft=s?600:0;}
'''+method('Entity.java','public void setPosition(double')+method('Entity.java','protected void setRotation(float')+method('Entity.java','protected void updateFallState(double')+method('Entity.java','public boolean handleWaterMovement()')+method('Entity.java','public boolean handleLavaMovement()')+method('Entity.java','public boolean isOffsetPositionInLiquid(double')+method('Entity.java','public void moveFlying(float')+move+heading+method('EntityLiving.java','public boolean isOnLadder()')+'''
'''+method('EntityLiving.java','protected void jump()')+method('EntityPlayerSP.java','private boolean isBlockTranslucent(int')+method('EntityPlayerSP.java','protected boolean pushOutOfBlocks(double')+'''
 void tick(float strafe,float forward,boolean jump,boolean sneaking){
  inWater=handleWaterMovement();if(inWater)fallDistance=0;sneak=sneaking;
  if(sprintingTicksLeft>0&&--sprintingTicksLeft==0)setSprinting(false);if(sprintToggleTimer>0)--sprintToggleTimer;
  movementInput.nextStrafe=strafe;movementInput.nextForward=forward;movementInput.nextJump=jump;movementInput.nextSneak=sneaking;
  boolean var1;
'''+input_step+'''
  if(flyToggleTimer>0)--flyToggleTimer;
  strafe=movementInput.moveStrafe;forward=movementInput.moveForward;
  if(delay>0)--delay;
  if(jump){if(inWater||handleLavaMovement())motionY+=(double).04F;else if(onGround&&delay==0){jump();delay=10;}}
  else delay=0;
  if(capabilities.isFlying){double old=motionY;float oldFactor=jumpMovementFactor;jumpMovementFactor=.05F;moveEntityWithHeading(strafe*.98F,forward*.98F);motionY=old*.6D;jumpMovementFactor=oldFactor;}
  else moveEntityWithHeading(strafe*.98F,forward*.98F);
  landMovementFactor=speedOnGround;jumpMovementFactor=speedInAir;
  if(isSprinting()){landMovementFactor=(float)((double)landMovementFactor+(double)speedOnGround*.3D);jumpMovementFactor=(float)((double)jumpMovementFactor+(double)speedInAir*.3D);}
  if(onGround&&capabilities.isFlying)capabilities.isFlying=false;
 }
}
public class PlayerOracle {
 static void initBlocks(){int[] full={1,2,3,4,5,7,13,14,15,16,17,21,35,43,56,58,73};for(int id:full)new Block(id,Material.rock);
  for(int id:new int[]{31,37,50,59,72})new NullBlock(id,Material.plants);
  new BlockFluid(9,Material.water);new BlockFluid(10,Material.lava);new BlockFluid(11,Material.lava);
  new Stairs(53);new Stairs(67);new Fence(85);new Pane(102);new Door(64);Block.ladder=new Ladder(65);Block.fenceGate=new Block(107,Material.wood);
  Block f=new Block(60,Material.rock);f.maxY=(double)(15.0F/16.0F);
 }
 public static void main(String[] args)throws Exception{
  initBlocks();World w=new World();try(DataInputStream in=new DataInputStream(new FileInputStream(args[0]))){w.blocks=new byte[48*48*128];w.metadata=new byte[w.blocks.length];in.readFully(w.blocks);in.readFully(w.metadata);}
  Entity p=new Entity();p.worldObj=w;p.setRotation(Float.parseFloat(args[4]),0);p.setPosition(Double.parseDouble(args[1]),Double.parseDouble(args[2]),Double.parseDouble(args[3]));p.motionY=-.0784000015258789D;
  p.capabilities.allowFlying=args.length>6&&args[6].equals("1");
  try(Scanner sc=new Scanner(new File(args[5]))){while(sc.hasNext()){
   p.tick(sc.nextFloat(),sc.nextFloat(),sc.nextInt()!=0,sc.nextInt()!=0);
   double[] v={p.posX,p.posY,p.posZ,p.motionX,p.motionY,p.motionZ,p.boundingBox.minX,p.boundingBox.minY,p.boundingBox.minZ,p.boundingBox.maxX,p.boundingBox.maxY,p.boundingBox.maxZ};
   for(double d:v)System.out.print(Long.toHexString(Double.doubleToRawLongBits(d))+" ");
   System.out.println((p.onGround?1:0)+" "+(p.isCollidedHorizontally?1:0)+" "+Integer.toHexString(Float.floatToRawIntBits(p.ySize))+" "+Integer.toHexString(Float.floatToRawIntBits(p.fallDistance)));
   AxisAlignedBB.clearBoundingBoxPool();Vec3D.initialize();
  }}
 }
}
'''

class Pair(ctypes.Structure):_fields_=[('lo',ctypes.c_uint32),('hi',ctypes.c_uint32)]
class Runtime(ctypes.Structure):
 _fields_=[('present',ctypes.c_int32)]+[(n,Pair)for n in ['pos_x','pos_y','pos_z','motion_x','motion_y','motion_z']]+[(n,ctypes.c_uint32)for n in ['yaw_bits','pitch_bits','fall_distance_bits']]+[(n,ctypes.c_int32)for n in ['dimension','fire','air','on_ground','health','block_x','block_y','block_z','food','flying','mayfly']]
class Box(ctypes.Structure):_fields_=[('low',ctypes.c_int32*3),('high',ctypes.c_int32*3)]
class Physical(ctypes.Structure):
 _fields_=[('box',Box)]+[(n,ctypes.c_float)for n in ['yaw','pitch','y_size','fall_distance','move_forward','land_factor','air_factor']]+[(n,ctypes.c_int32)for n in ['grounded','horizontal','vertical','sneak','jump_delay','water','lava','ticks','sprint','sprint_timer','sprint_left','flying','fly_timer','previous_jump','food','mayfly']]
def pair(v):b=struct.unpack('<Q',struct.pack('<d',v))[0];return Pair(b&0xffffffff,b>>32)
def fbits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def scenario(name):
 blocks=bytearray(48*48*128);metadata=bytearray(len(blocks))
 def put(x,y,z,id,meta=0):i=((x+24)*48+(z+24))*128+y;blocks[i]=id;metadata[i]=meta
 for x in range(-24,24):
  for z in range(-24,24):
   if name!='ledge' or z<1:put(x,63,z,1)
 controls=[(0,1,0,0)]*35;pos=(.5,64+struct.unpack('<f',struct.pack('<f',1.62))[0],.5);yaw=0
 if name=='jump':controls=[(0,0,1,0)]*30+[(0,0,0,0)]*10
 if name=='wall':
  for x in range(-2,3):
   for y in range(64,67):put(x,y,2,1)
 if name=='stairs':
  for x in range(-2,3):put(x,64,2,53,2)
 if name=='fence':
  for x in range(-2,3):put(x,64,2,85)
 if name=='pane':put(0,64,2,102)
 if name=='door':put(0,64,2,64,0);put(0,65,2,64,8)
 if name=='ledge':controls=[(0,1,0,1)]*70
 if name=='ladder':
  for y in range(64,70):put(0,y,0,65,3);put(0,y,-1,1)
  yaw=180
 if name in ('water','lava','current'):
  pos=(.5,65.2,.5);controls=[(0,1,1,0)]*20+[(0,0,0,0)]*10
  for x in range(-4,5):
   for z in range(-4,10):put(x,64,z,10 if name=='lava' else 9, min(7,max(0,z+3)) if name=='current' else 0)
 if name=='sprint':controls=[(0,0,0,0)]*2+[(0,1,0,0)]*2+[(0,0,0,0)]+[(0,1,0,0)]*10+[(0,1,1,0)]*10+[(0,0,0,0)]*8
 if name=='flight':controls=[(0,0,0,0)]*2+[(0,0,1,0),(0,0,0,0)]+[(0,1,1,0)]*12+[(0,1,0,1)]*10+[(0,0,0,0)]*8
 if name=='diagonal':controls=[(1,1,0,0)]*35;yaw=-39.125
 if name=='saved_rotation':controls=[(1,1,0,0)]*35;yaw=-3239.10546875
 if name=='pushout':pos=(.1,pos[1],.1);put(0,64,0,1);controls=[(0,0,0,0)]*20
 return blocks,metadata,pos,yaw,controls

def run(arm_compiler=None):
 with tempfile.TemporaryDirectory(prefix='mc10-player-',ignore_cleanup_errors=True) as tmp:
  tmp=Path(tmp);dll=tmp/'player.dll'
  subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(PROJECT/'tests/player_fixture.c'),'-o',str(dll)],check=True,capture_output=True)
  java=tmp/'net/minecraft/src';java.mkdir(parents=True)
  for n in ['AxisAlignedBB.java','MathHelper.java','Vec3D.java']:(java/n).write_text((SOURCE/n).read_text(),encoding='utf-8')
  (java/'PlayerOracle.java').write_text(java_source(),encoding='utf-8')
  compile=subprocess.run(['javac','-d',str(tmp),*map(str,java.glob('*.java'))],capture_output=True,text=True)
  assert compile.returncode==0,compile.stderr
  lib=ctypes.CDLL(str(dll));lib.mc_player_tick.argtypes=[ctypes.c_float,ctypes.c_float,ctypes.c_int32,ctypes.c_int32]
  runtime=Runtime.in_dll(lib,'mc_runtime_player');physical=Physical.in_dll(lib,'mc_player');values=(Pair*16384).in_dll(lib,'mc_fp_values')
  blockmem=(ctypes.c_char*(48*48*128)).in_dll(lib,'fixture_blocks');metamem=(ctypes.c_char*len(blockmem)).in_dll(lib,'fixture_metadata')
  sine=(ctypes.c_uint32*65536).in_dll(lib,'mc_math_sine');ctypes.memmove(sine,(PROJECT/'MC10/math10.dat').read_bytes()[16:],262144)
  lib.mc_math_crc32.restype=ctypes.c_uint32
  assert lib.mc_math_crc32()==struct.unpack_from('<I',(PROJECT/'MC10/math10.dat').read_bytes(),12)[0]
  native_vectors=[];total=0
  for name in ['ground','jump','wall','stairs','fence','pane','door','ledge','ladder','water','lava','current','sprint','flight','diagonal','saved_rotation','pushout']:
   blocks,metadata,pos,yaw,controls=scenario(name);world=tmp/'world.bin';world.write_bytes(blocks+metadata)
   commands=tmp/'controls.txt';commands.write_text('\n'.join(' '.join(map(str,c)) for c in controls))
   result=subprocess.run(['java','-cp',str(tmp),'net.minecraft.src.PlayerOracle',str(world),*map(str,pos),str(yaw),str(commands),'1'if name=='flight'else'0'],check=True,capture_output=True,text=True)
   oracle=[[int(v,16)if i<12 or i>=14 else int(v)for i,v in enumerate(row.split())]for row in result.stdout.splitlines()]
   ctypes.memmove(blockmem,bytes(blocks),len(blocks));ctypes.memmove(metamem,bytes(metadata),len(metadata))
   runtime.present=1;runtime.pos_x=pair(pos[0]);runtime.pos_y=pair(pos[1]);runtime.pos_z=pair(pos[2]);runtime.motion_x=pair(0);runtime.motion_y=pair(-.0784000015258789);runtime.motion_z=pair(0)
   runtime.yaw_bits=fbits(yaw);runtime.pitch_bits=0;runtime.fall_distance_bits=0;runtime.on_ground=0;runtime.health=20;runtime.food=20;runtime.flying=0;runtime.mayfly=int(name=='flight')
   lib.mc_fixture_init()
   for tick,c in enumerate(controls):
    assert lib.mc_player_tick(*c)==1,(name,tick,ctypes.c_int32.in_dll(lib,'mc_player_status').value)
    out=[v.lo|(v.hi<<32)for v in values[:12]]+[physical.grounded,physical.horizontal,fbits(physical.y_size),fbits(physical.fall_distance)]
    expected=oracle[tick]
    assert out==expected,(name,tick,[(i,hex(a),hex(b))for i,(a,b)in enumerate(zip(out,expected))if a!=b])
    if tick<8:native_vectors.append((name,tick,c,expected))
    total+=1
   print('Source player trajectory:',name,'PASS',len(controls),'ticks')
   if arm_compiler and name in ['ground','stairs','water','current','sprint','flight','diagonal','saved_rotation','pushout']:
    from player_arm import run_case
    run_case(PROJECT,tmp,arm_compiler,name,blocks,metadata,pos,yaw,controls,oracle)
  # A failed residency query must roll back numbers, box and player flags.
  before=bytes(runtime);before_player=bytes(physical);ctypes.c_int32.in_dll(lib,'fixture_missing').value=1
  assert lib.mc_player_tick(0,1,1,0)==0
  assert bytes(runtime)==before and bytes(physical)==before_player
  print(f'Player/collision Java oracle: PASS {total} bit-exact ticks; missing-chunk rollback PASS')
  runtime.motion_x=pair(11);runtime.motion_y=pair(-10.5);runtime.motion_z=pair(10)
  lib.mc_fixture_init();assert values[3].lo==values[3].hi==values[4].lo==values[4].hi==0
  assert values[5].lo|(values[5].hi<<32)==struct.unpack('<Q',struct.pack('<d',10))[0]
  runtime.yaw_bits=0x7f800000;lib.mc_fixture_init();assert ctypes.c_int32.in_dll(lib,'mc_player_status').value==5
  print('Player NBT initialization: PASS source motion clamp and safe non-finite rejection')
  import _ctypes;_ctypes.FreeLibrary(lib._handle)
if __name__=='__main__':
 import argparse
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--arm-compiler',type=Path)
 args=parser.parse_args();run(args.arm_compiler)
