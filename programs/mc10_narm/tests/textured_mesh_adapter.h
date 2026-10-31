int fixture_mesh_calls,fixture_mesh_max_raw,fixture_mesh_max_ranges;
int fixture_mesh_faces,fixture_mesh_range_total,fixture_mesh_fail=-1;
int fixture_mesh_wrong_count;
int drawTexturedMesh(const uint *mesh,const uint *ranges,uint range_count,
                     const float *transform,uint mode)
{
    int saved=mc_render_atlas_variant,faces=0;uint r,i,j,k,raw=0,first,count,material,ordinal,word,offset;
    const uint *m;float v[20];struct MCCachedQuad *quad,*source;
    assert(mesh==mc_render_mesh_descriptor&&ranges==mc_render_mesh_ranges&&transform==mc_render_camera&&mode==2);
    assert(range_count>0&&range_count<=256);
    assert(mesh[0]==(uint)(uintptr_t)&mc_render_geometry[0]);
    assert(mesh[1]==(uint)mc_render_geometry_count&&mesh[2]==84&&mesh[3]==4&&mesh[4]==80);
    assert(mesh[5]==(uint)(uintptr_t)&mc_render_mesh_indices[0]);
    assert(mesh[6]==(uint)(uintptr_t)&mc_render_mesh_materials[0]&&mesh[7]==512);
    fixture_mesh_calls++;fixture_mesh_range_total+=(int)range_count;
    if((int)range_count>fixture_mesh_max_ranges)fixture_mesh_max_ranges=(int)range_count;
    if(fixture_mesh_fail>=0&&fixture_mesh_calls>fixture_mesh_fail)return -4;
    for(r=0;r<range_count;r++)
    {
        first=ranges[r*2];count=ranges[r*2+1];assert(count&&first+count<=mesh[1]);raw+=count;
        for(i=first;i<first+count;i++)
        {
            quad=&mc_render_geometry[i];material=quad->style;assert(material<512);
            m=&mc_render_mesh_materials[material*6];assert(m[5]<=1);
            if(!m[5])continue;
            assert(m[2]==256&&m[3]==256&&m[4]==0);
            assert(!m[1]||m[1]==(uint)(uintptr_t)&mc_asset_mask[0]);
            for(j=0;j<4;j++)
            {
                word=mc_render_mesh_indices[i*2+j/2];ordinal=(word>>((j&1)*16))&65535;
                assert(ordinal<mesh[1]*4);source=&mc_render_geometry[ordinal/4];
                for(k=0;k<3;k++)((uint *)v)[j*5+k]=((uint *)&source->vertex[(ordinal&3)*5])[k];
                for(k=3;k<5;k++)((uint *)v)[j*5+k]=((uint *)&quad->vertex[j*5])[k];
            }
            offset=m[0]-(uint)(uintptr_t)&mc_render_light_atlas[0];
            assert(offset<sizeof(mc_render_light_atlas)&&offset%65536==0);
            mc_render_atlas_variant=(int)(offset/65536);fixture_capture(v,m[1]!=0);faces++;
        }
    }
    assert(raw<=256);if((int)raw>fixture_mesh_max_raw)fixture_mesh_max_raw=(int)raw;
    fixture_mesh_faces+=faces;mc_render_atlas_variant=saved;
    return faces+fixture_mesh_wrong_count;
}
