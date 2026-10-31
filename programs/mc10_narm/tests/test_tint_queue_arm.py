"""Execute actual production quad/list queue + tint LRU as NARM Thumb.

The transport consumer is a strict deferred reader: it checks vertex values,
texture/mask bytes only when the queued batch is flushed. This catches slots
overwritten between submission and consumption, including LRU-triggered flush.
No native service or RP2350/emulator is called.
"""
from pathlib import Path
import argparse
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]

def extract(text,name):
    import re
    m=re.search(r'void '+name+r'\([^;]*?\)\s*\{',text);assert m,name
    start=m.start();end=text.index('{',start)+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

PRELUDE=r'''
#include "narm.tch"
#define MC10_WORLD_SYSTEMS 1
#define MC10_TILE_PROFILE 1
#define MC_RENDER_LIST_CAPACITY 64
struct MCCachedQuad{float vertex[20];uint style;};
struct MCCachedQuad mc_render_geometry[128];
uint mc_render_light_atlas[16384*16],mc_asset_mask[2400];
uint mc_render_list_commands[MC_RENDER_LIST_CAPACITY*7];
float mc_render_list_vertices[MC_RENDER_LIST_CAPACITY*20];
float probe_immutable[128*20];float mc_render_camera[18];
int mc_render_source_tint_slot,mc_render_atlas_variant=15,mc_render_faces,mc_render_list_count,mc_render_list_enabled=1,mc_render_list_error;
int mc_profile_active;uint mc_profile_draw_us;
uint mc_render_raw_light_pair;int mc_render_light_key;
int mc_render_capture,mc_render_geometry_count,mc_render_geometry_limit=128,mc_render_capture_overflow,mc_render_capture_fancy,mc_render_capture_cull;
uint mc_asset_loaded_mask_bytes=9600;
uint probe_rgba[256];
uint *mc_block_color_tile(int tile){return probe_rgba;}
uint *mc_block_mesh_tint_pixels(int slot,int variant){return mc_render_light_atlas;}
uint *mc_block_mesh_tint_mask(int slot){return mc_asset_mask;}
int probe_consumed,probe_error,probe_flushes,probe_expected_start;
void mc_render_list_flush(void)
{
    int i,j;uint *command;float *v;unsigned char *texture;uint *mask;
    probe_flushes=probe_flushes+1;
    for(i=0;i<mc_render_list_count;i=i+1)
    {
        command=&mc_render_list_commands[i*7];v=(float *)command[0];texture=(unsigned char*)command[1];mask=(uint*)command[2];
        j=probe_expected_start+probe_consumed;
        if(v[0]!=(float)j||v[5]!=(float)j+1.0f||v[10]!=(float)j+2.0f||v[15]!=(float)j+3.0f)probe_error=1000+j;
        if(command[3]!=16||command[4]!=16||!mask)probe_error=2000+j;
        if(texture[0]!=probe_expected[j]||texture[255]!=probe_expected[j])probe_error=3000+j;
        if(mask[0]!=0xffffffff||mask[7]!=0xffffffff)probe_error=4000+j;
        probe_consumed=probe_consumed+1;
    }
    mc_render_list_count=0;
}
#include "mc10_biome_tint.tc"
'''
MAIN=r'''
int main(void)
{
    int i,j,slot;uint rgb;float *v;
    for(i=0;i<256;i=i+1)probe_rgba[i]=0xffe5650d;
    mc_tint_reset();probe_consumed=0;probe_expected_start=0;
    for(i=0;i<96;i=i+1)
    {
        rgb=(0x102030+(uint)i*0x010203)&0xffffff;mc_tint_begin_quad(143,rgb);
        mc_render_quad((float)i,0,0,0,0,(float)i+1.0f,1,0,15,0,(float)i+2.0f,1,1,15,15,(float)i+3.0f,0,1,0,15,1);
        if(probe_error)return probe_error;
    }
    mc_render_list_flush();if(probe_error)return probe_error;if(probe_consumed!=96)return 5000;
    /* All cached vertices are immutable. Queue an entire replay with >16
     * distinct tints, checking that slot eviction flushes before texture writes. */
    mc_tint_reset();probe_consumed=0;probe_expected_start=0;
    for(i=0;i<96;i=i+1){v=&probe_immutable[i*20];v[0]=(float)i;v[5]=(float)i+1.0f;v[10]=(float)i+2.0f;v[15]=(float)i+3.0f;}
    for(i=0;i<96;i=i+1)
    {rgb=(0x102030+(uint)i*0x010203)&0xffffff;mc_tint_begin_quad(143,rgb);mc_render_list_queue(&probe_immutable[i*20],1);if(probe_error)return probe_error;}
    mc_render_list_flush();if(probe_error)return probe_error;if(probe_consumed!=96)return 5001;
    /* Alternating 16-hot-key traffic accumulates deferred quads without LRU
     * misses, then forces both capacity and a seventeenth-key eviction. */
    mc_tint_reset();probe_consumed=0;probe_expected_start=0;
    for(i=0;i<96;i=i+1)
    {j=i<95?i%16:95;rgb=(0x102030+(uint)j*0x010203)&0xffffff;probe_expected[i]=probe_expected_original[j];mc_tint_begin_quad(143,rgb);mc_render_quad((float)i,0,0,0,0,(float)i+1.0f,1,0,15,0,(float)i+2.0f,1,1,15,15,(float)i+3.0f,0,1,0,15,1);if(probe_error)return probe_error;}
    mc_render_list_flush();if(probe_error)return probe_error;if(probe_consumed!=96)return 5002;
    return 0;
}
'''
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--game',type=Path,required=True);ap.add_argument('--compiler',type=Path,required=True);ap.add_argument('--output',type=Path,default=ROOT/'tint_queue_arm_temp');a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    source=(a.game/'src/mc10_render.tc').read_text();f32=lambda n:struct.unpack('<f',struct.pack('<f',n))[0]
    expected=[]
    for i in range(96):
        rgb=(0x102030+i*0x010203)&0xffffff;r,g,b=[int(f32(c*f32(((rgb>>shift)&255)/255)))for c,shift in((13,16),(101,8),(229,0))];expected.append((r&224)|((g>>5)<<2)|(b>>6))
    data='unsigned char probe_expected[]={'+','.join(map(str,expected))+'};\nunsigned char probe_expected_original[]={'+','.join(map(str,expected))+'};\n'
    code=data+PRELUDE+'\n'+extract(source,'mc_render_list_queue')+'\n'+extract(source,'mc_render_quad')+'\n'+MAIN
    tc=a.output/'tint_queue_arm.tc';image=tc.with_suffix('.narm');tc.write_text(code)
    result=subprocess.run([str(a.compiler),str(tc),'-I',str(a.game),'-I',str(a.game/'src'),'-O3','--stack-bytes','32768','-o',str(image)],capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
    print(result.stdout.strip(),flush=True)
    sys.path.insert(0,r'C:\Users\enzob\Documents\Codex\2026-10-13\co\work\arm_tools');sys.path.insert(0,str(a.game/'tests'))
    from player_arm import execute
    status=execute(image);assert status==0,('NARM Thumb deferred tint queue hazard',status)
    print('Actual production NARM Thumb deferred-queue tint probe PASS:288quads,96distincttints;direct/cachedvertices,textures,masks read onlyatflush;capacityandLRUeviction')
if __name__=='__main__':main()

