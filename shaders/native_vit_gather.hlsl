StructuredBuffer<float> input:register(t0);
StructuredBuffer<uint> indices:register(t1);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint count;}
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){if(id.x<count)output[id.x]=input[indices[id.x]];}
