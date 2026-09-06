// Controlled single-color-texture preblock input mix. Lab oracle uses CTA0
// for every 8x8 tile. Live texture transforms/global seed contract not yet bound.
StructuredBuffer<float> weights : register(t0);
StructuredBuffer<float> input : register(t1);
#if NATIVE_TEMPORAL_RGB
// Reconstructed RGB in full processing HWC order, produced on the GPU.
StructuredBuffer<float4> temporal_rgb : register(t3);
#endif
#if NATIVE_NOISE_TABLE
// Universal function values for all 24-bit uniform inputs, not image features.
StructuredBuffer<float> noise_table : register(t2);
uint uniform_index(uint s){uint w=((s>>((s>>28)+4))^s)*0x108ef2d9;return (w>>30)^(w>>8);}
#endif
RWStructuredBuffer<float> output : register(u0);
#if DYNAMIC_PARAMETERS
cbuffer RuntimeParameters:register(b0){uint runtime_seed;uint runtime_width;uint runtime_height;uint local_oracle;uint runtime_temporal_enabled;}
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
#if NATIVE_NOISE_TABLE
// Direct double -> half RNE. Casting to float first can double-round at ties.
float half_round_exact(double value){
 bool negative=value<0.0;double magnitude=negative?-value:value;
 if(magnitude>=65520.0)return asfloat(negative?0xff800000u:0x7f800000u);
 float approx=(float)magnitude;
 int exponent=max(int((asuint(approx)>>23)&255u)-127,-14)-10;
 float step=exp2(float(exponent));double scaled=magnitude*(double)exp2(float(-exponent));
 uint lower=(uint)(float)scaled;if((double)lower>scaled)lower--;
 double remainder=scaled-(double)lower;
 if(remainder>0.5||(remainder==0.5&&(lower&1u)))lower++;
 return (negative?-1.0:1.0)*float(lower)*step;
}
#endif
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=TOTAL_OUTPUTS/32)return;
 float prefix[32];
#if RAW_INPUT
 [loop]for(uint raw_channel=0;raw_channel<32;raw_channel++)prefix[raw_channel]=input[p*32+raw_channel];
#else
 uint x=p%8,y=(p/8)%8;
 uint seed=NOISE_SEED;
#if DYNAMIC_PARAMETERS
 seed=runtime_seed;
 if(!local_oracle){uint tile=p/64;x=(tile%(runtime_width/8))*8+p%8;y=(tile/(runtime_width/8))*8+(p%64)/8;}
#endif
 uint h=pcg((x*0x8da6b343)^(y*0xd8163841)^(seed*0x9e3779b9u)^0x243f6a88u);
#if NATIVE_NOISE_TABLE
 uint ia=uniform_index(h*0xcaa5b80d+0x21dd796b),ib=uniform_index(h*0x2c9277b5+0xac564b05);
 uint ic=uniform_index(h*0x83232c31+0x3463e0ac),idn=uniform_index(h*0xfa6dc5f9+0x4712a88e);
 precise float ng0=noise_table[ia*3]*noise_table[ic*3+1];
 precise float ng1=noise_table[ib*3]*noise_table[idn*3+1];
 precise float ng2=noise_table[ib*3]*noise_table[idn*3+2];
 float g0=half_round(ng0),g1=half_round(ng1),g2=half_round(ng2);
#else
 float a=uniform24(h*0xcaa5b80d+0x21dd796b),b=uniform24(h*0x2c9277b5+0xac564b05);
 float c=uniform24(h*0x83232c31+0x3463e0ac),d=uniform24(h*0xfa6dc5f9+0x4712a88e);
 float r0=sqrt(-2*log(a)),r1=sqrt(-2*log(b));
 float g0=half_round(r0*cos(6.283185482025146*c));
 float g1=half_round(r1*cos(6.283185482025146*d));
 float g2=half_round(r1*sin(6.283185482025146*d));
#endif
 float rgb_scale=LIVE_PROFILE?.125:2;
 float r=half_round(half_round(half_round(input[p*4])-0.5)*rgb_scale);
 float g=half_round(half_round(half_round(input[p*4+1])-0.5)*rgb_scale);
 float bl=half_round(half_round(half_round(input[p*4+2])-0.5)*rgb_scale);
 float features[16]={g1,g2,g,bl,g0,1,LIVE_PROFILE?.0078125:0,1,r,g,LIVE_PROFILE?1:0,LIVE_PROFILE?1:0,bl,r,LIVE_PROFILE?1:0,0};
#if NATIVE_TEMPORAL_RGB
 if(runtime_temporal_enabled){
  uint tile=p/64;
  uint tx=(tile%(runtime_width/8))*8+p%8;
  uint ty=(tile/(runtime_width/8))*8+(p%64)/8;
  float3 history=temporal_rgb[ty*runtime_width+tx].xyz;
  features[13]=half_round(half_round(half_round(history.x)-.5)*.125);
  features[2]=half_round(half_round(half_round(history.y)-.5)*.125);
  features[3]=half_round(half_round(half_round(history.z)-.5)*.125);
 }
#endif
#if DEBUG_FEATURES
 [unroll]for(uint debug_channel=0;debug_channel<16;debug_channel++)output[p*32+debug_channel]=features[debug_channel];
 [unroll]for(uint zero_channel=16;zero_channel<32;zero_channel++)output[p*32+zero_channel]=0;
 return;
#endif
 [loop]for(uint channel=0;channel<32;channel++){
#if NATIVE_NOISE_TABLE
  // Measured HMMA: align by operand exponent sums, truncate to 27 bits,
  // then add exactly. Sixteen signed terms fit the 32-bit accumulator.
  float products[16];int maximum_exponent=-1000;
  [unroll]for(uint i=0;i<16;i++){
   float a=features[i],b=weights[channel*16+i];products[i]=a*b;
   if(products[i]!=0){int ea=int((asuint(a)>>23)&255u)-126,eb=int((asuint(b)>>23)&255u)-126;maximum_exponent=max(maximum_exponent,ea+eb);}
  }
  if(maximum_exponent==-1000)prefix[channel]=0;
  else{
   float scale=asfloat(uint(27-maximum_exponent+127)<<23);int sum=0;
   [unroll]for(uint i=0;i<16;i++)sum+=(int)(products[i]*scale);
   // Split the integer conversion so no float32 cast loses accumulator bits.
   double exact_sum=(double)float(sum>>16)*65536.0+(double)float(asuint(sum)&65535u);
   float quantum=asfloat(uint(maximum_exponent-27+127)<<23);
   prefix[channel]=half_round_exact(exact_sum*(double)quantum);
  }
#else
  float sum=0;[unroll]for(uint i=0;i<16;i++)sum+=features[i]*weights[channel*16+i];
  prefix[channel]=half_round(sum);
#endif
 }
#endif
#if FULL_FFN
 float hidden[128];
 [loop]for(uint row=0;row<128;row++){
  float sum=0;[loop]for(uint i=0;i<32;i++)sum+=q8(prefix[i])*weights[512+row*32+i];
  float expanded=half_round(sum),gate=clamp(expanded,-4.0,4.0);
  float polynomial=half_round(gate*half_round(abs(gate)*(-.055908203125)+.447265625)+.89453125);
  hidden[row]=q8(half_round(expanded*polynomial));
 }
 [loop]for(uint c=0;c<32;c++){
  // Both preblock and ordinary C32 QMMA seed the accumulator with a
  // separately rounded residual before the four K32 matrix products.
  float accum=half_round(prefix[c]*weights[8704+c]);
  [unroll]for(uint group=0;group<4;group++){
   float sum=0;[loop]for(uint i=0;i<32;i++)sum+=hidden[group*32+i]*weights[4608+c*128+group*32+i];
   accum=half_round(accum+sum);
  }
  float result=accum;
  output[p*32+c]=RAW_OUTPUT?result:q8(result);
 }
#else
 [unroll]for(uint c=0;c<32;c++)output[p*32+c]=q8(prefix[c]);
#endif
}
