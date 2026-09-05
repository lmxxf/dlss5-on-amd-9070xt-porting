// Controlled single-color-texture preblock input mix. Lab oracle uses CTA0
// for every 8x8 tile. Live texture transforms/global seed contract not yet bound.
StructuredBuffer<float> weights : register(t0);
StructuredBuffer<float> input : register(t1);
RWStructuredBuffer<float> output : register(u0);
#if DYNAMIC_PARAMETERS
cbuffer RuntimeParameters:register(b0){uint runtime_seed;uint runtime_width;uint runtime_height;uint local_oracle;}
#endif
uint pcg(uint s){uint w=((s>>((s>>28)+4))^s)*0x108ef2d9;return (w>>22)^w;}
float uniform24(uint s){uint w=((s>>((s>>28)+4))^s)*0x108ef2d9;return float(((w>>30)^(w>>8))+1)*5.9604644775390625e-8;}
// Explicit IEEE nearest-even: SM5 f32tof16 may truncate toward zero.
float half_round(float v){
 uint bits=asuint(v),sg=bits&0x80000000u,a=bits&0x7fffffffu;
 if(a>=0x7f800000u)return v;
 if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return (sg!=0)?-q:q;}
 uint rounded=(a+0xfffu+((a>>13)&1u))&0xffffe000u;
 if(rounded>=0x47800000u)return asfloat(sg|0x7f800000u);
 return asfloat(sg|rounded);
}
float q8(float v){float a=abs(v),sg=v<0?-1:1;if(a<0.015625)return sg*round(a*512)/512;float e=floor(log2(a));float m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=TOTAL_OUTPUTS/32)return;
 uint x=p%8,y=(p/8)%8;
 uint seed=NOISE_SEED;
#if DYNAMIC_PARAMETERS
 seed=runtime_seed;
 if(!local_oracle){uint tile=p/64;x=(tile%(runtime_width/8))*8+p%8;y=(tile/(runtime_width/8))*8+(p%64)/8;}
#endif
 uint h=pcg((x*0x8da6b343)^(y*0xd8163841)^(seed*0x9e3779b9u)^0x243f6a88u);
 float a=uniform24(h*0xcaa5b80d+0x21dd796b),b=uniform24(h*0x2c9277b5+0xac564b05);
 float c=uniform24(h*0x83232c31+0x3463e0ac),d=uniform24(h*0xfa6dc5f9+0x4712a88e);
 float r0=sqrt(-2*log(a)),r1=sqrt(-2*log(b));
 float g0=half_round(r0*cos(6.283185482025146*c));
 float g1=half_round(r1*cos(6.283185482025146*d));
 float g2=half_round(r1*sin(6.283185482025146*d));
 float rgb_scale=LIVE_PROFILE?.125:2;
 float r=half_round(half_round(half_round(input[p*4])-0.5)*rgb_scale);
 float g=half_round(half_round(half_round(input[p*4+1])-0.5)*rgb_scale);
 float bl=half_round(half_round(half_round(input[p*4+2])-0.5)*rgb_scale);
 float features[16]={g1,g2,g,bl,g0,1,LIVE_PROFILE?.0078125:0,1,r,g,LIVE_PROFILE?1:0,LIVE_PROFILE?1:0,bl,r,LIVE_PROFILE?1:0,0};
#if DEBUG_FEATURES
 [unroll]for(uint debug_channel=0;debug_channel<16;debug_channel++)output[p*32+debug_channel]=features[debug_channel];
 [unroll]for(uint zero_channel=16;zero_channel<32;zero_channel++)output[p*32+zero_channel]=0;
 return;
#endif
 float prefix[32];
 [loop]for(uint channel=0;channel<32;channel++){
  float sum=0;[unroll]for(uint i=0;i<16;i++)sum+=features[i]*weights[channel*16+i];
  prefix[channel]=half_round(sum);
 }
#if FULL_FFN
 float hidden[128];
 [loop]for(uint row=0;row<128;row++){
  float sum=0;[loop]for(uint i=0;i<32;i++)sum+=q8(prefix[i])*weights[512+row*32+i];
  float expanded=half_round(sum),gate=clamp(expanded,-4.0,4.0);
  float polynomial=half_round(gate*half_round(abs(gate)*(-.055908203125)+.447265625)+.89453125);
  hidden[row]=q8(half_round(expanded*polynomial));
 }
 [loop]for(uint c=0;c<32;c++){
  float accum=0;
  [unroll]for(uint group=0;group<4;group++){
   float sum=0;[loop]for(uint i=0;i<32;i++)sum+=hidden[group*32+i]*weights[4608+c*128+group*32+i];
   accum=half_round(accum+sum);
  }
  float result=half_round(accum+prefix[c]*weights[8704+c]);
  output[p*32+c]=RAW_OUTPUT?result:q8(result);
 }
#else
 [unroll]for(uint c=0;c<32;c++)output[p*32+c]=q8(prefix[c]);
#endif
}
