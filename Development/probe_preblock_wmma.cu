// Direct half-accumulator tensor operation for a 64-pixel input-mix window.
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <mma.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void ck(cudaError_t e){if(e!=cudaSuccess){std::fprintf(stderr,"%s\n",cudaGetErrorString(e));std::exit(1);}}
static std::vector<__half> read(const char*p,size_t n){std::vector<__half>v(n);FILE*f=std::fopen(p,"rb");if(!f||std::fread(v.data(),2,n,f)!=n||std::fgetc(f)!=EOF)std::exit(2);std::fclose(f);return v;}
__global__ void mix(const __half*x,const __half*w,__half*out){
 using namespace nvcuda;
 wmma::fragment<wmma::matrix_a,16,16,16,__half,wmma::row_major>a;
 wmma::fragment<wmma::matrix_b,16,16,16,__half,wmma::col_major>b;
 wmma::fragment<wmma::accumulator,16,16,16,__half>c;
 wmma::load_matrix_sync(a,x+blockIdx.x*16*16,16);
 for(int bank=0;bank<2;bank++){
  wmma::load_matrix_sync(b,w+bank*16*16,16);wmma::fill_fragment(c,__float2half(0));wmma::mma_sync(c,a,b,c);wmma::store_matrix_sync(out+blockIdx.x*16*32+bank*16,c,32,wmma::mem_row_major);
 }
}
int main(int argc,char**argv){if(argc!=4&&argc!=5)return 2;size_t pixels=argc==5?std::strtoul(argv[4],nullptr,0):64;if(!pixels||pixels>262144||pixels%16)return 2;auto x=read(argv[1],pixels*16),w=read(argv[2],32*16);__half *dx,*dw,*dy;ck(cudaMalloc(&dx,x.size()*2));ck(cudaMalloc(&dw,w.size()*2));ck(cudaMalloc(&dy,pixels*32*2));ck(cudaMemcpy(dx,x.data(),x.size()*2,cudaMemcpyHostToDevice));ck(cudaMemcpy(dw,w.data(),w.size()*2,cudaMemcpyHostToDevice));mix<<<pixels/16,32>>>(dx,dw,dy);ck(cudaGetLastError());ck(cudaDeviceSynchronize());std::vector<__half>y(pixels*32);ck(cudaMemcpy(y.data(),dy,y.size()*2,cudaMemcpyDeviceToHost));FILE*f=std::fopen(argv[3],"wb");if(!f||std::fwrite(y.data(),2,y.size(),f)!=y.size())return 2;std::fclose(f);ck(cudaFree(dx));ck(cudaFree(dw));ck(cudaFree(dy));return 0;}
