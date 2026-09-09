#define wmain unused_input_test
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_temporal_coordinates.h"
#include "native_game_submission.h"
int wmain(int argc,wchar_t**argv){try{
 if(argc!=2)return 2;std::wstring dir=argv[1];auto read=[&](const wchar_t*name,size_t count){std::ifstream f((dir+L"\\"+name).c_str(),std::ios::binary|std::ios::ate);if(!f||f.tellg()!=std::streamoff(count*4))throw std::runtime_error("fixture size");std::vector<float>v(count);f.seekg(0);if(!f.read((char*)v.data(),count*4))throw std::runtime_error("fixture read");return v;};auto motion=read(L"motion.f32",256),oracle=read(L"oracle.f32",64);
 IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 info{};a->GetDesc1(&info);if(info.VendorId==0x1002){ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();break;}a->Release();}if(!d)throw std::runtime_error("AMD missing");
 auto*input=buf(d,1024,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p;D3D12_RANGE none{};ck(input->Map(0,&none,&p));memcpy(p,motion.data(),1024);input->Unmap(0,nullptr);auto*rb=buf(d,512,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 NativeTemporalCoordinates pass;const float transform[]={0,0,8,8,.125f,.125f};pass.Create(d,input,8,8,8,8,8,8,transform,dir);
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);
 for(UINT frame=0;frame<3;frame++){
  submit.Submit([&](ID3D12GraphicsCommandList*c){pass.Record(c);D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={pass.Output(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);c->CopyBufferRegion(rb,0,pass.Output(),0,512);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);});
  D3D12_RANGE range{0,512};ck(rb->Map(0,&range,&p));auto*a=static_cast<float*>(p);size_t different=0;for(UINT i=0;i<128;i++)if(!std::isfinite(a[i]))throw std::runtime_error("nonfinite coordinates");for(UINT i=0;i<64;i++)different+=a[i]!=oracle[i];std::ofstream out((dir+L"\\gpu.f32").c_str(),std::ios::binary);if(!out.write((char*)p,512))throw std::runtime_error("save");rb->Unmap(0,&none);printf("frame=%u original_warp0_values=64 different=%zu\n",frame,different);fflush(stdout);if(different)throw std::runtime_error("motion coordinates differ");
 }return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
