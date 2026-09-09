#ifndef POST_INPUT8
#define POST_INPUT8 0
#endif
#ifndef POST_INPUT_HALF
#define POST_INPUT_HALF 0
#endif
#if POST_INPUT8
// FAST PATH (DLSS5_POST70_OUT8): the body's output arrives as E4M3 bytes ([token][32]); decode exactly.
ByteAddressBuffer input8:register(t0);
float e4m3_to_float(uint b){uint e=(b>>3)&15u,m=b&7u;float v=e==0?float(m)/512.0:asfloat(((e+120u)<<23)|(m<<20));return (b&0x80u)?-v:v;}
StructuredBuffer<float> weights:register(t1),skip_or_color:register(t2);
StructuredBuffer<float> input:register(t3); // only so the unused merge/exact entries still compile; the host requires merge fold with out8
#define IN(i) input[i]
#elif POST_INPUT_HALF
// FAST PATH (DLSS5_C32_HALF_STREAM): the body's raw tiles are f16 pairs.
StructuredBuffer<uint> input_u:register(t0);StructuredBuffer<float> weights:register(t1),skip_or_color:register(t2);
float IN(uint i){uint w=input_u[i>>1];return f16tof32((i&1)?(w>>16):(w&0xffffu));}
StructuredBuffer<float> input:register(t3); // unused merge entry
#else
StructuredBuffer<float> input:register(t0),weights:register(t1),skip_or_color:register(t2);
#define IN(i) input[i]
#endif
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;float input_scale;uint work_width;uint shift_x;uint shift_y;}
#ifndef POST_TILED_INPUT
#define POST_TILED_INPUT 0
#endif
// FAST PATH (POST_TILED_INPUT): the rgb head reads the body's tile-major raw work buffer directly (same indexing as crop_raw).
uint feature_base(uint p){
#if POST_TILED_INPUT
 uint x=p%width+shift_x,y=p/width+shift_y;uint tile=(y/8)*(work_width/8)+x/8;return (tile*64+(y%8)*8+x%8)*32;
#else
 return p*32;
#endif
}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float aligned_half(int sum,float acc,float scale,int e){
 float scaled_acc=acc*scale;
 if(scaled_acc!=trunc(scaled_acc)||abs(scaled_acc)>=67108864.0)return asfloat(0x7fc00000u);
 bool negative=sum<0,aneg=acc<0;uint magnitude=negative?(0u-asuint(sum)):asuint(sum),other=(uint)abs(scaled_acc);
 if(negative==aneg)magnitude+=other;
 else if(magnitude>=other)magnitude-=other;
 else{magnitude=other-magnitude;negative=aneg;}
 if(magnitude==0)return 0;
 int quantum_exp=e-27,top=firstbithigh(magnitude),step_exp=max(top+quantum_exp,-14)-10,drop=step_exp-quantum_exp;
 uint rounded=magnitude;
 if(drop>32)rounded=0;
 else if(drop==32)rounded=magnitude>0x80000000u?1:0;
 else if(drop>0){rounded=magnitude>>drop;uint remainder=magnitude&((1u<<drop)-1u),half=1u<<(drop-1);if(remainder>half||(remainder==half&&(rounded&1u)))rounded++;}
 else step_exp=quantum_exp;
 float value=float(rounded)*exp2(float(step_exp));if(value>=65520.0)value=asfloat(0x7f800000u);
 return negative?-value:value;
}
[numthreads(64,1,1)]void merge(uint3 id:SV_DispatchThreadID){
 uint i=id.x+id.y*width*height,p=i/32,c=i%32;if(p>=width*height)return;
 uint low=((p/width)/2)*(width/2)+(p%width)/2;
 output[i]=H(H(input[low*32+c]*weights[c])+skip_or_color[i]*weights[32+c]);
}
[numthreads(64,1,1)]void finish(uint3 id:SV_DispatchThreadID){
 uint i=id.x+id.y*width*height,p=i/3,row=i%3;if(p>=width*height)return;float acc=0;const uint fb=feature_base(p);
#if POST_BASE_ONLY == 1
 output[i]=skip_or_color[p*4+row];return;
#endif
#if POST_BASE_ONLY != 2
 [unroll]for(uint part=0;part<2;part++){
  float products[16];int e=acc==0?-1000:int((asuint(acc)>>23)&255u)-125;
  [unroll]for(uint j=0;j<16;j++){float a=IN(fb+part*16+j),b=weights[row*32+part*16+j];products[j]=a*b;if(products[j]!=0)e=max(e,int((asuint(a)>>23)&255u)+int((asuint(b)>>23)&255u)-252);}
  if(e!=-1000){float scale=asfloat(uint(27-e+127)<<23);int sum=0;
   [unroll]for(uint j=0;j<16;j++)sum+=(int)(products[j]*scale);
   acc=aligned_half(sum,acc,scale,e);
  }
 }
#endif
 // Explicit float32 boundaries, matching the original encode/add/decode order.
 precise float base=skip_or_color[p*4+row]*0.125-0.0625;
 precise float encoded=acc*input_scale+base;
 precise float rgb=encoded*8.0+0.5;
 output[i]=clamp(rgb,0.0,1.0);
}
#ifndef POST_FAST_RGB
#define POST_FAST_RGB 0
#endif
#if POST_FAST_RGB
// FAST PATH (DLSS5_POST70_FAST_RGB): one thread per pixel, the 32 features read once for all three rows, plain f32 dot
// products with a single f16 rounding (the exact head emulates the Tensor Core integer alignment per 16-term half).
[numthreads(64,1,1)]void finish_fast(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;const uint fb=feature_base(p);
#if POST_INPUT8
 float f[32];[unroll]for(uint q=0;q<8;q++){uint w=input8.Load(fb+q*4);[unroll]for(uint k=0;k<4;k++)f[q*4+k]=e4m3_to_float((w>>(k*8))&255u);}
#else
 float f[32];[unroll]for(uint j=0;j<32;j++)f[j]=IN(fb+j);
#endif
 [unroll]for(uint row=0;row<3;row++){
  float acc=0;[unroll]for(uint j=0;j<32;j++)acc+=f[j]*weights[row*32+j];
  acc=f16tof32(f32tof16(acc));
  float base=skip_or_color[p*4+row]*0.125-0.0625;
  output[p*3+row]=clamp((acc*input_scale+base)*8.0+0.5,0.0,1.0);
 }
}
#endif
