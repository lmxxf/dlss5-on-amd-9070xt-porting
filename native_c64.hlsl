StructuredBuffer<float> input:register(t0),weights:register(t1),feature:register(t2);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[numthreads(64,1,1)]void ffn(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;float hidden[256],middle[64];
 [loop]for(uint row=0;row<256;row++){
  float a=0;[unroll]for(uint g=0;g<2;g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[p*64+g*32+j]*weights[row*64+g*32+j];a=H(a+s);}
  float gate=clamp(a,-4.0,4.0),poly=H(gate*H(abs(gate)*(-.055908203125)+.447265625)+.89453125);hidden[row]=F(H(a*poly));
 }
 [loop]for(uint row=0;row<64;row++){
  float a=0;[loop]for(uint g=0;g<8;g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=hidden[g*32+j]*weights[16384+row*256+g*32+j];a=H(a+s);}middle[row]=F(a);
 }
 [loop]for(uint row=0;row<64;row++){
  float a=H(input[p*64+row]*weights[36864+row]);[unroll]for(uint g=0;g<2;g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=middle[g*32+j]*weights[32768+row*64+g*32+j];a=H(a+s);}output[p*64+row]=F(a);
 }
}
groupshared float queries[2048],keys[2048],values[2048];
[numthreads(64,1,1)]void attention(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint head=gid.y,t=tid.x;if(gid.x>=width*height/64||head>=2)return;
 uint p=((gid.x/(width/8))*8+t/8)*width+(gid.x%(width/8))*8+t%8;
 float q[32],k[32],qs[16],ks[16];
 [loop]for(uint c=0;c<32;c++){
  float a=0,b=0,z=0;uint row=head*32+c;
  [unroll]for(uint g=0;g<2;g++){
   float sa=0,sb=0,sz=0;[loop]for(uint j=0;j<32;j++){float v=input[p*64+g*32+j];sa+=v*weights[row*64+g*32+j];sb+=v*weights[4096+row*64+g*32+j];sz+=v*weights[8192+row*64+g*32+j];}
   a=H(a+sa);b=H(b+sb);z=H(z+sz);
  }
  q[c]=a;k[c]=b;values[t*32+c]=F(z);
 }
 [unroll]for(uint i=0;i<16;i++){qs[i]=H(q[i]*q[i]+H(q[i+16]*q[i+16]));ks[i]=H(k[i]*k[i]+H(k[i+16]*k[i+16]));}
 [unroll]for(uint i=0;i<8;i++){qs[i]=H(qs[i*2]+qs[i*2+1]);ks[i]=H(ks[i*2]+ks[i*2+1]);}
 [unroll]for(uint step=4;step>0;step/=2){[loop]for(uint i=0;i<step;i++){qs[i]=H(qs[i]+qs[i+step]);ks[i]=H(ks[i]+ks[i+step]);}}
 float qi=H(rsqrt(max(qs[0],6.198883056640625e-5))),ki=H(rsqrt(max(ks[0],6.198883056640625e-5)));
 [loop]for(uint c=0;c<32;c++){queries[t*32+c]=F(H(H(q[c]*qi)*H(weights[24576+head])));keys[t*32+c]=F(H(k[c]*ki));}
 GroupMemoryBarrierWithGroupSync();float ex[64],prob[64];
 [loop]for(uint key=0;key<64;key++){
  float s=0;[loop]for(uint c=0;c<32;c++)s+=queries[t*32+c]*keys[key*32+c];
  float score=H(s+weights[16384+head*4096+t*64+key]);uint bits=f32tof16(clamp(H(score*.044921875+1.30078125),1.03125,1.5693359375));ex[key]=f16tof32(((bits<<5)+0x8000u)&65535u);
 }
 float parity[2];[unroll]for(uint odd=0;odd<2;odd++){
  float total=0;[unroll]for(uint lane=0;lane<4;lane++){
   uint base=odd+(lane%2)*2+(lane/2)*8;float partial=H(ex[base]+ex[base+16]);
   partial=H(partial+H(ex[base+4]+ex[base+20]));partial=H(partial+H(ex[base+32]+ex[base+48]));partial=H(partial+H(ex[base+36]+ex[base+52]));total=lane==0?partial:H(total+partial);
  }parity[odd]=total;
 }
 float inv=H(1/H(parity[0]+parity[1]));[loop]for(uint key=0;key<64;key++)prob[key]=F(H(ex[key]*inv));
 [loop]for(uint c=0;c<32;c++){float a=0;[unroll]for(uint g=0;g<2;g++){float s=0;[loop]for(uint key=0;key<32;key++)s+=prob[g*32+key]*values[(g*32+key)*32+c];a=H(a+s);}output[p*64+head*32+c]=F(a);}
}
[numthreads(64,1,1)]void projection(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;
 [loop]for(uint row=0;row<64;row++){float a=H(feature[p*64+row]*weights[24578+row]);[unroll]for(uint g=0;g<2;g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[p*64+g*32+j]*weights[12288+row*64+g*32+j];a=H(a+s);}output[p*64+row]=F(a);}
}
