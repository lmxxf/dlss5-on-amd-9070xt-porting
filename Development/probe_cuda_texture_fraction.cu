#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <cmath>
static void ck(cudaError_t e){if(e!=cudaSuccess){fprintf(stderr,"%s\n",cudaGetErrorString(e));exit(1);}}
__global__ void sample(cudaTextureObject_t texture,unsigned base,int radius,float4*out){
 unsigned i=blockIdx.x*blockDim.x+threadIdx.x;if(i>unsigned(2*radius))return;
 float u=__uint_as_float(base+i-radius);out[i]=tex2D<float4>(texture,u,.5f);
}
int main(int argc,char**argv){
 if(argc!=6){fprintf(stderr,"usage: probe width height normalized_u_bits radius output.csv\n");return 2;}
 int w=atoi(argv[1]),h=atoi(argv[2]),radius=atoi(argv[4]);unsigned base=strtoul(argv[3],nullptr,0);
 if(w<2||h<2||w>4096||h>4096||radius<1||radius>100000)return 2;
 std::vector<float4>pixels(size_t(w)*h);for(int y=0;y<h;y++)for(int x=0;x<w;x++)pixels[y*w+x]=make_float4(float(x),float(y),1.f,1.f);
 cudaArray_t array;auto format=cudaCreateChannelDesc<float4>();ck(cudaMallocArray(&array,&format,w,h));ck(cudaMemcpy2DToArray(array,0,0,pixels.data(),w*sizeof(float4),w*sizeof(float4),h,cudaMemcpyHostToDevice));
 cudaResourceDesc resource{};resource.resType=cudaResourceTypeArray;resource.res.array.array=array;cudaTextureDesc desc{};desc.addressMode[0]=desc.addressMode[1]=cudaAddressModeClamp;desc.filterMode=cudaFilterModeLinear;desc.readMode=cudaReadModeElementType;desc.normalizedCoords=1;
 cudaTextureObject_t texture;ck(cudaCreateTextureObject(&texture,&resource,&desc,nullptr));float4*gpu;const int count=2*radius+1;ck(cudaMalloc(&gpu,count*sizeof(float4)));sample<<<(count+63)/64,64>>>(texture,base,radius,gpu);ck(cudaGetLastError());ck(cudaDeviceSynchronize());std::vector<float4>result(count);ck(cudaMemcpy(result.data(),gpu,count*sizeof(float4),cudaMemcpyDeviceToHost));
 std::ofstream out(argv[5]);if(!out)return 2;out<<"u_bits,u,ideal_x,tex_x,ideal_quantized_x\n";out.precision(12);int mismatch=0;
 for(int i=0;i<count;i++){unsigned bits=base+i-radius;float u;memcpy(&u,&bits,4);float ideal=u*w-.5f;float q=nearbyintf(ideal*256.f)/256.f;mismatch+=q!=result[i].x;out<<bits<<','<<u<<','<<ideal<<','<<result[i].x<<','<<q<<'\n';}
 printf("samples=%d float32_nearest_fraction_mismatches=%d center_tex_x=%.9g\n",count,mismatch,result[radius].x);
 ck(cudaFree(gpu));ck(cudaDestroyTextureObject(texture));ck(cudaFreeArray(array));return 0;
}
