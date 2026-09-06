StructuredBuffer<float> input:register(t0),weights:register(t1),residual:register(t2);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint tokens;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
 uint token=id.x/OUTPUT_CHANNELS,row=id.x%OUTPUT_CHANNELS;if(token>=tokens)return;
#if DECODER_ENTRY
 float total=0;
 const uint partitions=INPUT_CHANNELS==1024?4:1;
 [loop]for(uint part=0;part<partitions;part++){
  float a=0;
  [loop]for(uint k=part*(INPUT_CHANNELS/partitions);k<(part+1)*(INPUT_CHANNELS/partitions);k+=32){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[token*INPUT_CHANNELS+k+j]*weights[row*INPUT_CHANNELS+k+j];a=H(a+s);}
  total=part==0?a:H(total+a);
 }
 // Explicit square main extents through128 (block66); contracts checked by host.
 uint width=tokens==16384?128:tokens==4096?64:tokens==1024?32:tokens==256?16:8;
 [unroll]for(uint dy=0;dy<2;dy++)[unroll]for(uint dx=0;dx<2;dx++){
  uint index=((token/width*2+dy)*(width*2)+token%width*2+dx)*OUTPUT_CHANNELS+row;
  float merged=H(total+F(residual[index])*weights[INPUT_CHANNELS*OUTPUT_CHANNELS+row]);
  output[index]=OUTPUT_CHANNELS==32?merged:F(merged);
 }
#elif EXPAND
 float a=0;
 [loop]for(uint k=0;k<INPUT_CHANNELS;k+=32){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[token*INPUT_CHANNELS+k+j]*weights[row*INPUT_CHANNELS+k+j];a=H(a+s);}
 float gate=clamp(a,-4.0,4.0),poly=H(gate*H(abs(gate)*(-.055908203125)+.447265625)+.89453125);
 output[id.x]=F(H(a*poly));
#else
 float total=0;
 [loop]for(uint part=0;part<4;part++){
  float a=part==0?H(residual[token*OUTPUT_CHANNELS+row]*weights[INPUT_CHANNELS*OUTPUT_CHANNELS+row]):0;
  [loop]for(uint k=part*(INPUT_CHANNELS/4);k<(part+1)*(INPUT_CHANNELS/4);k+=32){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[token*INPUT_CHANNELS+k+j]*weights[row*INPUT_CHANNELS+k+j];a=H(a+s);}
  total=part==0?a:H(total+a);
 }
 output[id.x]=F(total);
#endif
}
