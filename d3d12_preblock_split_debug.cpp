#define wmain unused_rgb_main
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_preblock_runtime.h"
#include "native_game_submission.h"
extern "C" {__declspec(dllexport) extern const UINT D3D12SDKVersion=721;__declspec(dllexport) const char*D3D12SDKPath=".\\D3D12\\";}
int wmain(int argc,wchar_t**argv){try{
 if(argc!=2)return 2;std::wstring dir=argv[1];
 const IID e={0x76f5573e,0xf13a,0x40f5,{0xb2,0x97,0x81,0xce,0x9e,0x18,0x93,0x3f}};ck(D3D12EnableExperimentalFeatures(1,&e,nullptr,nullptr));
 auto read=[](const std::wstring&p){std::ifstream f(p.c_str(),std::ios::binary|std::ios::ate);if(!f||f.tellg()<=0||size_t(f.tellg())%4)throw std::runtime_error("fixture");std::vector<float>v(size_t(f.tellg())/4);f.seekg(0);if(!f.read((char*)v.data(),v.size()*4))throw std::runtime_error("read");return v;};
 auto rgb=read(dir+L"\\input.f32"),fw=read(dir+L"\\block0-ffn.f32"),aw=read(dir+L"\\block0-attention.f32"),noise=read(L"D:\\DLSSNR-Lab\\matrix-probe\\native-runtime-rgb512\\functions.f32");rgb.resize(64*64*4);
 IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002)ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();if(d)break;}if(!d)throw std::runtime_error("AMD missing");
 auto*in=buf(d,rgb.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};ck(in->Map(0,&none,&p));std::memcpy(p,rgb.data(),rgb.size()*4);in->Unmap(0,nullptr);
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);
 NativePreblockRuntime old,candidate;_wputenv_s(L"DLSS5_TEST_SPLIT_PREBLOCK_FFN",L"0");old.Create(d,in,64,64,fw,aw,dir,true,false,&noise);_wputenv_s(L"DLSS5_TEST_SPLIT_PREBLOCK_FFN",L"1");candidate.Create(d,in,64,64,fw,aw,dir,true,false,&noise);
 constexpr UINT64 bytes=64*64*32*4;auto*rb=buf(d,bytes*4,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);auto*prefix=buf(d,4096,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);candidate.CapturePrefixForTest(prefix);
 bool mismatch=false;
 for(UINT frame=0;frame<2;frame++){
  submit.Submit([&](ID3D12GraphicsCommandList*c){old.Record(c,0);candidate.Record(c,0);ID3D12Resource*rs[]={old.FfnTilesForTest(),candidate.FfnTilesForTest(),old.Main(),candidate.Main()};for(UINT i=0;i<4;i++){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={rs[i],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);c->CopyBufferRegion(rb,bytes*i,rs[i],0,bytes);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);}});
  D3D12_RANGE range{0,size_t(bytes*4)};ck(rb->Map(0,&range,&p));auto*v=(float*)p;for(UINT pair=0;pair<2;pair++){size_t diff=0,bad=0;for(size_t i=0;i<bytes/4;i++){float a=v[(pair*2)*bytes/4+i],b=v[(pair*2+1)*bytes/4+i];diff+=a!=b;bad+=!std::isfinite(b);}mismatch|=diff!=0||bad!=0;printf("frame=%u pair=%u different=%zu nonfinite=%zu old_first=%g new_first=%g\n",frame,pair,diff,bad,v[pair*2*bytes/4],v[(pair*2+1)*bytes/4]);}std::ofstream out((dir+L"\\debug-stages.f32").c_str(),std::ios::binary);out.write((char*)p,bytes*4);rb->Unmap(0,&none);
  D3D12_RANGE pr{0,4096};ck(prefix->Map(0,&pr,&p));std::ofstream po((dir+L"\\debug-prefix.f32").c_str(),std::ios::binary);po.write((char*)p,4096);printf("prefix_first=%g\n",((float*)p)[0]);prefix->Unmap(0,&none);
 }
 return mismatch?3:0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
