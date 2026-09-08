#define wmain unused_texture_test_wmain
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_actual_network70.h"
#include "native_temporal_coordinates.h"
#include "native_temporal_sample.h"
#ifdef MATRIX_BENCH
extern "C" {__declspec(dllexport) extern const UINT D3D12SDKVersion=721;__declspec(dllexport) const char*D3D12SDKPath=".\\D3D12\\";}
#endif

int wmain(int argc,wchar_t**argv){try{
#ifdef MATRIX_BENCH
 const IID experimental={0x76f5573e,0xf13a,0x40f5,{0xb2,0x97,0x81,0xce,0x9e,0x18,0x93,0x3f}};ck(D3D12EnableExperimentalFeatures(1,&experimental,nullptr,nullptr));
#endif
 if(argc!=3)return 2;std::wstring dir=argv[1];
 const wchar_t*single_setting=_wgetenv(L"DLSS5_TEST_SINGLE_LIST");
 if(single_setting&&wcscmp(single_setting,L"1"))throw std::runtime_error("invalid single-list test flag");
 const bool single_list=single_setting!=nullptr;
 std::printf("network70 single_list=%u\n",single_list);std::fflush(stdout);
 UINT post_shift=0;
 if(const wchar_t*s=_wgetenv(L"DLSS5_TEST_POST_SHIFT")){
  if(s[0]<L'0'||s[0]>L'3'||s[1])throw std::runtime_error("invalid post shift");
  post_shift=UINT(s[0]-L'0');
 }
 std::printf("network70 post_shift=%u\n",post_shift);std::fflush(stdout);
 // Fast path (hardware arithmetic differences): keep running and report the value count; error metrics come from compare_fast_output.py.
 const wchar_t*inexact=_wgetenv(L"DLSS5_TEST_ALLOW_INEXACT");if(inexact&&wcscmp(inexact,L"0")&&wcscmp(inexact,L"1"))throw std::runtime_error("invalid allow-inexact flag");const bool allow_inexact=inexact&&!wcscmp(inexact,L"1");
 std::printf("network70 allow_inexact=%u\n",allow_inexact);std::fflush(stdout);
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
 auto*network=new NativeActualNetwork70;network->Create(d,reflect->Output(),base,noise,dir,sampler?sampler->Output():nullptr,post_shift);
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);q->Release();
 auto*rb=buf(d,oracle.size()*4,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 UINT frames=temporal?5:3;
 if(const wchar_t*s=_wgetenv(L"DLSS5_TEST_FRAME_COUNT")){wchar_t*end=nullptr;auto n=wcstoul(s,&end,10);if(!*s||*end||n<5||n>30)throw std::runtime_error("frame count must be 5..30");frames=UINT(n);}
 IDXGIAdapter3*memory_adapter=nullptr;
 if(const wchar_t*s=_wgetenv(L"DLSS5_TEST_MEMORY_BUDGET")){
  if(wcscmp(s,L"1"))throw std::runtime_error("invalid memory budget flag");
  ck(f->EnumAdapterByLuid(d->GetAdapterLuid(),IID_PPV_ARGS(&memory_adapter)));
 }
 auto memory_report=[&](UINT frame){if(!memory_adapter)return;for(UINT segment=0;segment<2;segment++){DXGI_QUERY_VIDEO_MEMORY_INFO info{};ck(memory_adapter->QueryVideoMemoryInfo(0,segment?DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL:DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info));printf("memory_budget frame=%u segment=%u usage=%llu budget=%llu reservation=%llu\n",frame,segment,info.CurrentUsage,info.Budget,info.CurrentReservation);}fflush(stdout);};
 memory_report(0);
 for(UINT frame=0;frame<frames;frame++){
  const bool enabled=temporal&&frame%2==1;
  const auto&expected=enabled?temporal_oracle:oracle;
  const auto started=std::chrono::steady_clock::now();
  submit.Submit([&](ID3D12GraphicsCommandList*c){reflect->Record(c);if(enabled){coordinates->Record(c);sampler->Record(c);}if(single_list)network->RecordUnsubmitted(c,0,enabled);});
  if(!single_list)network->Run(submit,0,enabled);
  const double recorded_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
  submit.Flush();
  std::printf("network70 frame=%u single_list=%u deferred=%u submit_wait_ms=%.3f flushed_ms=%.3f\n",frame,single_list,submit.Deferred(),recorded_ms,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());std::fflush(stdout);
  submit.Submit([&](ID3D12GraphicsCommandList*c){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={network->Output(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);c->CopyBufferRegion(rb,0,network->Output(),0,oracle.size()*4);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);});
  submit.Flush();
  void*p=nullptr;D3D12_RANGE range{0,oracle.size()*4},none{};ck(rb->Map(0,&range,&p));auto*actual=static_cast<const float*>(p);size_t different=0;
  for(size_t i=0;i<oracle.size();i++)different+=!std::isfinite(actual[i])||actual[i]!=expected[i];
  std::ofstream out((dir+(enabled?L"\\gpu-network70-temporal.f32":L"\\gpu-network70.f32")).c_str(),std::ios::binary);if(!out.write(reinterpret_cast<const char*>(p),oracle.size()*4))throw std::runtime_error("readback save failed");rb->Unmap(0,&none);
  std::printf("network70 frame=%u history=%u values=%zu different=%zu\n",frame,enabled,oracle.size(),different);std::fflush(stdout);if(different&&!allow_inexact)throw std::runtime_error("extracted network differs");memory_report(frame+1);
 }
 if(memory_adapter)memory_adapter->Release();delete network;delete sampler;delete coordinates;delete reflect;std::printf("extracted_network70=exact frames=%u; controlled history; game integration pending\n",frames);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
