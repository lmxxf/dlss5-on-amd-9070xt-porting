#define wmain unused_texture_test_wmain
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_actual_network70.h"
#include "native_temporal_coordinates.h"
#include "native_temporal_sample.h"

int wmain(int argc,wchar_t**argv){try{
 if(argc!=3)return 2;std::wstring dir=argv[1];
 auto read=[](const std::wstring&path){std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("fixture missing");auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error("fixture size");std::vector<float>v(size_t(n)/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(v.data()),n))throw std::runtime_error("fixture truncated");return v;};
 auto rgb=read(dir+L"\\input.f32"),oracle=read(dir+L"\\oracle-final.f32"),noise=read(argv[2]);
 const wchar_t*history_path=_wgetenv(L"DLSS5_TEST_TEMPORAL_HISTORY");
 const wchar_t*motion_path=_wgetenv(L"DLSS5_TEST_TEMPORAL_MOTION");
 const wchar_t*reciprocal_path=_wgetenv(L"DLSS5_TEST_RECIPROCAL_TABLE");
 const wchar_t*temporal_oracle_path=_wgetenv(L"DLSS5_TEST_TEMPORAL_ORACLE");
 const bool temporal=history_path!=nullptr;
 if((history_path||motion_path||reciprocal_path||temporal_oracle_path)&&!(history_path&&motion_path&&reciprocal_path&&temporal_oracle_path))throw std::runtime_error("temporal test requires all inputs and independent final oracle");
 std::vector<float>history,motion,reciprocals,temporal_oracle;
 if(temporal){history=read(history_path);motion=read(motion_path);reciprocals=read(reciprocal_path);temporal_oracle=read(temporal_oracle_path);
  if(history.size()!=1920ull*1080*4||motion.size()!=history.size()||reciprocals.size()!=8388608||temporal_oracle.size()!=oracle.size())throw std::runtime_error("temporal fixture geometry");
  for(const auto*v:{&history,&motion,&reciprocals,&temporal_oracle})for(float x:*v)if(!std::isfinite(x))throw std::runtime_error("nonfinite temporal fixture");
 }
 if(rgb.size()!=1920ull*1080*4||oracle.size()!=1920ull*1152*3)throw std::runtime_error("fixture geometry");
 for(float x:rgb)if(!std::isfinite(x))throw std::runtime_error("nonfinite RGB");for(float x:oracle)if(!std::isfinite(x))throw std::runtime_error("nonfinite oracle");
 IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 info{};a->GetDesc1(&info);if(info.VendorId==0x1002){ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();break;}a->Release();}if(!d)throw std::runtime_error("AMD missing");
 auto upload=[&](const std::vector<float>&v){auto*r=buf(d,v.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};ck(r->Map(0,&none,&p));std::memcpy(p,v.data(),v.size()*4);r->Unmap(0,nullptr);return r;};
 auto*input=upload(rgb);std::vector<float>color(1920ull*1152*4);
 for(UINT y=0;y<1152;y++){UINT r=y%2158,sy=r<1080?r:2158-r;std::memcpy(color.data()+size_t(y)*1920*4,rgb.data()+size_t(sy)*1920*4,1920*16);}auto*base=upload(color);
 auto*reflect=new NativeRgbReflect;reflect->Create(d,input,1920,1080,1920,1152,dir);
 NativeTemporalCoordinates*coordinates=nullptr;NativeTemporalSample*sampler=nullptr;
 if(temporal){
  auto*mv=upload(motion);auto*h=upload(history);auto*rcp=upload(reciprocals);
  coordinates=new NativeTemporalCoordinates;const float transform[]={0,0,1920,1080,1.f/1920,1.f/1080};
  coordinates->Create(d,mv,1920,1080,1920,1152,1920,1080,transform,dir,true);
  sampler=new NativeTemporalSample;sampler->Create(d,h,coordinates->Output(),1920,1080,1920*1152,dir,true,rcp);
  mv->Release();h->Release();rcp->Release();
 }
 // Keep the whole network alive on failure: timeout is not GPU cancellation.
 auto*network=new NativeActualNetwork70;network->Create(d,reflect->Output(),base,noise,dir,sampler?sampler->Output():nullptr);
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);q->Release();
 auto*rb=buf(d,oracle.size()*4,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 const UINT frames=temporal?5:3;
 for(UINT frame=0;frame<frames;frame++){
  const bool enabled=temporal&&frame%2==1;
  const auto&expected=enabled?temporal_oracle:oracle;
  submit.Submit([&](ID3D12GraphicsCommandList*c){reflect->Record(c);if(enabled){coordinates->Record(c);sampler->Record(c);}});network->Run(submit,0,enabled);
  submit.Submit([&](ID3D12GraphicsCommandList*c){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={network->Output(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);c->CopyBufferRegion(rb,0,network->Output(),0,oracle.size()*4);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);});
  void*p=nullptr;D3D12_RANGE range{0,oracle.size()*4},none{};ck(rb->Map(0,&range,&p));auto*actual=static_cast<const float*>(p);size_t different=0;
  for(size_t i=0;i<oracle.size();i++)different+=!std::isfinite(actual[i])||actual[i]!=expected[i];
  std::ofstream out((dir+(enabled?L"\\gpu-network70-temporal.f32":L"\\gpu-network70.f32")).c_str(),std::ios::binary);if(!out.write(reinterpret_cast<const char*>(p),oracle.size()*4))throw std::runtime_error("readback save failed");rb->Unmap(0,&none);
  std::printf("network70 frame=%u history=%u values=%zu different=%zu\n",frame,enabled,oracle.size(),different);std::fflush(stdout);if(different)throw std::runtime_error("extracted network differs");
 }
 delete network;delete sampler;delete coordinates;delete reflect;std::printf("extracted_network70=exact frames=%u; controlled history; game integration pending\n",frames);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
