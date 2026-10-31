"""Every 1.0 block ItemBlock flat-icon decision against actual owned Java."""
from pathlib import Path
import ctypes as C
import os
import subprocess
import sys
import tempfile
from test_third_person import PROJECT,CANDIDATE,ORIGINAL,method
sys.path.insert(0,str(PROJECT/'tools'))
from source_block_oracle import compile_oracle

def run():
    with tempfile.TemporaryDirectory(prefix='mc10-item-block-',ignore_cleanup_errors=True)as temp:
        tmp=Path(temp);item=(ORIGINAL/'Item.java').read_text();block=(ORIGINAL/'ItemBlock.java').read_text()
        java='''class Item{int iconIndex;Item(int id){}Item setIconIndex(int x){iconIndex=x;return this;}Item setMaxDamage(int x){return this;}Item setHasSubtypes(boolean x){return this;}'''+method(item,'getIconFromDamage')+method(item,'getColorFromDamage')+'}\n'
        java+='class ItemBlock extends Item{private int blockID;'+method(block,'ItemBlock')+method(block,'getBlockID')+'}\n'
        for name in('ItemSapling','ItemColored','ItemLilyPad'):
            source=(ORIGINAL/(name+'.java')).read_text();base='ItemColored'if name=='ItemLilyPad'else'ItemBlock'
            java+='class '+name+' extends '+base+'{'
            if name=='ItemColored':java+='private final Block field_35436_a=Block.blocksList[this.getBlockID()];'
            java+=method(source,name)
            for member in('getIconFromDamage','getColorFromDamage'):
                if member+'('in source:java+=method(source,member)
            java+='}\n'
        render=method((ORIGINAL/'RenderBlocks.java').read_text(),'renderItemIn3d')
        java+='class InventoryFacts{'+render+'''public static void main(String[]args){for(int id=1;id<256;id++){Block b=Block.blocksList[id];if(b==null)continue;Item item=id==6?new ItemSapling(id-256):id==31?new ItemColored(id-256,true):id==106?new ItemColored(id-256,false):id==111?new ItemLilyPad(id-256):new ItemBlock(id-256);for(int m=0;m<16;m++)System.out.println(id+","+m+","+(renderItemIn3d(b.getRenderType())?-1:item.getIconFromDamage(m))+","+item.getColorFromDamage(m));}}}'''
        out=compile_oracle(ORIGINAL,tmp/'oracle',extra_java={'InventoryFacts.java':java})
        result=subprocess.run(['java','-Xms8m','-Xmx64m','-XX:ReservedCodeCacheSize=16m','-XX:CompressedClassSpaceSize=32m','-Xss256k','-cp',str(out),'InventoryFacts'],capture_output=True,text=True);assert result.returncode==0,result.stderr
        fixture=tmp/'item.c';fixture.write_text('typedef unsigned int uint;\n#define MC10_PLAYER_FOUNDATION 1\n#define MC10_WORLD_SYSTEMS 1\n#include "mc10_items.tc"\n')
        dll=tmp/'item.dll';built=subprocess.run(['gcc','-shared','-O2',str(fixture),'-I',str(CANDIDATE/'src'),'-o',str(dll)],capture_output=True,text=True);assert built.returncode==0,built.stderr
        lib=C.CDLL(str(dll));lib.mc_item_icon_color.restype=C.c_uint32;count=flat=0;ids=set()
        for line in result.stdout.splitlines():
            id,damage,icon,color=map(int,line.split(','));actual=lib.mc_item_flat_block_icon(id,damage);assert actual==icon,('owned ItemBlock flatdecision/icon',id,damage,actual,icon)
            if icon>=0:assert lib.mc_item_icon_color(id,damage)==color,('owned ItemBlock inventorytint',id,damage);flat+=1;ids.add(id)
            count+=1
        assert count==122*16 and flat==44*16,(count,flat,sorted(ids))
        import _ctypes;_ctypes.FreeLibrary(lib._handle)
    print('PASS owned ItemBlock source:122 IDs x16metadata=1952 flat/3Ddecisions;44non3DIDs x16metadata exacticons/tints, includesinternalnonobtainableblocks')
if __name__=='__main__':run()

