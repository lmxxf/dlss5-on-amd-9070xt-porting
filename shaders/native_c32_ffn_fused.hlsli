/* FAST PATH (DLSS5_C32_FUSED_FFN): the C32 FFN (native_wave_c32_ffn_blocked.hlsl, fast3 + FP8 + mapped input, non-tiled
   weights) runs in the attention fast4 prologue on the wave's own 16 tokens. Its output feeds the QKV cast directly and stays
   in registers as the projection residual; the ffn scratch buffer is neither written nor read. Same operations in the same
   order (expand blocks are independent; the contract K steps keep the order 0..3), so the result is bit for bit the same.
   Wave-private LDS: ex slice = staged f16 input tile (all mapped sources are H()-rounded, so f16 is exact) / mode 5 features;
   pw8 slice = E4M3 input tile (prefix8); the wave's own qkv8 rows = the hidden layer, two halves of 64 channels (the QKV rows
   are written only after the FFN). Supported map modes: 1 raster f32, 2 previous Main(), 3 previous raw tiles f16,
   5 pre-block inline prefix, 9 post70 merge fold (main8 low + main8 skip). */
ByteAddressBuffer ffn_weights:register(t4);  /* packed FFN weights of native_preblock_runtime (expand E4M3 @20608, contract E4M3 @16512, scales @16384, diagonals @24704, prefix B tiles @27776) */
ByteAddressBuffer ffn_input:register(t5);    /* mapped source: mode 1/2 f32 raster or Main(), 3 f16 raw tiles, 5 rgb f32 [pixel][4], 9 low main8 bytes */
ByteAddressBuffer ffn_skip:register(t6);     /* mode 9: skip main8 bytes (raster over the pre-block work grid) */
ByteAddressBuffer ffn_coeff:register(t7);    /* mode 9: 64 merge coefficients; mode 5: temporal history, float4 per raster pixel */
#ifndef NATIVE_C32_MERGE4
#define NATIVE_C32_MERGE4 0
#endif
uint ffn_pcg(uint s){uint w=((s>>((s>>28)+4))^s)*0x108ef2d9;return (w>>22)^w;}
float ffn_uniform24(uint s){uint w=((s>>((s>>28)+4))^s)*0x108ef2d9;return float(((w>>30)^(w>>8))+1)*5.9604644775390625e-8;}
float ffn_half_round(float v){return f16tof32(f32tof16(v));}
float ffn_e4m3(uint b){uint e=(b>>3)&15u,m=b&7u;float v=e==0?float(m)/512.0:asfloat(((e+120u)<<23)|(m<<20));return (b&0x80u)?-v:v;}
float ffn_activate(float v){float g=clamp(v,-4.0,4.0);precise float q=abs(g)*(-.055908203125)+.447265625;precise float p=g*q+.89453125;return v*p;} /* precise: FMA contraction here is context dependent (fused in one build, not in another); the standalone kernel is built with NATIVE_C32_PRECISE_CHAIN for the same reason */
int ffn_source_index(uint p){
 if(map_mode==0)return int(p*32);
 uint tile=p/64,x=(tile%(runtime_width/8))*8+p%8,y=(tile/(runtime_width/8))*8+(p%64)/8;
 int sx=int(x)-int(shift_x),sy=int(y)-int(shift_y);
 if(sx<0||sy<0||sx>=int(src_width)||sy>=int(src_height))return -1;
 if(map_mode==1||map_mode==9)return int((uint(sy)*src_width+uint(sx))*32);
 uint px=uint(sx)+prev_shift_x,py=uint(sy)+prev_shift_y;
 if(map_mode==2)return int((py*prev_work_width+px)*32);
 uint ptile=(py/8)*(prev_work_width/8)+px/8;return int((ptile*64+(py%8)*8+px%8)*32);
}
int ffn_low_index(int src){
 if(src<0)return -1;uint p=uint(src)/32,lx=(p%src_width)/2,ly=(p/src_width)/2;
 return int(((ly+prev_shift_y)*prev_work_width+lx+prev_shift_x)*32);
}
/* first = the wave's first token, t = lane, ebase/pbase = the wave's ex/pw8 slices, hbase = the wave's qkv8 rows (uint index).
   Leaves the E4M3 input tile in pw8[pbase..+128]; out0/out1 = FFN output channels 0-15 / 16-31 (f16-rounded f32). */
void ffn_fused(uint first,uint t,uint ebase,uint pbase,uint hbase,out C out0,out C out1){
 C in0,in1;
 const int mine=t<16?ffn_source_index(first+t):0;
 [branch]if(map_mode==5){
  if(t<16){
   const uint p=first+t;
   uint x=p%8,y=(p/8)%8;
   if(!local_oracle){uint tile=p/64;x=(tile%(runtime_width/8))*8+p%8;y=(tile/(runtime_width/8))*8+(p%64)/8;}
   uint h=ffn_pcg((x*0x8da6b343)^(y*0xd8163841)^(runtime_seed*0x9e3779b9u)^0x243f6a88u);
   float a=ffn_uniform24(h*0xcaa5b80d+0x21dd796b),b=ffn_uniform24(h*0x2c9277b5+0xac564b05);
   float c=ffn_uniform24(h*0x83232c31+0x3463e0ac),d=ffn_uniform24(h*0xfa6dc5f9+0x4712a88e);
   float r0=sqrt(-2*log(a)),r1=sqrt(-2*log(b));
   float g0=ffn_half_round(r0*cos(6.283185482025146*c)),g1=ffn_half_round(r1*cos(6.283185482025146*d)),g2=ffn_half_round(r1*sin(6.283185482025146*d));
   float r=ffn_half_round(ffn_half_round(ffn_half_round(asfloat(ffn_input.Load(p*16)))-0.5)*.125),g=ffn_half_round(ffn_half_round(ffn_half_round(asfloat(ffn_input.Load(p*16+4)))-0.5)*.125),bl=ffn_half_round(ffn_half_round(ffn_half_round(asfloat(ffn_input.Load(p*16+8)))-0.5)*.125);
   float features[16]={g1,g2,g,bl,g0,1,.0078125,1,r,g,1,1,bl,r,1,0};
   if(temporal_enabled){uint tile=p/64;uint tx=(tile%(runtime_width/8))*8+p%8,ty=(tile/(runtime_width/8))*8+(p%64)/8;float3 hist=asfloat(ffn_coeff.Load3((ty*runtime_width+tx)*16));
    features[13]=ffn_half_round(ffn_half_round(ffn_half_round(hist.x)-.5)*.125);features[2]=ffn_half_round(ffn_half_round(ffn_half_round(hist.y)-.5)*.125);features[3]=ffn_half_round(ffn_half_round(ffn_half_round(hist.z)-.5)*.125);}
   [unroll]for(uint i=0;i<16;i++)ex[ebase+t*32+i]=float16_t(features[i]);
   [unroll]for(uint i=16;i<32;i++)ex[ebase+t*32+i]=float16_t(0.0);
  }
  GroupMemoryBarrier();
  A fa=A::Load(ex,ebase,32,dx::linalg::MatrixLayout::RowMajor);
  B b0=B::Load(ffn_weights,27776,32,dx::linalg::MatrixLayout::RowMajor,16),b1=B::Load(ffn_weights,27776+1024,32,dx::linalg::MatrixLayout::RowMajor,16);
  in0=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(fa,b0);in1=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(fa,b1);
  for(uint i=0;i<in0.Length();i++){in0.Set(i,ffn_half_round(in0.Get(i)));in1.Set(i,ffn_half_round(in1.Get(i)));}
 }else{
  [branch]if(map_mode==9){
#if NATIVE_C32_MERGE4
   /* FAST PATH (DLSS5_C32_MERGE4): one 4-byte load per source gives 4 channels; lane t handles token it*4+t/8, channels (t%8)*4..+3 (4 passes instead of 16; same per-element arithmetic). */
   const uint c0=(t%8)*4;const float4 w0=asfloat(ffn_coeff.Load4(c0*4)),w1=asfloat(ffn_coeff.Load4((32+c0)*4));const int mine_low=t<16?ffn_low_index(mine):0;
   [unroll]for(uint it=0;it<4;it++){const uint j=it*4+t/8;int src=WaveReadLaneAt(mine,j);float4 v=0;
    if(src>=0){uint sk4=ffn_skip.Load(uint(src)+c0),lo4=ffn_input.Load(uint(WaveReadLaneAt(mine_low,j))+c0);
     [unroll]for(uint q=0;q<4;q++){float sk=ffn_e4m3((sk4>>(q*8))&255u),lo=ffn_e4m3((lo4>>(q*8))&255u);precise float m=f16tof32(f32tof16(lo*w0[q]))+sk*w1[q];v[q]=f16tof32(f32tof16(m));}}
    ex[ebase+j*32+c0]=float16_t(v.x);ex[ebase+j*32+c0+1]=float16_t(v.y);ex[ebase+j*32+c0+2]=float16_t(v.z);ex[ebase+j*32+c0+3]=float16_t(v.w);}
#else
   const float w0=asfloat(ffn_coeff.Load(t*4)),w1=asfloat(ffn_coeff.Load((32+t)*4));const int mine_low=t<16?ffn_low_index(mine):0;
   [unroll]for(uint j=0;j<16;j++){int src=WaveReadLaneAt(mine,j);float v=0;
    if(src>=0){uint low=uint(WaveReadLaneAt(mine_low,j))+t;uint a=uint(src)+t;float sk=ffn_e4m3((ffn_skip.Load(a&~3u)>>((a&3u)*8))&255u);float lo=ffn_e4m3((ffn_input.Load(low&~3u)>>((low&3u)*8))&255u);precise float m=f16tof32(f32tof16(lo*w0))+sk*w1;v=f16tof32(f32tof16(m));}
    ex[ebase+j*32+t]=float16_t(v);}
#endif
  }else{
   [unroll]for(uint j=0;j<16;j++){int src=WaveReadLaneAt(mine,j);float v=src<0?0:(map_mode==3?float(ffn_input.Load<float16_t>((uint(src)+t)*2)):asfloat(ffn_input.Load((uint(src)+t)*4)));ex[ebase+j*32+t]=float16_t(v);}
  }
  GroupMemoryBarrier();
  in0=C16::Load(ex,ebase,32,dx::linalg::MatrixLayout::RowMajor).Cast<dx::linalg::ComponentType::F32>();in1=C16::Load(ex,ebase+16,32,dx::linalg::MatrixLayout::RowMajor).Cast<dx::linalg::ComponentType::F32>();
 }
 in0.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(pw8,pbase,8,dx::linalg::MatrixLayout::RowMajor);
 in1.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(pw8,pbase+4,8,dx::linalg::MatrixLayout::RowMajor);
 GroupMemoryBarrier();
 A8 a=A8::Load(pw8,pbase,8,dx::linalg::MatrixLayout::RowMajor);
 C acc[2];
 [unroll]for(uint half=0;half<2;half++){
  [unroll]for(uint block=half*4;block<half*4+4;block++){
   B8 b=B8::Load(ffn_weights,20608+block*16*32,32,dx::linalg::MatrixLayout::ColMajor,16);
   C h=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<h.Length();i++)h.Set(i,ffn_activate(h.Get(i)));
   h.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(qkv8,hbase+(block%4)*4,16,dx::linalg::MatrixLayout::RowMajor);
  }
  GroupMemoryBarrier();
  if(half==0){
   [branch]if(map_mode==3){
    acc[0]=C::Splat(0.0f);acc[1]=C::Splat(0.0f);
    [unroll]for(uint block=0;block<2;block++)[unroll]for(uint part=0;part<3;part++){B8 d=B8::Load(ffn_weights,24704+(block*3+part)*512,16,dx::linalg::MatrixLayout::RowMajor,16);acc[block].MultiplyAccumulate(a,d);}
   }else{
    for(uint i=0;i<acc[0].Length();i++){uint2 rc=acc[0].GetCoordinate(i);acc[0].Set(i,in0.Get(i)*asfloat(ffn_weights.Load(16384+rc.y*4)));acc[1].Set(i,in1.Get(i)*asfloat(ffn_weights.Load(16384+64+rc.y*4)));}
   }
  }
  [unroll]for(uint g=half*2;g<half*2+2;g++){
   A8 ah=A8::Load(qkv8,hbase+(g%2)*8,16,dx::linalg::MatrixLayout::RowMajor);
   [unroll]for(uint block=0;block<2;block++){B8 b=B8::Load(ffn_weights,16512+block*16*128+g*32,128,dx::linalg::MatrixLayout::ColMajor,16);acc[block].MultiplyAccumulate(ah,b);}
  }
  GroupMemoryBarrier();
 }
 for(uint i=0;i<acc[0].Length();i++){acc[0].Set(i,f16tof32(f32tof16(acc[0].Get(i))));acc[1].Set(i,f16tof32(f32tof16(acc[1].Get(i))));}
 out0=acc[0];out1=acc[1];
}
