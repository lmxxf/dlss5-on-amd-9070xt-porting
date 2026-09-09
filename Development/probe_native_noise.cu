// Trace the original random-field operation sequence; no model weights.
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
static void ck(cudaError_t e){if(e!=cudaSuccess){std::fprintf(stderr,"%s\n",cudaGetErrorString(e));std::exit(1);}}
__device__ unsigned pcg(unsigned s){unsigned w=((s>>((s>>28)+4))^s)*0x108ef2d9u;return (w>>22)^w;}
__device__ float uniform(unsigned s){unsigned w=((s>>((s>>28)+4))^s)*0x108ef2d9u;return float(((w>>30)^(w>>8))+1)*0x1p-24f;}
__device__ float native_sqrt(float x){float y;asm("sqrt.approx.ftz.f32 %0, %1;":"=f"(y):"f"(x));return y;}
__global__ void trace(float*out,unsigned width,unsigned height,unsigned seed){
 unsigned p=blockIdx.x*blockDim.x+threadIdx.x;if(p>=width*height)return;
 unsigned x=p%width,y=p/width,h=pcg(x*0x8da6b343u^y*0xd8163841u^seed*0x9e3779b9u^0x243f6a88u);
 float a=uniform(h*0xcaa5b80du+0x21dd796bu),b=uniform(h*0x2c9277b5u+0xac564b05u);
 float c=uniform(h*0x83232c31u+0x3463e0acu),d=uniform(h*0xfa6dc5f9u+0x4712a88eu);
 float l0=__log2f(a),l1=__log2f(b);
 float r0=native_sqrt(__fmul_rn(-2.f,__fmul_rn(l0,.6931471824645996f)));
 float r1=native_sqrt(__fmul_rn(-2.f,__fmul_rn(l1,.6931471824645996f)));
 float t0=__fmul_rn(c,6.283185482025146f),t1=__fmul_rn(d,6.283185482025146f);
 float co0=__cosf(t0),co1=__cosf(t1),si1=__sinf(t1);
 float v[16]={a,b,c,d,l0,l1,r0,r1,t0,t1,co0,co1,si1,__fmul_rn(r0,co0),__fmul_rn(r1,co1),__fmul_rn(r1,si1)};
 for(int i=0;i<16;i++)out[p*16+i]=v[i];
}
__global__ void tables(float*out){
 unsigned i=blockIdx.x*blockDim.x+threadIdx.x;
 float u=float(i+1)*0x1p-24f,angle=__fmul_rn(u,6.283185482025146f);
 out[i*3]=native_sqrt(__fmul_rn(-2.f,__fmul_rn(__log2f(u),.6931471824645996f)));
 out[i*3+1]=__cosf(angle);out[i*3+2]=__sinf(angle);
}
int main(int argc,char**argv){
 if(argc==3&&!std::strcmp(argv[1],"--tables")){
  const size_t n=size_t(1<<24)*3;float*d=nullptr;ck(cudaMalloc(&d,n*4));tables<<<(1<<24)/256,256>>>(d);ck(cudaGetLastError());ck(cudaDeviceSynchronize());std::vector<float>v(n);ck(cudaMemcpy(v.data(),d,n*4,cudaMemcpyDeviceToHost));FILE*f=std::fopen(argv[2],"wb");if(!f)return 2;bool ok=std::fwrite(v.data(),4,n,f)==n;ok=std::fclose(f)==0&&ok;ck(cudaFree(d));return ok?0:2;
 }
 if(argc!=5)return 2;unsigned w=std::strtoul(argv[2],nullptr,0),h=std::strtoul(argv[3],nullptr,0),s=std::strtoul(argv[4],nullptr,0);if(!w||!h||w>512||h>512)return 2;
 size_t n=size_t(w)*h*16;float*d=nullptr;ck(cudaMalloc(&d,n*4));trace<<<(w*h+127)/128,128>>>(d,w,h,s);ck(cudaGetLastError());ck(cudaDeviceSynchronize());std::vector<float>v(n);ck(cudaMemcpy(v.data(),d,n*4,cudaMemcpyDeviceToHost));FILE*f=std::fopen(argv[1],"wb");if(!f)return 2;bool ok=std::fwrite(v.data(),4,n,f)==n;ok=std::fclose(f)==0&&ok;ck(cudaFree(d));return ok?0:2;}
