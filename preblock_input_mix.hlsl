// Controlled single-color-texture preblock input mix. Lab oracle uses CTA0
// for every 8x8 tile. Live texture transforms/global seed contract not yet bound.
StructuredBuffer<float> weights : register(t0);
StructuredBuffer<float> input : register(t1);
RWStructuredBuffer<float> output : register(u0);
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
 uint h=pcg((x*0x8da6b343)^(y*0xd8163841)^(0x3f800000u*0x9e3779b9u)^0x243f6a88u);
 float a=uniform24(h*0xcaa5b80d+0x21dd796b),b=uniform24(h*0x2c9277b5+0xac564b05);
 float c=uniform24(h*0x83232c31+0x3463e0ac),d=uniform24(h*0xfa6dc5f9+0x4712a88e);
 float r0=sqrt(-2*log(a)),r1=sqrt(-2*log(b));
 float g0=half_round(r0*cos(6.283185482025146*c));
 float g1=half_round(r1*cos(6.283185482025146*d));
 float g2=half_round(r1*sin(6.283185482025146*d));
 float r=half_round(half_round(half_round(input[p*4])-0.5)*2);
 float g=half_round(half_round(half_round(input[p*4+1])-0.5)*2);
 float bl=half_round(half_round(half_round(input[p*4+2])-0.5)*2);
 float features[16]={g1,g2,g,bl,g0,1,0,1,r,g,0,0,bl,r,0,0};
#if DEBUG_FEATURES
 [unroll]for(uint debug_channel=0;debug_channel<16;debug_channel++)output[p*32+debug_channel]=features[debug_channel];
 [unroll]for(uint zero_channel=16;zero_channel<32;zero_channel++)output[p*32+zero_channel]=0;
 return;
#endif
 [loop]for(uint channel=0;channel<32;channel++){
  float sum=0;[unroll]for(uint i=0;i<16;i++)sum+=features[i]*weights[channel*16+i];
  output[p*32+channel]=q8(half_round(sum));
 }
}
