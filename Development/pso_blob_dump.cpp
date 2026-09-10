// pso_blob_dump: creates a compute PSO for a compiled shader (cso) on the AMD device through the private Agility SDK
// (D3D12\D3D12Core.dll next to the exe, same as matrix_probe) and writes ID3D12PipelineState::GetCachedBlob() to disk,
// plus every ELF image found inside it (the AMD driver caches its PAL pipeline code object there, which
// `rga.exe -s bin --co <elf>` disassembles into gfx1201 ISA with register/occupancy statistics). RGA's own DX12 path cannot
// build SM 6.10 pipelines, so this is the way to see the ISA of the wave-matrix kernels.
// A superset root signature is used (SRV t0..t7, UAV u0..u5 as root descriptors, 32 root constants at b0): pipeline creation
// only needs every register the shader binds to exist. usage: pso_blob_dump.exe <cso> <out-prefix> [entry-is-irrelevant]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = 721;
__declspec(dllexport) const char *D3D12SDKPath = ".\\D3D12\\";
}
static std::vector<char> read_file(const char*p){std::vector<char>v;FILE*f=std::fopen(p,"rb");if(!f)return v;std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::fseek(f,0,SEEK_SET);v.resize(size_t(n));if(n)std::fread(v.data(),1,size_t(n),f);std::fclose(f);return v;}
static void write_file(const std::string&p,const void*d,size_t n){FILE*f=std::fopen(p.c_str(),"wb");if(!f)return;std::fwrite(d,1,n,f);std::fclose(f);}
int main(int argc,char**argv){
 if(argc<3){std::printf("usage: pso_blob_dump.exe <cso> <out-prefix>\n");return 1;}
 const IID experimental={0x76f5573e,0xf13a,0x40f5,{0xb2,0x97,0x81,0xce,0x9e,0x18,0x93,0x3f}};
 HRESULT hr=D3D12EnableExperimentalFeatures(1,&experimental,nullptr,nullptr);std::printf("experimental_hr=%08x\n",unsigned(hr));
 IDXGIFactory6*factory=nullptr;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 2;
 IDXGIAdapter1*adapter=nullptr;for(UINT i=0;;i++){IDXGIAdapter1*c=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&c))==DXGI_ERROR_NOT_FOUND)break;if(!c)continue;DXGI_ADAPTER_DESC1 d{};c->GetDesc1(&d);if(d.VendorId==0x1002&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=c;break;}c->Release();}
 factory->Release();if(!adapter){std::printf("no AMD adapter\n");return 3;}
 ID3D12Device*device=nullptr;hr=D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device));adapter->Release();std::printf("device_hr=%08x\n",unsigned(hr));if(FAILED(hr))return 4;
 D3D12_ROOT_PARAMETER params[15]{};UINT n=0;
 for(UINT i=0;i<8;i++){params[n].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[n].Descriptor={i,0};n++;}
 for(UINT i=0;i<6;i++){params[n].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[n].Descriptor={i,0};n++;}
 params[n].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[n].Constants={0,0,32};n++;
 D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=n;desc.pParameters=params;
 ID3DBlob*blob=nullptr,*error=nullptr;hr=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);if(FAILED(hr)){std::printf("rootsig serialize hr=%08x %s\n",unsigned(hr),error?(const char*)error->GetBufferPointer():"");return 5;}
 ID3D12RootSignature*root=nullptr;hr=device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root));blob->Release();if(FAILED(hr)){std::printf("rootsig hr=%08x\n",unsigned(hr));return 6;}
 std::vector<char>cso=read_file(argv[1]);if(cso.empty()){std::printf("cannot read %s\n",argv[1]);return 7;}
 D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={cso.data(),cso.size()};
 ID3D12PipelineState*pso=nullptr;hr=device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso));std::printf("pso_hr=%08x\n",unsigned(hr));if(FAILED(hr))return 8;
 ID3DBlob*cached=nullptr;hr=pso->GetCachedBlob(&cached);std::printf("cached_hr=%08x bytes=%zu\n",unsigned(hr),cached?size_t(cached->GetBufferSize()):size_t(0));if(FAILED(hr)||!cached)return 9;
 const char*p=(const char*)cached->GetBufferPointer();size_t size=cached->GetBufferSize();std::string prefix=argv[2];
 write_file(prefix+".blob",p,size);
 UINT count=0;
 for(size_t i=0;i+64<=size;i++){
  if(p[i]==0x7f&&p[i+1]=='E'&&p[i+2]=='L'&&p[i+3]=='F'){
   /* ELF64 little endian: e_shoff at 0x28, e_shentsize at 0x3a, e_shnum at 0x3c; the image ends at the section header table */
   unsigned long long shoff=0;std::memcpy(&shoff,p+i+0x28,8);unsigned short shentsize=0,shnum=0;std::memcpy(&shentsize,p+i+0x3a,2);std::memcpy(&shnum,p+i+0x3c,2);
   size_t end=size_t(shoff)+size_t(shentsize)*shnum;if(!shoff||i+end>size){std::printf("elf at %zu: header out of range\n",i);continue;}
   write_file(prefix+"-"+std::to_string(count)+".elf",p+i,end);std::printf("elf %u at offset %zu bytes=%zu\n",count,i,end);count++;i+=end-1;
  }
 }
 std::printf("elf_count=%u\n",count);
 return 0;
}
