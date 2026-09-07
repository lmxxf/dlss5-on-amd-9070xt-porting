StructuredBuffer<float> input:register(t0);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
// Caller supplies finite FP8-lattice features; conversion to half is exact.
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
 uint n=id.x+id.y*4194240u;if(n>=width*height*128)return;
 output.Store(n*4,(f32tof16(input[n*2])&65535u)|(f32tof16(input[n*2+1])<<16));
}
