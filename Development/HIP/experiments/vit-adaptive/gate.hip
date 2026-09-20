// GPU-only decision: no per-frame CPU readback. One wave per logical token.
WAVE void reuse_token_stats(const float*x,const float*anchor,const uint*state,float*stats,uint n,uint rw,uint rh,uint stride){
 uint t=bid(),lane=__builtin_amdgcn_workitem_id_x();if(t>=n)return;
 uint r=(t&~15u)|((t&1u)<<3)|((t&14u)>>1);
 float d=0,a=0,bad=0;
 if(state[2]&&r/stride<rh&&r%stride<rw){for(uint c=lane;c<1024;c+=32){float v=x[t*1024+c],old=anchor[t*1024+c];if(!__builtin_isfinite(v)||!__builtin_isfinite(old))bad=1;else{d+=absf(v-old);a+=absf(old);}}}
 for(uint offset=16;offset;offset>>=1){d+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(d)));a+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(a)));bad=maxf(bad,value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(bad))));}
 if(!lane){stats[t*3]=d;stats[t*3+1]=a;stats[t*3+2]=bad?1.e30f:d/maxf(a,16.f);}
}
WAVE void reuse_decide(const float*stats,uint*state,uint n,uint period,float global_limit,float local_limit,uint mode){
 uint lane=__builtin_amdgcn_workitem_id_x();float d=0,a=0,peak=0;
 for(uint t=lane;t<n;t+=32){d+=stats[t*3];a+=stats[t*3+1];peak=maxf(peak,stats[t*3+2]);}
 for(uint offset=16;offset;offset>>=1){d+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(d)));a+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(a)));peak=maxf(peak,value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(peak))));}
 if(!lane){float rel=d/maxf(a,16.f);uint reason=!state[2]?1:mode==2?2:state[1]+1>=period?3:peak>=1.e29f?4:mode==3?0:rel>global_limit?5:peak>local_limit?6:0;
 state[0]=reason==0;state[1]=reason?0:state[1]+1;state[2]=1;state[3]++;state[4]=bits(rel);state[5]=bits(peak);state[6]=reason;}
}
WAVE void reuse_finish(const float*x,const float*full,float*anchor_in,float*anchor_out,const float*gain,const uint*state,float*out,uint count){
 uint i=bid()*256+__builtin_amdgcn_workitem_id_x();bool reuse=state[0]!=0;
 for(uint j=0;j<8;j++,i+=32)if(i<count){float v=x[i];if(reuse){float a=anchor_in[i],b=anchor_out[i];out[i]=v==a?b:F(H(__builtin_fmaf(gain[i%1024],v-a,b)));}else{float y=full[i];out[i]=y;anchor_in[i]=v;anchor_out[i]=y;}}
}
