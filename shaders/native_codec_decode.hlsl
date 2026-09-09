// Mode1 candidate. Oracle: captured codec-22724-36e36d370.dxbc.
// Host must reject other modes; GPU comparison required before integration.
Texture2D<float4> Proxy : register(t1);
Texture2D<float4> Neural : register(t2);
Texture2D<float4> OutputOriginal : register(t3);
RWTexture2D<float4> Output : register(u0);
cbuffer CodecConstants : register(b0) {
 uint2 Size; uint2 SourceSize; uint2 SourceBase; uint2 ProxySize;
 float PaperWhiteScale; float TransferStrength; float ColorStrength; uint HdrMode;
 float4 Padding;
};
float Luminance(float3 c) { return dot(c,float3(0.212639,0.715169,0.072192)); }
float3 Decode(float3 c) {
 c=saturate(c);
 return c<=0.04045 ? c/12.92 : pow((c+0.055)/1.055,2.4);
}
float3 ToLab(float3 c) {
 const float3x3 a={0.4122214708,0.5363325363,0.0514459929,
  0.2119034982,0.6806995451,0.1073969566,0.0883024619,0.2817188376,0.6299787005};
 const float3x3 b={0.2104542553,0.7936177850,-0.0040720468,
  1.9779984951,-2.4285922050,0.4505937099,0.0259040371,0.7827717662,-0.8086757660};
 float3 l=mul(a,c);return mul(b,sign(l)*pow(abs(l),1.0/3.0));
}
float3 FromLab(float3 c) {
 const float3x3 a={1,0.3963377774,0.2158037573,1,-0.1055613458,-0.0638541728,1,-0.0894841775,-1.2914855480};
 const float3x3 b={4.0767416621,-3.3077115913,0.2309699292,
  -1.2684380046,2.6097574011,-0.3413193965,-0.0041960863,-0.7034186147,1.7076147010};
 float3 l=mul(a,c);return mul(b,l*l*l);
}
float3 ClampAp1(float3 c) {
 const float3x3 a={0.613097,0.339523,0.047379,0.070194,0.916354,0.013452,0.020616,0.109570,0.869815};
 const float3x3 b={1.705051,-0.621792,-0.083259,-0.130256,1.140805,-0.010548,-0.024003,-0.128969,1.152972};
 return mul(b,max(0,mul(a,c)));
}
float3 Hue(float3 incorrect,float3 correct) {
 float3 a=ToLab(incorrect),b=ToLab(correct);
 float ca=length(a.yz),cb=length(b.yz);
 a.yz=b.yz*(cb==0?1:ca/cb);
 return ClampAp1(FromLab(a));
}
float3 Upgrade(float3 original,float3 proxy,float3 neural) {
 float oy=Luminance(original),py=Luminance(proxy),ny=Luminance(neural);
 float3 result=original;
 if(!(ny<=1e-5)) {
  float ratio=0;
  if(oy<py)ratio=oy/max(py,1e-6);
  else ratio=(ny+max(0,oy-py))/ny;
  result=lerp(original,Hue(neural*ratio,neural),TransferStrength);
 }
 return result;
}
[numthreads(16,16,1)]
void main(uint3 id:SV_DispatchThreadID) {
 if(any(id.xy>=Size))return;
 if(HdrMode!=1||PaperWhiteScale<=0){Output[id.xy]=0;return;}
 uint2 extent=max(ProxySize,uint2(1,1));
 uint2 p=min(uint2((float2(id.xy)+0.5)*float2(extent)/float2(Size)),extent-1);
 float4 source=OutputOriginal.Load(int3(id.xy,0));
 float3 original=max(source.rgb,0)/PaperWhiteScale;
 float3 upgraded=Upgrade(original,Decode(Proxy.Load(int3(p,0)).rgb),Decode(Neural.Load(int3(id.xy,0)).rgb));
 float oy=Luminance(original),uy=Luminance(upgraded);
 float ratio=oy==0?1:clamp(uy/oy,0,4);
 float3 result=lerp(original*ratio,upgraded,ColorStrength);
 Output[id.xy]=float4(result*PaperWhiteScale,source.a);
}
