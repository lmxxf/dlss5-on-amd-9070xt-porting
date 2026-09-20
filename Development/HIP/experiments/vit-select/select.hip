// AttExp only. Append to deep_fast.hip; all original entry points remain intact.
DEV float select_load(const unsigned char*p,uint t,uint c){return __builtin_amdgcn_cvt_f32_fp8(p[t*1024+c],0);}
DEV float select_reduce(float x){uint lane=__builtin_amdgcn_workitem_id_x();for(uint d=16;d;d>>=1)x+=value(__builtin_amdgcn_ds_bpermute((lane^d)*4,bits(x)));return x;}
WAVE void vit_select_prep(const unsigned char*in,unsigned char*pool,uint*route,uint tokens,float threshold){
 uint G=(tokens+31)/32,h=bid()/G,g=bid()%G,first=g*32,c=__builtin_amdgcn_workitem_id_x(),col=h*32+c,KT=tokens/2;
 if(h>=32)return;float kd=0,kn=0,vd=0,vn=0,unsafe=0;bool spatial=first+31<tokens;uint cols=tokens==400?25:32;
 for(uint j=0;j<16&&first+2*j+1<tokens;j++){
  uint t=first+2*j,pair=t/2;spatial=spatial&&(t/cols==(t+1)/cols);
  float k0=select_load(in,tokens+t,col),k1=select_load(in,tokens+t+1,col);
  float v0=select_load(in,2*tokens+t,col),v1=select_load(in,2*tokens+t+1,col);
  kd+=(k0-k1)*(k0-k1);kn+=k0*k0+k1*k1;vd+=(v0-v1)*(v0-v1);vn+=v0*v0+v1*v1;
  // Keep P quantization at the original scale. Encode multiplicity in V instead.
  float vm=F(.5f*(v0+v1));unsafe+=absf(vm)>224.f?1.f:0.f;
  pool[pair*1024+col]=byte_F(.5f*(k0+k1));pool[(KT+pair)*1024+col]=byte_F(vm*2.f);
 }
 kd=select_reduce(kd);kn=select_reduce(kn);vd=select_reduce(vd);vn=select_reduce(vn);
 spatial=spatial&&select_reduce(unsafe)==0.f;
 float distance=__builtin_fmaxf(kd/__builtin_fmaxf(kn,1.e-12f),vd/__builtin_fmaxf(vn,1.e-12f));
 if(c==0){route[(h*G+g)*2]=spatial&&distance<=threshold;route[(h*G+g)*2+1]=bits(distance);}
}
template<uint MAXT>DEV void vit_select_body(const unsigned char*in,const unsigned char*pool,const uint*route,float*out,uint tokens){
 uint head=bid()%32,first=bid()/32*16;if(first>=tokens)return;
 __attribute__((shared)) unsigned short t16[2*16*24];
 const uint S=tokens+16,G=(tokens+31)/32,KT=tokens/2;
 i2 q[2]{};for(uint k=0;k<32;k+=16)__builtin_memcpy(&q[k/16],in+(first+rc())*1024+head*32+k+gr()*8,8);
 f8 sum{},acc[2]{};uint tile=0;
 for(uint key=0;key<tokens;){uint group=key;bool pooled=(key&31u)==0&&route[(head*G+key/32)*2]&&first/32!=key/32;f8 a{};
   const unsigned char*ks=(pooled?pool:in)+(pooled?key/2:tokens+key)*1024+rc()*1024+head*32;
   for(uint k=0;k<32;k+=16){i2 y{};__builtin_memcpy(&y,ks+k+gr()*8,8);a=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(q[k/16],y,a);}
   unsigned short*t=t16+tile*16*24;
   for(uint e=0;e<8;e++){float af=clampf(a[e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);uint hb=(bits(af)>>13)-0x1c000u;unsigned short u=(unsigned short)(((hb<<4)+0x4000u)&65535u);t[(gr()*8+e)*24+rc()]=u;}
   __builtin_amdgcn_fence(3,"workgroup");__builtin_amdgcn_s_barrier();__builtin_amdgcn_fence(2,"workgroup");
   h8 x{};i2 xb{};for(uint e=0;e<8;e++){unsigned short u=t[rc()*24+gr()*8+e];union{unsigned short u;_Float16 h;}cv;cv.u=u;x[e]=cv.h;pack(xb,e,from_half(u));}
   h8 multiplier{};for(uint e=0;e<8;e++)multiplier[e]=(_Float16)(pooled?2.f:1.f);sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,multiplier,sum);
   const unsigned char*vs=(pooled?pool:in)+(pooled?KT+key/2:2*tokens+key)*1024+head*32;
   for(uint c=0;c<2;c++){i2 y{};for(uint e=0;e<8;e++){uint b=vs[(gr()*8+e)*1024+c*16+rc()];y[e/4]=int(uint(y[e/4])|(b<<(8*(e%4))));}acc[c]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(xb,y,acc[c]);}
   key+=pooled?32:16;tile^=1u;
 }
 float inv[8];for(uint e=0;e<8;e++)inv[e]=1.f/sum[e];
 __builtin_amdgcn_fence(3,"workgroup");__builtin_amdgcn_s_barrier();__builtin_amdgcn_fence(2,"workgroup");
 for(uint c=0;c<2;c++)for(uint e=0;e<8;e++)out[(first+gr()*8+e)*1024+head*32+c*16+rc()]=F(Hrtz(acc[c][e]*inv[e]));
}
WAVE void vit_attention_select_400(const unsigned char*in,const unsigned char*pool,const uint*route,float*out,uint tokens){vit_select_body<400>(in,pool,route,out,tokens);}
WAVE void vit_attention_select_640(const unsigned char*in,const unsigned char*pool,const uint*route,float*out,uint tokens){vit_select_body<640>(in,pool,route,out,tokens);}
