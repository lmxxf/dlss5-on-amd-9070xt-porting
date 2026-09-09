#define NATIVE_TEXTURE_LOG_PATH LR"(D:\DLSSNR-Lab\logs\native-texture-contract-unit.txt)"
#include "native_nvapi_texture_contract.cpp"
#include <stdexcept>
static unsigned forwarded=0;
static int __cdecl fake_independent(IndependentDescriptor*p){++forwarded;p->handle=0x123456789abcdef0ull;return -7;}
static int __cdecl fake_surface(ID3D12Device*d,D3D12_CPU_DESCRIPTOR_HANDLE h,uint32_t*out){if(d||h.ptr!=0x123)throw std::runtime_error("surface args");++forwarded;*out=0xabcdef01u;return -9;}
static int __cdecl fake_merged(MergedTexture*p){++forwarded;if(!p)return -11;p->handle=0x8120000a801ull;return 0;}
int main(){try{
 original_independent=&fake_independent;original_surface=&fake_surface;original=&fake_merged;
 IndependentDescriptor p{};p.size_in=p.size_out=sizeof(p);p.type=0;p.descriptor.ptr=0x123;
 if(independent(&p)!=-7||p.handle!=0x123456789abcdef0ull||p.descriptor.ptr!=0x123||p.size_in!=48)throw std::runtime_error("independent forwarding");
 uint32_t handle=0;if(surface(nullptr,{0x123},&handle)!=-9||handle!=0xabcdef01u)throw std::runtime_error("surface forwarding");
 MergedTexture m{};m.size_in=m.size_out=sizeof(m);m.texture.ptr=0x321;
 if(merged(&m)!=0||m.handle!=0x8120000a801ull||m.texture.ptr!=0x321||merged(nullptr)!=-11)throw std::runtime_error("merged forwarding");
 if(forwarded!=4)throw std::runtime_error("forward count");
 puts("metadata wrapper forwarding pass; no GPU, no hook installation, not live ABI acceptance");return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
