// GPU-only decision: no per-frame CPU readback. One wave per logical token.
WAVE void reuse_token_stats(const float*x,const float*anchor,const uint*state,float*stats,uint n,uint rw,uint rh,uint stride){
 uint t=bid(),lane=__builtin_amdgcn_workitem_id_x();if(t>=n)return;
 uint r=(t&~15u)|((t&1u)<<3)|((t&14u)>>1);
 float d=0,a=0,bad=0;
 if(state[2]&&r/stride<rh&&r%stride<rw){for(uint c=lane;c<1024;c+=32){float v=x[t*1024+c],old=anchor[t*1024+c];if(!__builtin_isfinite(v)||!__builtin_isfinite(old))bad=1;else{d+=absf(v-old);a+=absf(old);}}}
 for(uint offset=16;offset;offset>>=1){d+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(d)));a+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(a)));bad=maxf(bad,value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(bad))));}
 if(!lane){stats[t*3]=d;stats[t*3+1]=a;stats[t*3+2]=bad?1.e30f:d/maxf(a,16.f);}
}
WAVE void reuse_decide(const float*stats,uint*state,uint n,uint period,float global_limit,float local_limit,uint mode,const float*image_delta,uint image_tiles,uint image_cols,float image_limit){
 uint lane=__builtin_amdgcn_workitem_id_x();float d=0,a=0,peak=0,image_peak=0;
 // Require a coherent 2x2 tile change: isolated near-black edge ratios must not veto every pan.
 for(uint t=lane;t<image_tiles;t+=32){if(image_delta[t]>=1.e29f)image_peak=1.e30f;
  if(t%image_cols+1<image_cols&&t+image_cols+1<image_tiles){float patch=minf(minf(image_delta[t],image_delta[t+1]),minf(image_delta[t+image_cols],image_delta[t+image_cols+1]));image_peak=maxf(image_peak,patch);}}
 for(uint t=lane;t<n;t+=32){d+=stats[t*3];a+=stats[t*3+1];peak=maxf(peak,stats[t*3+2]);}
 for(uint offset=16;offset;offset>>=1){d+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(d)));a+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(a)));peak=maxf(peak,value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(peak))));image_peak=maxf(image_peak,value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(image_peak))));}
 if(!lane){float rel=d/maxf(a,16.f);uint reason=!state[2]?1:mode==2?2:state[1]+1>=period?3:(!__builtin_isfinite(rel)||peak>=1.e29f||image_peak>=1.e29f)?4:mode==3?0:rel>global_limit?5:peak>local_limit?6:image_peak>image_limit?7:0;
 state[0]=reason==0;state[1]=reason?0:state[1]+1;state[2]=1;state[3]++;state[4]=bits(rel);state[5]=bits(peak);state[6]=reason;state[7]=bits(image_peak);}
}
WAVE void reuse_finish(const float*x,const float*full,float*anchor_in,float*anchor_out,const float*gain,const uint*state,float*out,uint count){
 uint i=bid()*256+__builtin_amdgcn_workitem_id_x();bool reuse=state[0]!=0;
 for(uint j=0;j<8;j++,i+=32)if(i<count){float v=x[i];if(reuse){float a=anchor_in[i],b=anchor_out[i];out[i]=v==a?b:F(H(__builtin_fmaf(gain[i%1024],v-a,b)));}else{float y=full[i];out[i]=y;anchor_in[i]=v;anchor_out[i]=y;}}
}
// Exact 32x32 RGB tile means; sparse sampling aliases moving edges.
WAVE void reuse_image_stats(const float*rgba,const float*anchor,const uint*state,float*signature,float*delta,uint width,uint height){
 uint tile=bid(),cols=(width+31)/32,rows=(height+31)/32,lane=__builtin_amdgcn_workitem_id_x();if(tile>=cols*rows)return;
 float r=0,g=0,b=0,bad=0;
 for(uint p=lane;p<1024;p+=32){uint x=tile%cols*32+p%32,y=tile/cols*32+p/32;x=x<width?x:width-1;y=y<height?y:height-1;
  float vr=rgba[(y*width+x)*4],vg=rgba[(y*width+x)*4+1],vb=rgba[(y*width+x)*4+2];if(!__builtin_isfinite(vr)||!__builtin_isfinite(vg)||!__builtin_isfinite(vb))bad=1;else{r+=vr;g+=vg;b+=vb;}}
 for(uint o=16;o;o>>=1){r+=value(__builtin_amdgcn_ds_bpermute(((lane+o)&31)*4,bits(r)));g+=value(__builtin_amdgcn_ds_bpermute(((lane+o)&31)*4,bits(g)));b+=value(__builtin_amdgcn_ds_bpermute(((lane+o)&31)*4,bits(b)));bad=maxf(bad,value(__builtin_amdgcn_ds_bpermute(((lane+o)&31)*4,bits(bad))));}
 if(!lane){r/=1024;g/=1024;b/=1024;signature[tile*3]=r;signature[tile*3+1]=g;signature[tile*3+2]=b;float rel=0;if(state[2]){float ar=anchor[tile*3],ag=anchor[tile*3+1],ab=anchor[tile*3+2];rel=(absf(r-ar)+absf(g-ag)+absf(b-ab))/maxf(absf(ar)+absf(ag)+absf(ab),1.e-6f);}delta[tile]=bad?1.e30f:rel;}
}
WAVE void reuse_image_commit(const float*signature,float*anchor,const uint*state,uint count){
 if(state[0])return;uint i=bid()*256+__builtin_amdgcn_workitem_id_x();for(uint j=0;j<8;j++,i+=32)if(i<count)anchor[i]=signature[i];
}
