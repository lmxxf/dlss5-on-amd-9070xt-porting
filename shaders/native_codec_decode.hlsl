cbuffer CodecConstants : register(b0) {
 uint2 Size; uint2 SourceSize; uint2 SourceBase; uint2 ProxySize;
 float PaperWhiteScale; float TransferStrength; float ColorStrength; uint HdrMode;
 float4 Padding;
 uint OutputRowPitch; uint3 Reserved;
};
#ifndef NATIVE_CODEC_EXPOSURE
#define NATIVE_CODEC_EXPOSURE 0
#endif
#if NATIVE_CODEC_EXPOSURE
Texture2D<float> GameExposure : register(t4);
#endif
// Same meter as encode; sample the original game colour so both sides share the white point.
Texture2D<float4> Proxy : register(t1);
Texture2D<float4> Neural : register(t2);
Texture2D<float4> OutputOriginal : register(t3);
static const float kTargetEncodedMean = 0.45f;
float WhitePointForMean(float meanLuma) {
    float encoded = pow(kTargetEncodedMean, 2.2f);
    float ratio = encoded / (1.0 - encoded);
    float wp = meanLuma / ratio;
    return clamp(wp, 0.01f, 10000.0f);
}
float SampleMeanLuma() {
    uint w = max(SourceSize.x, 1u), h = max(SourceSize.y, 1u);
    float sum = 0.0;
    const int N = 5;
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            uint2 p = uint2(uint((x + 0.5) * (w - 1) / (N - 1)), uint((y + 0.5) * (h - 1) / (N - 1)));
            p = min(p, uint2(w, h) - 1);
            float3 c = max(OutputOriginal.Load(int3(p, 0)).rgb, 0.0);
            sum += dot(c, float3(0.2126, 0.7152, 0.0722));
        }
    }
    return max(sum / float(N * N), 1e-4);
}
float EffectivePaperWhite() {
#if NATIVE_CODEC_EXPOSURE
 float e=GameExposure.Load(int3(0,0,0));
 float pre=asfloat(Reserved.y),scale=asfloat(Reserved.z);
 float exposure=e*scale/pre;
 return PaperWhiteScale / ((isfinite(exposure)&&exposure>0)?exposure:1.0);
#else
 if ((Reserved.x & 0x10000u) != 0)
     return WhitePointForMean(SampleMeanLuma()) * PaperWhiteScale;
 float preOnly=asfloat(Reserved.y);
 if ((Reserved.x & 0x20000u) != 0 && isfinite(preOnly)&&preOnly>0&&abs(preOnly-1.0)>1e-3)
     return PaperWhiteScale*preOnly;
 return PaperWhiteScale;
#endif
}
#ifndef NATIVE_CODEC_FIT
#define NATIVE_CODEC_FIT 0
#endif
uint ByteOffset(uint2 p,uint bpp) {
#if NATIVE_CODEC_FIT
 return p.y*OutputRowPitch+p.x*bpp;
#else
 return (p.y*1920+p.x)*bpp;
#endif
}
// Mode1 candidate. Oracle: captured codec-22724-36e36d370.dxbc.
// Host must reject other modes; GPU comparison required before integration.
#ifndef NATIVE_CODEC_UINT_OUT
#define NATIVE_CODEC_UINT_OUT 0
#endif
#ifndef NATIVE_CODEC_SRGB_IO
#define NATIVE_CODEC_SRGB_IO 0
#endif
#ifndef NATIVE_CODEC_UNORM8_OUT
#define NATIVE_CODEC_UNORM8_OUT 0
#endif
#ifndef NATIVE_CODEC_BGRA
#define NATIVE_CODEC_BGRA 0
#endif
#if NATIVE_CODEC_UNORM8_OUT
// 8-bit UNORM host textures (Magpie): UNORM8 bits in a raw buffer (row pitch 1920*4), copied into the texture by the frame; BGRA byte order when NATIVE_CODEC_BGRA.
RWByteAddressBuffer OutputBits : register(u0);
#ifndef NATIVE_CODEC_DEBUG_TINT
#define NATIVE_CODEC_DEBUG_TINT 0
#endif
void Store(uint2 p,float4 v){
#if NATIVE_CODEC_DEBUG_TINT
 v.g*=0.25;
#endif
 uint4 q=uint4(round(saturate(v)*255.0));
#if NATIVE_CODEC_BGRA
 OutputBits.Store(ByteOffset(p,4),q.z|(q.y<<8)|(q.x<<16)|(q.w<<24));
#else
 OutputBits.Store(ByteOffset(p,4),q.x|(q.y<<8)|(q.z<<16)|(q.w<<24));
#endif
}
#elif NATIVE_CODEC_R11_OUT
// R11G11B10_FLOAT game textures (UE5 scene colour, Black Myth: Wukong): the three small floats packed into one 32-bit word per pixel in a
// raw buffer (row pitch width*4), copied into the texture by the frame. f11 = half bits >> 4 (5-bit exponent, 6-bit mantissa), f10 = half
// bits >> 5 (5-bit mantissa); negatives clamp to 0 (the format is unsigned), the range is that of FP16, no saturation.
RWByteAddressBuffer OutputBits : register(u0);
void Store(uint2 p,float4 v){
 uint3 h=f32tof16(max(v.rgb,0.0));
 uint r=(h.x>>4)&0x7FFu,g=(h.y>>4)&0x7FFu,b=(h.z>>5)&0x3FFu;
 OutputBits.Store(ByteOffset(p,4),r|(g<<11)|(b<<22));
}
#elif NATIVE_CODEC_UINT_OUT
// Typeless UNORM16 game textures (Rise of the Ronin): this driver device-removes on any non-float RGBA16 typed UAV, so the
// UNORM bits go into a raw buffer (row pitch 1920*8) that the frame copies into the game texture with CopyTextureRegion.
RWByteAddressBuffer OutputBits : register(u0);
void Store(uint2 p,float4 v){uint4 q=uint4(round(saturate(v)*65535.0));OutputBits.Store2(ByteOffset(p,8),uint2(q.x|(q.y<<16),q.z|(q.w<<16)));}
#else
RWTexture2D<float4> Output : register(u0);
void Store(uint2 p,float4 v){Output[p]=v;}
#endif
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
#ifndef NATIVE_CODEC_NEURAL_BUFFER
#define NATIVE_CODEC_NEURAL_BUFFER 0
#endif
#if NATIVE_CODEC_NEURAL_BUFFER
/* DLSS5_IO_FUSE=1 (2026-10-01, input-slim): read the network's f32 RGB output directly instead of the RGBA16F texture the neural pass
   used to fill; each value takes the same f32 -> f16 round trip the texture store did. Row stride = network surface width (ProxySize.x). */
StructuredBuffer<float> NeuralRgb : register(t5);
float3 NeuralAt(uint2 q){uint i=(q.y*ProxySize.x+q.x)*3;return f16tof32(f32tof16(float3(NeuralRgb[i],NeuralRgb[i+1],NeuralRgb[i+2])));}
#endif
#if NATIVE_CODEC_FIT
float3 ReadFitted(Texture2D<float4> image,float2 p) {
 p=clamp(p,Padding.xy,Padding.xy+Padding.zw-1);
 uint2 lo=uint2(floor(p)),hi=min(lo+1,uint2(Padding.xy+Padding.zw-1));float2 f=p-lo;
 return lerp(lerp(image.Load(int3(lo,0)).rgb,image.Load(int3(hi.x,lo.y,0)).rgb,f.x),
             lerp(image.Load(int3(lo.x,hi.y,0)).rgb,image.Load(int3(hi,0)).rgb,f.x),f.y);
}
#if NATIVE_CODEC_NEURAL_BUFFER
float3 ReadFittedNeural(float2 p) {
 p=clamp(p,Padding.xy,Padding.xy+Padding.zw-1);
 uint2 lo=uint2(floor(p)),hi=min(lo+1,uint2(Padding.xy+Padding.zw-1));float2 f=p-lo;
 return lerp(lerp(NeuralAt(lo),NeuralAt(uint2(hi.x,lo.y)),f.x),
             lerp(NeuralAt(uint2(lo.x,hi.y)),NeuralAt(hi),f.x),f.y);
}
#define NEURAL_FITTED(q) ReadFittedNeural(q)
#else
#define NEURAL_FITTED(q) ReadFitted(Neural,q)
#endif
#endif
#if NATIVE_CODEC_NEURAL_BUFFER
#define NEURAL_AT(q) NeuralAt(q)
#else
#define NEURAL_AT(q) Neural.Load(int3(q,0)).rgb
#endif
[numthreads(16,16,1)]
void main(uint3 id:SV_DispatchThreadID) {
 if(any(id.xy>=Size))return;
 // Allocation padding is outside the host active render area.
 if(any(id.xy>=SourceSize)){Store(id.xy,OutputOriginal.Load(int3(id.xy,0)));return;}
 if(HdrMode!=1||PaperWhiteScale<=0){Store(id.xy,0);return;}
 uint2 extent=max(ProxySize,uint2(1,1));
 uint2 p=min(uint2((float2(id.xy)+0.5)*float2(extent)/float2(SourceSize)),extent-1);
 float4 source=OutputOriginal.Load(int3(id.xy,0));
#if NATIVE_CODEC_SRGB_IO
 /* DLSS5_CODEC_SRGB (Magpie): the source is display-referred sRGB; linearize it for the blend and re-encode the result */
 float3 original=Decode(saturate(source.rgb));
#else
 float3 original=max(source.rgb,0)/EffectivePaperWhite();
#endif
 #if NATIVE_CODEC_FIT
 float2 network_p=Padding.xy+(float2(id.xy)+.5)*Padding.zw/float2(SourceSize)-.5;
 float3 upgraded=Upgrade(original,Decode(ReadFitted(Proxy,network_p)),Decode(NEURAL_FITTED(network_p)));
#else
 float3 upgraded=Upgrade(original,Decode(Proxy.Load(int3(p,0)).rgb),Decode(NEURAL_AT(id.xy)));
#endif
 float oy=Luminance(original),uy=Luminance(upgraded);
 float ratio=oy==0?1:clamp(uy/oy,0,4);
 // Legacy CS=1 remains the upgraded result. Hosts explicitly opt into the
 // alternative curve: CS=0 original, CS=1 game chroma/network luma, CS=2 upgraded.
 float3 result=lerp(original*ratio,upgraded,ColorStrength);
 if ((Reserved.x & 0x40000u) != 0) {
  float3 hueSafe=original*ratio;
  result=lerp(original,hueSafe,clamp(ColorStrength,0.0,1.0));
  result=lerp(result,upgraded,max(ColorStrength-1.0,0.0));
 }
 uint view=Reserved.x & 0xFFFFu;
 // Optional per-dispatch views; view 0 preserves the captured composition exactly.
 if(view==1||view==2){
#if NATIVE_CODEC_FIT
  result=view==1?Decode(ReadFitted(Proxy,network_p)):Decode(NEURAL_FITTED(network_p));
#else
  result=view==1?Decode(Proxy.Load(int3(p,0)).rgb):Decode(NEURAL_AT(id.xy));
#endif
 }else if(view==3)result=saturate(0.5+(upgraded-original)*20.0);
 else if(view==4)result*=float3(1.2,0.3,1.2);

#if NATIVE_CODEC_SRGB_IO
 result=saturate(result);result=result<=0.0031308?result*12.92:1.055*pow(result,1.0/2.4)-0.055;
 Store(id.xy,float4(result,source.a));
#else
 Store(id.xy,float4(result*EffectivePaperWhite(),source.a));
#endif
}
