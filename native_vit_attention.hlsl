StructuredBuffer<float> input:register(t0);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint tokens;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
uint key_index(uint c){return ((c&16)>>4)|((c&1)<<1)|((c&2)<<1)|(c&8)|((c&4)<<2)|(c&32);}
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
 if(id.x>=tokens*32)return;uint query=id.x/32,head=id.x%32;float ex[64],quantized[64];
 [loop]for(uint key=0;key<64;key++){
  float score=0;[loop]for(uint c=0;c<32;c++)score+=input[query*1024+head*32+c]*input[(tokens+key)*1024+head*32+c];
  float affine=clamp(H(H(score)*f16tof32(0x2dbb)+1.708984375),1.439453125,1.9775390625);
  uint b=f32tof16(affine);ex[key]=f16tof32(((b<<4)+0x4000)&65535);quantized[key]=F(ex[key]);
 }
 float sums[2];
 [unroll]for(uint parity=0;parity<2;parity++){
  float accumulated=0;
  [unroll]for(uint group=0;group<4;group++){
   uint base=parity+(group&1)*2+(group>>1)*8;
   float a=H(ex[key_index(base)]+ex[key_index(base+16)]);
   a=H(a+H(ex[key_index(base+4)]+ex[key_index(base+20)]));
   a=H(a+H(ex[key_index(base+32)]+ex[key_index(base+48)]));
   a=H(a+H(ex[key_index(base+36)]+ex[key_index(base+52)]));
   accumulated=group?H(accumulated+a):a;
  }
  sums[parity]=accumulated;
 }
 float inverse=H(1.0/H(sums[0]+sums[1]));
 [loop]for(uint c=0;c<32;c++){
  float accumulated=0;[unroll]for(uint part=0;part<2;part++){float a=0;[loop]for(uint j=0;j<32;j++){uint key=part*32+j;a+=quantized[key]*input[(2*tokens+key)*1024+head*32+c];}accumulated=H(accumulated+a);}
  output[query*1024+head*32+c]=F(H(accumulated*inverse));
 }
}
