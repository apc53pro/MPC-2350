int main(void)
{
 int view,pair,order,index,mode,stop,j,warm;uint *words;
 int permutation[6][3]={{0,1,2},{1,2,0},{2,0,1},{0,2,1},{2,1,0},{1,0,2}};struct MCPlayerState saved;
 reset_view();saved=mc_runtime_player;
 assert(mc_geometry_collect()&&frames==72&&mc_bench_completed==60);
 assert(!memcmp(&saved,&mc_runtime_player,sizeof(saved))&&mc_render_show_hud==1&&!fixture_flags&&!mc_profile_active);
 assert(mc_render_list_enabled==1&&mc_render_submit_optimized==1&&!mc_render_cull_mode&&!mc_render_cull_diagnostics);
 for(view=0;view<4;view++)
 {
  index=view*18;
  for(mode=0;mode<3;mode++)
  {
   assert(frame_mode[index+mode]==1&&frame_submit[index+mode]==1);
   assert(frame_flags[index+mode]==(uint)(mode?1537:1025));
   assert(frame_cull[index+mode]==(uint)(mode==2?3:mode));
   assert(frame_cull_diagnostics[index+mode]==(uint)(mode!=0));
  }
  for(mode=1;mode<3;mode++)
  {
   warm=(mode-1)*4+view;
   assert(mc_cull_warmup[warm][0]==20&&mc_cull_warmup[warm][1]==6);
   if(mode==1)for(j=2;j<7;j++)assert(!mc_cull_warmup[warm][j]);
   else
   {
    assert(mc_cull_warmup[warm][2]==40&&mc_cull_warmup[warm][3]==10);
    assert(mc_cull_warmup[warm][4]==80&&mc_cull_warmup[warm][5]==20&&mc_cull_warmup[warm][6]==1);
   }
   assert(mc_geometry_pixel_warmup[warm][0]==(uint)(mode==1?960:860));
   assert(mc_geometry_pixel_warmup[warm][1]==200&&mc_geometry_pixel_warmup[warm][2]==40);
   assert(mc_geometry_pixel_warmup[warm][3]==(uint)(mode==1?700:600));
   assert(mc_geometry_job_warmup[warm][1]==(uint)(mode==1?90:80));
  }
  for(pair=0;pair<5;pair++)for(order=0;order<3;order++)
  {
   mode=permutation[(view*5+pair)%6][order];index=view*18+3+pair*3+order;
   assert(frame_mode[index]==1&&frame_flags[index]==1025&&frame_submit[index]==1);
   assert(frame_cull[index]==(uint)(mode==2?3:mode)&&!frame_cull_diagnostics[index]);
   assert(frame_pitch[index]==(view>=2?0xc2b40000:saved.pitch_bits));
   assert(frame_yaw[index]==saved.yaw_bits&&frame_hud[index]==((view&1)==0));
   assert(mc_geometry_samples[mode*20+view*5+pair].order==(uint)((view*5+pair)%6));
   assert(mc_geometry_samples[mode*20+view*5+pair].flags==1025);
   assert(mc_geometry_samples[mode*20+view*5+pair].tile[1]==(uint)(100-mode*10));
   assert(mc_geometry_samples[mode*20+view*5+pair].tile[2]==(uint)(1060-mode*100));
   for(j=3;j<=5;j++)assert(!mc_geometry_samples[mode*20+view*5+pair].tile[j]);
   for(j=14;j<MC_GEOMETRY_FIELDS;j++)assert(!mc_geometry_samples[mode*20+view*5+pair].stage[j]);
   assert(mc_geometry_samples[mode*20+view*5+pair].stage[0]==(uint)(mode==0?1200:(mode==1?950:800)));
  }
 }
 assert(!mc_render_cull_backfaces&&!mc_render_cull_eligible);
 for(j=60;j<=64;j++)assert(!renderStat((uint)j));
 for(j=67;j<=69;j++)assert(!renderStat((uint)j));
 for(j=71;j<=75;j++)assert(!renderStat((uint)j));
 mc_geometry_format_log();assert(!mc_bench_full&&mc_bench_length<MC_BENCH_LOG_CAP);
 max_write=17;assert(mc_bench_write_log()&&io_writes>1&&!strcmp(created_path,FIXTURE_SECOND_PATH));
 assert(file_length==mc_bench_length&&!memcmp(file_data,mc_bench_log,file_length));
 corrupt_read=1;assert(!mc_bench_write_log());corrupt_read=0;
 write_zero=1;assert(!mc_bench_write_log());write_zero=0;
 for(stop=0;stop<72;stop++)
 {
  reset_view();saved=mc_runtime_player;cancel_after=stop;
  mc_render_submit_optimized=(stop>>2)&1;mc_render_cull_mode=stop&3;mc_render_cull_diagnostics=(stop>>1)&1;
  assert(!mc_geometry_collect()&&!fixture_flags&&!mc_profile_active&&mc_render_list_enabled==1);
  assert(mc_render_submit_optimized==((stop>>2)&1)&&mc_render_cull_mode==(stop&3)&&mc_render_cull_diagnostics==((stop>>1)&1));
  assert(!memcmp(&saved,&mc_runtime_player,sizeof(saved))&&mc_render_show_hud==1);
  reset_view();saved=mc_runtime_player;fixture_service_failure=stop;
  assert(!mc_geometry_collect()&&mc_render_list_error==-4&&!fixture_flags&&!mc_profile_active);
  assert(mc_render_submit_optimized==((stop>>2)&1)&&mc_render_cull_mode==(stop&3)&&mc_render_cull_diagnostics==((stop>>1)&1));
  assert(!memcmp(&saved,&mc_runtime_player,sizeof(saved))&&mc_render_show_hud==1&&mc_render_list_enabled==1);
  fixture_service_failure=-1;
 }
 reset_view();forced_event=NARM_EVENT_FOCUS_LOST;assert(!mc_geometry_collect()&&!fixture_flags);
 reset_view();fixture_bad_mode=1;assert(!mc_geometry_collect()&&!fixture_flags);fixture_bad_mode=0;
 reset_view();fixture_old_firmware=1;assert(!mc_geometry_collect()&&!frames);
 assert(mc_benchmark_main()==1&&!frames);fixture_old_firmware=0;
 reset_view();fixture_old_stage_firmware=1;assert(renderStat(24)==0x54420001);
 assert(!mc_geometry_collect()&&!frames);assert(mc_benchmark_main()==1&&!frames);fixture_old_stage_firmware=0;
 reset_view();fixture_old_geometry_firmware=1;
 assert(renderStat(24)==0x54420001&&renderStat(34)==0x44530001);
 assert(!mc_geometry_collect()&&!frames);assert(mc_benchmark_main()==1&&!frames);fixture_old_geometry_firmware=0;
 reset_view();fixture_old_preparation_firmware=1;j=fixture_world_bootstraps;
 assert(renderStat(65)==0x47500001);
 assert(!mc_geometry_collect()&&!frames&&!fixture_flags&&!mc_profile_active);
 assert(mc_benchmark_main()==1&&!frames&&fixture_world_bootstraps==j);fixture_old_preparation_firmware=0;
 /* Build 24 protocols pass, but missing culling protocol fails before bootstrap. */
 reset_view();fixture_old_culling_firmware=1;j=fixture_world_bootstraps;
 assert(renderStat(65)==0x47500001&&renderStat(66)==0x50500001);
 assert(!mc_geometry_collect()&&!frames&&!fixture_flags&&!mc_profile_active);
 assert(mc_benchmark_main()==1&&!frames&&fixture_world_bootstraps==j);fixture_old_culling_firmware=0;
 reset_view();fixture_old_culling_firmware=2;j=fixture_world_bootstraps;
 assert(!mc_geometry_collect()&&!frames&&!fixture_flags&&!mc_profile_active);
 assert(mc_benchmark_main()==1&&!frames&&fixture_world_bootstraps==j);fixture_old_culling_firmware=0;
 /* Explicitly selected baseline state must not leak into successful play. */
 mc_render_submit_optimized=0;mc_render_cull_mode=0;mc_render_cull_diagnostics=1;
 fixture_test_result_handoff(72,60);
 /* Maximum-width numeric fields need more than 64KiB but fit 96KiB,
  * keeping the mandatory timed detailed fields zero.
  * No invalid timing fixture is reported: the final normal run overwrites it. */
 words=(uint *)&mc_geometry_samples[0];
 for(j=0;j<(int)sizeof(mc_geometry_samples)/4;j++)words[j]=0xffffffff;
 /* Detailed per-pixel/traffic fields are necessarily zero in timed rows. */
 for(index=0;index<60;index++)
 {
  mc_geometry_samples[index].flags=1025;
  for(j=3;j<=5;j++)mc_geometry_samples[index].tile[j]=0;
  mc_geometry_samples[index].tile[6]=15360;
  for(j=14;j<MC_GEOMETRY_FIELDS;j++)mc_geometry_samples[index].stage[j]=0;
 }
 words=(uint *)&mc_geometry_warmup[0];
 for(j=0;j<(int)sizeof(mc_geometry_warmup)/4;j++)words[j]=0xffffffff;
 words=(uint *)&mc_geometry_pixel_warmup[0];
 for(j=0;j<(int)sizeof(mc_geometry_pixel_warmup)/4;j++)words[j]=0xffffffff;
 words=(uint *)&mc_geometry_job_warmup[0];
 for(j=0;j<(int)sizeof(mc_geometry_job_warmup)/4;j++)words[j]=0xffffffff;
 words=(uint *)&mc_cull_warmup[0];
 for(j=0;j<(int)sizeof(mc_cull_warmup)/4;j++)words[j]=0xffffffff;
 mc_geometry_format_log();
 fprintf(stderr,"abc_numeric_bound_bytes=%u\n",mc_bench_length);
 if(mc_bench_full||mc_bench_length<=65536||mc_bench_length>=98304)
  fprintf(stderr,"numeric CSV bound: %u full=%d\n",mc_bench_length,mc_bench_full);
 assert(MC_BENCH_LOG_CAP==98304&&!mc_bench_full&&mc_bench_length>65536&&mc_bench_length<98304);
 reset_view();max_write=100000;assert(mc_benchmark_main()==0&&frames==72&&mc_bench_completed==60&&!fixture_flags&&!game_calls);
 mc_bench_length=MC_BENCH_LOG_CAP-1;mc_bench_char('X');assert(mc_bench_full&&!mc_bench_write_log());
 mc_geometry_format_log();assert(fixture_cull_title_seen&&fixture_cull_completed_title_seen);
 fwrite(mc_bench_log,1,mc_bench_length,stdout);return 0;
}
