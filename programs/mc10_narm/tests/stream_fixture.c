/* Host-only region/cache adapter; exercises the actual fixed-pool provider. */
typedef unsigned int uint;
#include "mc10_types.tc"
#include "mc10_u64.tc"
#include "mc10_java_double.tc"
#include "mc10_java_random.tc"
#include "mc10_chunk.tc"
#define MC10_PLAYER_FOUNDATION 1
#define MC_RUNTIME_CHUNKS 9
#define MC_REGION_OK 0
#define MC_REGION_ERR_MISSING -23
struct MCWorldInfo {int hardcore,game_type;
#ifdef MC_TEST_WORLD_STREAM
uint random_seed_lo,random_seed_hi;
#endif
};
struct MCWorldRuntime {struct MCWorldInfo *info;};
struct MCResidentChunk {
 struct MCChunk chunk;struct MCNibbleArray data,skylight,blocklight;
 int state,status,compression,relocated;uint timestamp,raw_bytes;
};
struct MCResidentChunk mc_runtime_chunks[9];
char mc_runtime_blocks[9*32768],mc_runtime_metadata[9*16384];
char mc_runtime_skylight[9*16384],mc_runtime_blocklight[9*16384],mc_runtime_heightmap[9*256];
struct MCWorldRuntime mc_runtime_world;
struct MCWorldInfo fixture_world;
int mc_runtime_center_x,mc_runtime_center_z,mc_runtime_loaded,mc_runtime_missing,mc_runtime_invalid;
int mc_runtime_entities,mc_runtime_tiles,mc_runtime_ticks,mc_runtime_access_ok;
int mc_region_compression,mc_region_relocated;uint mc_region_timestamp,mc_region_raw_bytes;
int fixture_load_calls,fixture_fail_enabled,fixture_fail_x,fixture_fail_z,fixture_fail_status;
int fixture_cache_begin,fixture_cache_pumps,fixture_cache_cancel,fixture_verify_fail;
int fixture_block_error;
int mc_render_rebuild_bad_id,mc_render_rebuild_bad_x,mc_render_rebuild_bad_y,mc_render_rebuild_bad_z;
int fixture_use_bank,fixture_bank_count,fixture_bank_x[64],fixture_bank_z[64];
char fixture_bank[64*82176];

struct MCResidentChunk *mc_runtime_find_chunk(int x,int z)
{int i;for(i=0;i<9;i++)if(mc_runtime_chunks[i].state==1&&mc_chunk_is_at_location(&mc_runtime_chunks[i].chunk,x,z))return &mc_runtime_chunks[i];return 0;}
int mc_runtime_chunk_exists(int x,int z){return mc_runtime_find_chunk(x,z)!=0;}
int mc_runtime_verify_access(void)
{
 int i;struct MCResidentChunk *r;
 if(fixture_verify_fail)return 0;
 for(i=0;i<9;i++)
 {
  r=&mc_runtime_chunks[i];
  if(r->state!=1||r->chunk.blocks!=&mc_runtime_blocks[i*32768]||r->chunk.height_map!=&mc_runtime_heightmap[i*256]||
     r->chunk.data!=&r->data||r->chunk.skylight_map!=&r->skylight||r->chunk.blocklight_map!=&r->blocklight||
     r->data.data!=&mc_runtime_metadata[i*16384]||r->skylight.data!=&mc_runtime_skylight[i*16384]||r->blocklight.data!=&mc_runtime_blocklight[i*16384])return 0;
 }
 return 1;
}
void mc_render_rebuild_cancel(void)
{fixture_cache_cancel++;fixture_cache_pumps=0;mc_render_rebuild_bad_id=0;}
void mc_render_rebuild_begin(void){fixture_cache_begin++;fixture_cache_pumps=0;}
int mc_render_rebuild_step(void)
{
 fixture_cache_pumps++;
 if(fixture_cache_pumps>=5&&fixture_block_error)
 {mc_render_rebuild_bad_id=200;mc_render_rebuild_bad_x=-513;mc_render_rebuild_bad_y=127;mc_render_rebuild_bad_z=-1;}
 return fixture_cache_pumps>=5;
}
int mc_render_rebuild_block_error(void){return mc_render_rebuild_bad_id!=0;}
uint fixture_pattern(int x,int z,int index){return ((uint)x*37u+(uint)z*73u+(uint)index*13u+((uint)index>>8))&255;}
int mc_region_load_chunk(struct MCWorldInfo *world,int x,int z,
 struct MCChunk *chunk,struct MCNibbleArray *data,struct MCNibbleArray *sky,struct MCNibbleArray *light,
 char *blocks,char *metadata,char *skylight,char *blocklight,char *heightmap)
{
 int i,bank=-1;char *dest;
 fixture_load_calls++;
 if(fixture_use_bank)
 {for(i=0;i<fixture_bank_count;i++)if(fixture_bank_x[i]==x&&fixture_bank_z[i]==z)bank=i;if(bank<0)return MC_REGION_ERR_MISSING;}
 for(i=0;i<82176;i++)
 {
  dest=i<32768?blocks+i:(i<49152?metadata+i-32768:(i<65536?skylight+i-49152:(i<81920?blocklight+i-65536:heightmap+i-81920)));
  *dest=bank<0?(char)fixture_pattern(x,z,i):fixture_bank[bank*82176+i];
 }
 if(fixture_fail_enabled&&x==fixture_fail_x&&z==fixture_fail_z)return fixture_fail_status;
 mc_nibble_array_bind(data,metadata,7);mc_nibble_array_bind(sky,skylight,7);mc_nibble_array_bind(light,blocklight,7);
 mc_chunk_bind(chunk,blocks,heightmap,data,sky,light,x,z);
 chunk->last_update_lo=(uint)x;chunk->last_update_hi=(uint)z;chunk->is_terrain_populated=1;
 chunk->entity_count=(x&3);chunk->tile_entity_count=(z&3);chunk->tile_tick_count=2;chunk->has_entities=chunk->entity_count!=0;
 mc_region_compression=2;mc_region_relocated=(x+z)&1;mc_region_timestamp=(uint)(x^z);mc_region_raw_bytes=82200;
 return 0;
}
#include "mc10_stream.tc"
void fixture_init(int x,int z)
{
 int i;struct MCResidentChunk *r;
 mc_runtime_center_x=x;mc_runtime_center_z=z;mc_runtime_world.info=&fixture_world;
 fixture_fail_enabled=0;fixture_verify_fail=0;fixture_block_error=0;
 for(i=0;i<9;i++)
 {
  r=&mc_runtime_chunks[i];r->state=1;
  mc_region_load_chunk(&fixture_world,x+i%3-1,z+i/3-1,&r->chunk,&r->data,&r->skylight,&r->blocklight,
    &mc_runtime_blocks[i*32768],&mc_runtime_metadata[i*16384],&mc_runtime_skylight[i*16384],&mc_runtime_blocklight[i*16384],&mc_runtime_heightmap[i*256]);
  r->status=0;r->compression=mc_region_compression;r->relocated=mc_region_relocated;
  r->timestamp=mc_region_timestamp;r->raw_bytes=mc_region_raw_bytes;
 }
 fixture_load_calls=0;fixture_cache_begin=0;fixture_cache_cancel=0;
 mc_stream_reset();mc_stream_request(x,z);while(mc_stream_step()==0){}
 fixture_load_calls=0;fixture_cache_begin=0;fixture_cache_cancel=0;mc_stream_commits=0;
}
int fixture_slot_x(int i){return mc_runtime_chunks[i].chunk.x_position;}
int fixture_slot_z(int i){return mc_runtime_chunks[i].chunk.z_position;}
int fixture_slot_state(int i){return mc_runtime_chunks[i].state;}
uint fixture_slot_metadata(int i,int field)
{
 struct MCResidentChunk *r=&mc_runtime_chunks[i];
 if(field==0)return r->chunk.last_update_lo;if(field==1)return r->chunk.last_update_hi;
 if(field==2)return r->chunk.entity_count;if(field==3)return r->chunk.tile_entity_count;
 if(field==4)return r->chunk.tile_tick_count;if(field==5)return r->timestamp;
 if(field==6)return r->compression;if(field==7)return r->raw_bytes;
 return 0;
}
