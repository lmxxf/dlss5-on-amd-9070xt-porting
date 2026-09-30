// Explicit typed ViT edges, same reductions and rounding as the production fragment kernels.
#ifndef HIP_VIT_STREAM_KERNELS
#define HIP_VIT_STREAM_KERNELS 0
#endif
#ifndef HIP_VIT_STREAM_QKV_UNROLL
#define HIP_VIT_STREAM_QKV_UNROLL 0
#endif
#ifndef HIP_VIT_STREAM_QKV_HOIST
#define HIP_VIT_STREAM_QKV_HOIST 0 /* 1: vit_stream_qkv_frag_hin reads its per-head Q scale once before the stores (same value; the original re-reads it after every aliasing byte store) */
#endif
#ifndef HIP_VIT_QKV_ORDER
#define HIP_VIT_QKV_ORDER 0 /* 1/2/3: alternative block->(part,token tile,head) orders for vit_stream_qkv_frag_hin; same per-wave work */
#endif
#ifndef HIP_VIT_QKV_W5
#define HIP_VIT_QKV_W5 0 /* 1: add export vit_stream_qkv_frag_hin_w5 (160 threads = 5 waves on one head, 5 token tiles; head weights staged once per group through LDS). Same per-wave WMMA chains/fragments/epilogue as vit_stream_qkv_frag_hin; grid = 96*ceil(tokens/80). */
#endif
#ifndef VIT_QKV_TAIL_VEC
#define VIT_QKV_TAIL_VEC 0 /* 1: vit_stream_qkv_frag_hin_w5 byte tail transposed through the wave's LDS norm tile, one 16-byte store per lane; bytes unchanged (results/deep-tail-20260930) */
#endif
#if HIP_VIT_STREAM_KERNELS
template<uint TM,uint TN,bool ByteAv,bool HalfSkip>
DEV void vit_stream_project_tiles(const float*in,const float*w,const float*skip,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 constexpr uint IN=1024,OUT=1024;
 uint first=bid()/(OUT/(16*TN))*(16*TM),row=bid()%(OUT/(16*TN))*(16*TN);if(first>=tokens)return;
 uint live=0;
#pragma unroll
 for(uint t=0;t<TM;t++)live+=first+t*16<tokens;
 const unsigned char*wf=reinterpret_cast<const unsigned char*>(w);
 f8 total[TM][TN]{};
 for(uint p=0;p<4;p++){f8 a[TM][TN]{};
  if(!p)
#pragma unroll
  for(uint t=0;t<TM;t++)for(uint j=0;j<TN;j++)if(t<live)for(uint e=0;e<8;e++)a[t][j][e]=H((HalfSkip?float(reinterpret_cast<const _Float16*>(skip)[(first+t*16+gr()*8+e)*OUT+row+j*16+rc()]):skip[(first+t*16+gr()*8+e)*OUT+row+j*16+rc()])*w[IN*OUT+row+j*16+rc()]);
  for(uint k=p*(IN/4);k<(p+1)*(IN/4);k+=16){i2 x[TM];
#pragma unroll
   for(uint t=0;t<TM;t++){x[t]=i2{};if(t<live){if constexpr(ByteAv)__builtin_memcpy(&x[t],reinterpret_cast<const unsigned char*>(in)+(first+t*16+rc())*IN+k+gr()*8,8);else {DF_PACK8_LOOP(x[t],in[(first+t*16+rc())*IN+k+gr()*8+e]);}}}
#pragma unroll
   for(uint j=0;j<TN;j++){uint r0=row+j*16;i2 y;__builtin_memcpy(&y,wf+((r0/16)*(IN/32)+k/32)*512+((((k%32)/16)*2+gr())*16+rc())*8,8);
#pragma unroll
    for(uint t=0;t<TM;t++)a[t][j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(x[t],y,a[t][j]);}}
#pragma unroll
  for(uint t=0;t<TM;t++)
#pragma unroll
  for(uint j=0;j<TN;j++)for(uint e=0;e<8;e++)total[t][j][e]=p?total[t][j][e]+a[t][j][e]:a[t][j][e];}
#pragma unroll
 for(uint t=0;t<TM;t++){if(t>=live)continue;for(uint j=0;j<TN;j++)for(uint e=0;e<8;e++)out[(first+t*16+gr()*8+e)*OUT+row+j*16+rc()]=F(H(total[t][j][e]));}
}
WAVE void vit_stream_project_n64_b(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){vit_stream_project_tiles<1,4,true,false>(in,w,skip,out,tokens,reuse_gate);}
WAVE void vit_stream_project_n64_h(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){vit_stream_project_tiles<1,4,false,true>(in,w,skip,out,tokens,reuse_gate);}
WAVE void vit_stream_project_n64_bh(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){vit_stream_project_tiles<1,4,true,true>(in,w,skip,out,tokens,reuse_gate);}
WAVE void vit_stream_qkv_frag_hin(const float*in,const float*w,unsigned char*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint perpart=(tokens/16)*32,part=bid()/perpart,local=bid()%perpart,first=local/32*16,row=local%32*32;
#if HIP_VIT_QKV_ORDER==1
 /* head-major inside a part: consecutive groups share the same 32 weight columns */
 {uint tiles=tokens/16;first=local%tiles*16;row=local/tiles*32;}
#elif HIP_VIT_QKV_ORDER==2
 /* part-interleaved: consecutive groups are Q/K/V of the same token tile and head */
 {part=bid()%3;local=bid()/3;first=local/32*16;row=local%32*32;if(bid()>=3*perpart)part=3;}
#elif HIP_VIT_QKV_ORDER==3
 /* 4 token tiles x 8 heads blocks: neighbours share either A rows or weight columns */
 {uint tiles=tokens/16,blk=local/32,in=local%32;if(tiles%4==0){uint tb=blk%(tiles/4),hb=blk/(tiles/4);first=(tb*4+in/8)*16;row=(hb*8+in%8)*32;}}
#endif
 if(part>=3)return;f8 acc[2]{};
 const _Float16*w16=reinterpret_cast<const _Float16*>(w)+part*1048576;
#if HIP_VIT_STREAM_QKV_UNROLL == 2
 _Pragma("unroll 2")
#endif
 for(uint k=0;k<1024;k+=16){h8 a{};__builtin_memcpy(&a,reinterpret_cast<const _Float16*>(in)+(first+rc())*1024+k+gr()*8,16);for(uint j=0;j<2;j++){h8 b{};__builtin_memcpy(&b,w16+(((row+j*16)/16)*64+k/16)*256+(gr()*16+rc())*8,16);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 __attribute__((shared)) float raw[16*33];
 f8 sum{};
 if(part<2){
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)raw[(gr()*8+e)*33+j*16+rc()]=acc[j][e];
  __builtin_amdgcn_s_barrier();
  for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){float v=raw[rc()*33+k+gr()*8+e];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);}
 }
#if HIP_VIT_HOIST_SCALE || HIP_VIT_STREAM_QKV_HOIST
 // The per-row scale read once (uniform); inside the loop it was re-read after every out[] store (aliasing), 16 global_load + wait pairs.
 const float scale=part==0?5.65625f*w[1572864+row/32]:1.f;
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(v);}
#else
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){float scale=part==0?5.65625f*w[1572864+row/32]:1.f;v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(v);}
#endif
}

#if HIP_VIT_QKV_W5
KERNEL __attribute__((amdgpu_flat_work_group_size(160,160))) void vit_stream_qkv_frag_hin_w5(const float*in,const float*w,unsigned char*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 constexpr uint CH=8;/* k16 steps per staged chunk: 2 column tiles x 8 fragments x 512B = 8KB, double buffered; the 4-register move below assumes 8 */
 __attribute__((shared)) _Float16 lds[2*2*CH*256];/* 16KB; after the K loop it holds the five per-wave 16x33 float norm tiles (10560B) */
 const uint t=__builtin_amdgcn_workitem_id_x(),wv=t>>5,lane=t&31,groups=(tokens/16+4)/5,perpart=groups*32,part=bid()/perpart,local=bid()%perpart,first=(local/32*5+wv)*16,row=local%32*32;if(part>=3)return;
 const bool live=first<tokens;
 const _Float16*hb=reinterpret_cast<const _Float16*>(w)+part*1048576+(row/16)*64*256;
 h8 s0{},s1{},s2{},s3{};/* threads t<128 (waves 0-3) move pieces t+i*128: ct=i>>1, kk=((t>>5)&3)+(i&1)*4, lane */
#define QKV_W5_SRC(i) (hb+((i)>>1)*16384+(kc*CH+((t>>5)&3)+((i)&1)*4)*256+lane*8)
#define QKV_W5_DST(i) (lds+sb*(2*CH*256)+((i)>>1)*(CH*256)+(((t>>5)&3)+((i)&1)*4)*256+lane*8)
#define QKV_W5_FETCH {if(t<128){__builtin_memcpy(&s0,QKV_W5_SRC(0),16);__builtin_memcpy(&s1,QKV_W5_SRC(1),16);__builtin_memcpy(&s2,QKV_W5_SRC(2),16);__builtin_memcpy(&s3,QKV_W5_SRC(3),16);}}
#define QKV_W5_STASH {if(t<128){__builtin_memcpy(QKV_W5_DST(0),&s0,16);__builtin_memcpy(QKV_W5_DST(1),&s1,16);__builtin_memcpy(QKV_W5_DST(2),&s2,16);__builtin_memcpy(QKV_W5_DST(3),&s3,16);}}
 f8 acc[2]{};
 {const uint kc=0;QKV_W5_FETCH}
#pragma unroll 1
 for(uint kc=0;kc<1024/(16*CH);kc++){{const uint sb=kc&1;QKV_W5_STASH}__builtin_amdgcn_s_barrier();if(kc+1<1024/(16*CH)){const uint kc1=kc+1;{const uint kc=kc1;QKV_W5_FETCH}}
  if(live)
#pragma unroll
  for(uint kk=0;kk<CH;kk++){uint k=(kc*CH+kk)*16;h8 a{};__builtin_memcpy(&a,reinterpret_cast<const _Float16*>(in)+(first+rc())*1024+k+gr()*8,16);
#pragma unroll
   for(uint j=0;j<2;j++){h8 b;__builtin_memcpy(&b,lds+(kc&1)*(2*CH*256)+j*(CH*256)+kk*256+lane*8,16);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}}
#undef QKV_W5_SRC
#undef QKV_W5_DST
#undef QKV_W5_FETCH
#undef QKV_W5_STASH
 __builtin_amdgcn_s_barrier();
 float*raw=reinterpret_cast<float*>(lds)+wv*(16*33);
 f8 sum{};
 if(part<2){
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)raw[(gr()*8+e)*33+j*16+rc()]=acc[j][e];
  __builtin_amdgcn_s_barrier();
  for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){float v=raw[rc()*33+k+gr()*8+e];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);}
 }
 if(!live)return;
 const float scale=part==0?5.65625f*w[1572864+row/32]:1.f;
#if VIT_QKV_TAIL_VEC
 // Byte tail through the wave's own (now dead) norm tile: same byte_F per element into a 16x32 row-major LDS tile, then
 // lane = (token lane>>1, half lane&1) stores 16 contiguous bytes (1 global b128 instead of 16 b8). Bytes unchanged.
 {unsigned char*tb=reinterpret_cast<unsigned char*>(raw);
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}tb[(gr()*8+e)*32+j*16+rc()]=byte_F(v);}
  WG_FENCE(3);__builtin_amdgcn_wave_barrier();WG_FENCE(2);
  uint r=lane>>1,h=lane&1u;int __attribute__((ext_vector_type(4))) q;__builtin_memcpy(&q,__builtin_assume_aligned(tb+r*32+h*16,16),16);
  __builtin_memcpy(__builtin_assume_aligned(out+size_t(part*tokens+first+r)*1024+row+h*16,16),&q,16);}
#else
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(v);}
#endif
}
#endif
// HIP_VIT_QKV_TM (2026-10-01, results/vit-1080-gap-20261001): mochizuki's gemmvqkvnorm gives one group a 64-token x
// 256-column block (grid 10x12 at 640 tokens, nearly flat in token count). Here one wave takes TM (2 or 4) 16-token
// tiles of one head: 2*TM independent accumulator chains, each weight fragment loaded once for all TM tiles. Per output
// the K loop (k=0..1008 step 16, same A/B fragments), the norm tile/row-sum WMMA, rsq/scale and byte_F store are those of
// vit_stream_qkv_frag_hin_w5 (non-tail-vec), so bytes are identical. grid = 96*ceil(tokens/(16*TM)), block 32.
#ifndef HIP_VIT_QKV_TM
#define HIP_VIT_QKV_TM 0
#endif
#if HIP_VIT_QKV_TM
template<uint TM>
DEV void vit_qkv_tm_body(const float*in,const float*w,unsigned char*out,uint tokens){
 const uint groups=(tokens/16+TM-1)/TM,perpart=groups*32,part=bid()/perpart,local=bid()%perpart,first=local/32*(16*TM),row=local%32*32;if(part>=3)return;
 const _Float16*w16=reinterpret_cast<const _Float16*>(w)+part*1048576;
 f8 acc[TM][2]{};
 for(uint k=0;k<1024;k+=16){h8 b[2];for(uint j=0;j<2;j++)__builtin_memcpy(&b[j],w16+(((row+j*16)/16)*64+k/16)*256+(gr()*16+rc())*8,16);
#pragma unroll
  for(uint t=0;t<TM;t++){if(first+t*16>=tokens)continue;h8 a{};__builtin_memcpy(&a,reinterpret_cast<const _Float16*>(in)+(first+t*16+rc())*1024+k+gr()*8,16);
   for(uint j=0;j<2;j++)acc[t][j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b[j],acc[t][j]);}}
 __attribute__((shared)) float raw[16*33];
 const float scale=part==0?5.65625f*w[1572864+row/32]:1.f;
#pragma unroll
 for(uint t=0;t<TM;t++){if(first+t*16>=tokens)continue;f8 sum{};
  if(part<2){
   WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
   for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)raw[(gr()*8+e)*33+j*16+rc()]=acc[t][j][e];
   WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
   for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){float v=raw[rc()*33+k+gr()*8+e];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);}
  }
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[t][j][e];if(part<2){v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+t*16+gr()*8+e)*1024+row+j*16+rc()]=byte_F(v);}
 }
}
#if HIP_VIT_QKV_TM==2
WAVE void vit_stream_qkv_frag_hin_tm2(const float*in,const float*w,unsigned char*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_qkv_tm_body<2>(in,w,out,tokens);}
#elif HIP_VIT_QKV_TM==4
WAVE void vit_stream_qkv_frag_hin_tm4(const float*in,const float*w,unsigned char*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_qkv_tm_body<4>(in,w,out,tokens);}
#else
#error HIP_VIT_QKV_TM must be 2 or 4
#endif
#endif
WAVE void vit_stream_contract_frag_hout(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(inputs==4096&&outputs==1024)vit_contract_blocked_body<false,false,false,true,true>(in,w,skip,out,tokens);}
#endif
