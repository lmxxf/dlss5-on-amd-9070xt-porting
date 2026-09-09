#include <cuda_runtime.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <stdexcept>
static void ck(cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));}
__global__ void reciprocal(const float*input,float*output,unsigned count){
 unsigned i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
 float v;asm("rcp.approx.ftz.f32 %0, %1;":"=f"(v):"f"(input[i]));output[i]=v;
}
int main(int argc,char**argv){try{
 if(argc!=3)return 2;
 std::ifstream f(argv[1],std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("input missing");
 auto bytes=f.tellg();if(bytes<=0||bytes%4||bytes>128*1024*1024)throw std::runtime_error("input size");
 std::vector<float>v(size_t(bytes)/4);f.seekg(0);if(!f.read((char*)v.data(),bytes))throw std::runtime_error("read");
 float *a=nullptr,*b=nullptr;ck(cudaMalloc(&a,size_t(bytes)));ck(cudaMalloc(&b,size_t(bytes)));
 ck(cudaMemcpy(a,v.data(),size_t(bytes),cudaMemcpyHostToDevice));
 reciprocal<<<(v.size()+255)/256,256>>>(a,b,unsigned(v.size()));ck(cudaGetLastError());ck(cudaDeviceSynchronize());
 ck(cudaMemcpy(v.data(),b,size_t(bytes),cudaMemcpyDeviceToHost));
 std::ofstream out(argv[2],std::ios::binary);if(!out.write((char*)v.data(),bytes))throw std::runtime_error("write");
 ck(cudaFree(a));ck(cudaFree(b));printf("reciprocal values=%zu\n",v.size());return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
