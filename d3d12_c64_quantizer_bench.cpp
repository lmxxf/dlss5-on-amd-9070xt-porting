#define wmain unused_rgb_main
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_c64.h"
#include "native_game_submission.h"
#include "native_network_timestamps.h"
int wmain(int argc,wchar_t**argv){try{
 if(argc<2||argc>4)return 2;bool split=argc>=3&&!wcscmp(argv[2],L"split");bool detail=argc==4&&!wcscmp(argv[3],L"detail");if(argc==4&&!detail)return 2;if(argc>=3&&!split)return 2;std::wstring dir=argv[1];printf("candidate_mode=%s\n",split?"split_ffn":"fp8_quantizer");fflush(stdout);
 auto read=[&](const wchar_t*name){std::ifstream f((dir+L"\\"+name).c_str(),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("missing fixture");auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error("fixture size");std::vector<float>v(size_t(n)/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(v.data()),n))throw std::runtime_error("fixture read");for(float x:v)if(!std::isfinite(x))throw std::runtime_error("nonfinite fixture");return v;};
 auto values=read(L"input.f32"),oracle=read(L"oracle.f32"),fw=read(L"ffn.f32"),aw=read(L"attention.f32");if(values.size()!=120*72*256||oracle.size()!=values.size())throw std::runtime_error("block52 geometry");
 IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002)ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();if(d)break;}if(!d)throw std::runtime_error("AMD missing");
 const UINT64 bytes=values.size()*4;auto*input=buf(d,bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);auto*rb=buf(d,bytes*2,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);void*p=nullptr;D3D12_RANGE none{};ck(input->Map(0,&none,&p));std::memcpy(p,values.data(),bytes);input->Unmap(0,nullptr);
 auto*legacy=new NativeC64;auto*fast=new NativeC64;legacy->Create(d,input,120,72,fw,aw,dir,false,256,false);puts("legacy initialized");fflush(stdout);fast->Create(d,input,120,72,fw,aw,dir,false,256,!split,split);puts("candidate initialized");fflush(stdout);
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);NativeNetworkTimestamps timer;timer.Create(d);
 for(UINT frame=0;frame<5;frame++){
  timer.Reset();submit.Submit([&](ID3D12GraphicsCommandList*c){timer.Mark(c,"start");if(frame%2){fast->Record(c,detail?&timer:nullptr,"candidate");timer.Mark(c,"fast");legacy->Record(c,detail?&timer:nullptr,"baseline");timer.Mark(c,"legacy");}else{legacy->Record(c,detail?&timer:nullptr,"baseline");timer.Mark(c,"legacy");fast->Record(c,detail?&timer:nullptr,"candidate");timer.Mark(c,"fast");}timer.Resolve(c);
   ID3D12Resource*out[]={legacy->Output(),fast->Output()};for(UINT i=0;i<2;i++){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={out[i],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);c->CopyBufferRegion(rb,bytes*i,out[i],0,bytes);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);}
  });timer.Report(submit.TimestampFrequency());
  D3D12_RANGE range{0,SIZE_T(bytes*2)};ck(rb->Map(0,&range,&p));auto*a=static_cast<float*>(p);auto*b=a+values.size();size_t baseline_diff=0,fast_diff=0,bit_diff=0;
  for(size_t i=0;i<values.size();i++){baseline_diff+=!std::isfinite(a[i])||a[i]!=oracle[i];fast_diff+=!std::isfinite(b[i])||b[i]!=oracle[i];bit_diff+=std::memcmp(a+i,b+i,4)!=0;}rb->Unmap(0,&none);
  printf("block52 frame=%u values=%zu baseline_diff=%zu fast_diff=%zu bit_diff=%zu\n",frame,values.size(),baseline_diff,fast_diff,bit_diff);fflush(stdout);if(baseline_diff||fast_diff||bit_diff)throw std::runtime_error("quantizer core mismatch");
 }delete fast;delete legacy;return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
