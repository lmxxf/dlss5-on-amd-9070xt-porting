#include <cuda_runtime.h>
#include <cstdio>
#include <fstream>
#include <vector>
static void ck(cudaError_t e){if(e!=cudaSuccess){fprintf(stderr,"%s\n",cudaGetErrorString(e));exit(1);}}
__global__ void run(const float*in,float*out,unsigned n){unsigned i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=n)return;float r,s;asm("rcp.approx.ftz.f32 %0, %1;":"=f"(r):"f"(in[i]));asm("rsqrt.approx.ftz.f32 %0, %1;":"=f"(s):"f"(in[i]));out[i*2]=r;out[i*2+1]=s;}
int main(int argc,char**argv){if(argc!=3)return 2;std::ifstream f(argv[1],std::ios::binary|std::ios::ate);if(!f)return 2;auto bytes=f.tellg();if(bytes<=0||size_t(bytes)%4)return 2;std::vector<float>in(size_t(bytes)/4),out(in.size()*2);f.seekg(0);f.read((char*)in.data(),bytes);float*a,*b;ck(cudaMalloc(&a,size_t(bytes)));ck(cudaMalloc(&b,out.size()*4));ck(cudaMemcpy(a,in.data(),size_t(bytes),cudaMemcpyHostToDevice));run<<<(in.size()+127)/128,128>>>(a,b,in.size());ck(cudaGetLastError());ck(cudaDeviceSynchronize());ck(cudaMemcpy(out.data(),b,out.size()*4,cudaMemcpyDeviceToHost));std::ofstream g(argv[2],std::ios::binary);g.write((char*)out.data(),out.size()*4);ck(cudaFree(a));ck(cudaFree(b));return g?0:2;}
