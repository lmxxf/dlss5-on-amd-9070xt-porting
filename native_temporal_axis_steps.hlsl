// Diagnostic only: Y-axis arithmetic for pixels beginning at row340.
#define main temporal_production_main
#include "native_temporal_production.hlsl"
#undef main
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
    if(id.x>=count/4||id.x+652800>=count)return;
    precise float p=coordinates[id.x+652800].y;
    precise float center=floor(mad(p,float(height),-.5))+.5;
    precise float t=saturate(float(fma(double(p),double(height),-double(center))));
    precise float t2=t*t,t3=t2*t,sum=t+t3;
    precise float left=mad(sum,-.5,t2),scaled=t2*2.5;
    precise float inner=temporal_inner_product(t3,scaled);inner=inner+1;
    precise float right=(t3-t2)*.5;
    precise float other=1-left;other=other-inner;other=other-right;
    precise float middle=inner+other,reciprocal=temporal_reciprocal(middle);
    precise float position=float(fma(double(other),double(reciprocal),double(center)));
    precise float normalized=position*inverse_height;
    precise float pixel=float(fma(double(normalized),double(height),0.0));
    precise float uv=pixel*inverse_height;
    reconstructed[4*id.x]=float4(p,center,t,t2);
    reconstructed[4*id.x+1]=float4(t3,sum,left,scaled);
    reconstructed[4*id.x+2]=float4(inner,right,other,middle);
    reconstructed[4*id.x+3]=float4(reciprocal,position,normalized,uv);
}
