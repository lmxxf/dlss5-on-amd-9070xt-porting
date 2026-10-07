#pragma once
// Independent implementation of the documented low-band residual algorithm.
// Reference: SAOG0721/Magpie 2fceab5e. No GPL shader source is copied here.
inline constexpr char NativeLowFrequencyShader[]=R"shader(
Texture2D<float2> Vectors:register(t0);
StructuredBuffer<float4> Original:register(t1);
StructuredBuffer<float4> Difference:register(t2);
StructuredBuffer<uint2> Observed:register(t3);
StructuredBuffer<uint2> ObservationGuide:register(t4);
StructuredBuffer<uint2> PriorBand:register(t5);
StructuredBuffer<uint2> PriorGuide:register(t6);
RWStructuredBuffer<float4> DifferenceOut:register(u0);
RWStructuredBuffer<uint2> ObservedOut:register(u1);
RWStructuredBuffer<uint2> ObservationGuideOut:register(u2);
RWStructuredBuffer<uint2> NextBand:register(u3);
RWStructuredBuffer<uint2> NextGuide:register(u4);
RWStructuredBuffer<float> Result:register(u5);
cbuffer Control:register(b0){uint W,H,PH,Past;uint MW,MH,Motion,Pad;float SX,SY,Memory,Unused;uint4 Reserved;};
float4 Decode(uint2 a){return float4(f16tof32(a.x&65535),f16tof32(a.x>>16),f16tof32(a.y&65535),f16tof32(a.y>>16));}
uint2 Encode(float3 a,bool valid){a=clamp(a,-65504,65504);return uint2(f32tof16(a.x)|(f32tof16(a.y)<<16),f32tof16(a.z)|(f32tof16(valid?1:0)<<16));}
float Confidence(float d,float a,float b){float z=saturate((d-a)/(b-a));return 1-z*z*(3-2*z);}
float Error(float3 a,float3 b){float3 d=abs(a-b);return max(d.x,max(d.y,d.z));}
bool At(int2 p){return all(p>=0)&&p.x<int(W)&&p.y<int(H);}
float2 Displacement(int2 p){if(!Motion)return 0;uint2 t=min(uint2((float2(p)+.5)/float2(W,H)*float2(MW,MH)),uint2(MW-1,MH-1));return Vectors.Load(int3(t,0))*float2(SX,SY);}
bool Position(int2 p,out float2 q){q=float2(p)+Displacement(p);return At(p)&&all(isfinite(q))&&all(q>=0)&&q.x<=W-1&&q.y<=H-1;}
[numthreads(8,8,1)]void Extract(uint3 t:SV_DispatchThreadID){if(t.x>=W||t.y>=H)return;uint i=t.y*W+t.x;float3 x=Original[i].rgb,y=float3(Result[3*i],Result[3*i+1],Result[3*i+2]);bool ok=all(isfinite(x))&&all(isfinite(y))&&!all(y==0);DifferenceOut[i]=float4(y-x,ok?1:0);}
[numthreads(8,8,1)]void Observe(uint3 t:SV_DispatchThreadID){uint cw=(W+1)/2,ch=(H+1)/2;if(t.x>=cw||t.y>=ch)return;float3 a=0,g=0;uint count=0;bool ok=true;
 for(uint k=0;k<4;k++){uint2 p=t.xy*2+uint2(k%2,k/2);if(p.x>=W||p.y>=H)continue;uint i=p.y*W+p.x;float4 r=Difference[i];ok=ok&&r.w>0;a+=r.rgb;g+=Original[i].rgb;count++;}
 uint i=t.y*cw+t.x;ObservedOut[i]=Encode(a/max(count,1),ok&&count>0);ObservationGuideOut[i]=Encode(g/max(count,1),ok&&count>0);}
bool LowBand(int2 p,float3 guide,out float3 value){uint cw=(W+1)/2,ch=(H+1)/2;float2 a=floor(float2(p)*.5-.25);float3 sum=0;float total=0;
 for(uint k=0;k<4;k++){int2 q=clamp(int2(a)+int2(k%2,k/2),0,int2(cw,ch)-1);float2 c0=min(clamp(a,0,float2(cw,ch)-1)*2+.5,float2(W,H)-1),c1=min(clamp(a+1,0,float2(cw,ch)-1)*2+.5,float2(W,H)-1);float2 f=saturate((float2(p)-c0)/max(c1-c0,1));float weight=(k%2?f.x:1-f.x)*(k/2?f.y:1-f.y);
 uint i=q.y*cw+q.x;float4 r=Decode(Observed[i]),g=Decode(ObservationGuide[i]);if(r.w<.999||g.w<.999)continue;weight*=Confidence(Error(guide,g.rgb),.02,.10);sum+=weight*r.rgb;total+=weight;}
 value=sum/max(total,1e-12);return total>=.05;}
bool PreviousBand(float2 p,float3 guide,out float3 value){int2 a=int2(floor(p));float2 f=frac(p);float3 sum=0;float total=0;
 for(uint k=0;k<4;k++){int2 q=a+int2(k%2,k/2);float weight=(k%2?f.x:1-f.x)*(k/2?f.y:1-f.y);if(weight<=0||!At(q))continue;uint i=q.y*W+q.x;float4 b=Decode(PriorBand[i]),g=Decode(PriorGuide[i]);if(b.w<.999||g.w<.999)continue;weight*=Confidence(Error(guide,g.rgb),.025,.10);sum+=weight*b.rgb;total+=weight;}
 value=sum/max(total,1e-12);return total>=.25;}
[numthreads(8,8,1)]void Update(uint3 t:SV_DispatchThreadID){if(t.x>=W||t.y>=H)return;int2 p=t.xy;uint i=t.y*W+t.x;float3 x=Original[i].rgb;float4 r=Difference[i];float3 low;bool valid=r.w>0&&LowBand(p,x,low);NextGuide[i]=Encode(x,valid);
 if(!valid||all(r.rgb==0)){NextBand[i]=Encode(0,false);return;}
 float3 filtered=low;float2 previous;float3 old;
 if(Past&&Position(p,previous)&&PreviousBand(previous,x,old)){float average=0,worst=0;float3 minimum=r.rgb,maximum=r.rgb;bool patch=true;
 for(int n=0;n<9;n++){int2 q=p+int2(n%3-1,n/3-1);float2 v;if(!Position(q,v)){patch=false;continue;}if(Motion&&any(abs((v-float2(q))-(previous-float2(p)))>2)){patch=false;continue;}
 float3 guide=Original[q.y*W+q.x].rgb;int2 a=int2(floor(v));float2 f=frac(v);float3 prior=0;float mass=0;
 for(int k=0;k<4;k++){int2 at=a+int2(k%2,k/2);float w=(k%2?f.x:1-f.x)*(k/2?f.y:1-f.y);if(w<=0)continue;if(!At(at)){patch=false;continue;}float4 g=Decode(PriorGuide[at.y*W+at.x]);if(g.w<.999){patch=false;continue;}prior+=w*g.rgb;mass+=w;}
 if(mass<.999){patch=false;continue;}float e=Error(guide,prior);average+=e/9;worst=max(worst,e);float4 current=Difference[q.y*W+q.x];if(current.w<=0)patch=false;minimum=min(minimum,current.rgb);maximum=max(maximum,current.rgb);}
 float quality=Confidence(average,.008,.04)*Confidence(worst,.025,.10);if(patch&&quality>0){float3 padding=.02+quality*abs(old);float3 allowed=clamp(old,minimum-padding,maximum+padding);filtered=low+(allowed-low)*(Memory*quality);}}
 NextBand[i]=Encode(filtered,true);float3 outColor=saturate(float3(Result[3*i],Result[3*i+1],Result[3*i+2])+(filtered-low));if(Past&&any(filtered!=low)){Result[3*i]=outColor.x;Result[3*i+1]=outColor.y;Result[3*i+2]=outColor.z;}}
)shader";
