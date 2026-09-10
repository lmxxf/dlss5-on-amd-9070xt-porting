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
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);
 /* DLSS5_TEST_PRESENT=1 (profiling only): a hidden 64x64 window with a swapchain on the same queue, Present() once per frame, so that
    frame-boundary profilers (Radeon GPU Profiler in frame capture mode) see the whole network as one frame. No effect on the network. */
 IDXGISwapChain3*present_chain=nullptr;
 if(const wchar_t*pr=_wgetenv(L"DLSS5_TEST_PRESENT")){if(wcscmp(pr,L"0")&&wcscmp(pr,L"1"))throw std::runtime_error("invalid present flag");if(!wcscmp(pr,L"1")){
  /* composition swapchain: no window needed (the bench usually runs from an SSH session without an interactive desktop) */
  DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=64;sd.Height=64;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;sd.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
  IDXGISwapChain1*sc1=nullptr;HRESULT sh=f->CreateSwapChainForComposition(q,&sd,nullptr,&sc1);if(FAILED(sh))throw std::runtime_error("present swapchain HRESULT="+std::to_string(unsigned(sh)));ck(sc1->QueryInterface(IID_PPV_ARGS(&present_chain)));sc1->Release();std::printf("present_chain=1\n");}}
 q->Release();
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
  submit.Flush();if(present_chain)ck(present_chain->Present(0,0));
  std::printf("network70 frame=%u single_list=%u deferred=%u submit_wait_ms=%.3f flushed_ms=%.3f\n",frame,single_list,submit.Deferred(),recorded_ms,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());std::fflush(stdout);
  submit.Submit([&](ID3D12GraphicsCommandList*c){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={network->Output(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);c->CopyBufferRegion(rb,0,network->Output(),0,oracle.size()*4);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);});
  submit.Flush();
  void*p=nullptr;D3D12_RANGE range{0,oracle.size()*4},none{};ck(rb->Map(0,&range,&p));auto*actual=static_cast<const float*>(p);size_t different=0;
  for(size_t i=0;i<oracle.size();i++)different+=!std::isfinite(actual[i])||actual[i]!=expected[i];
  // Determinism probe: bitwise compare against the previous frame with the same history state (same inputs, same seed).
  {static std::vector<float>previous[2];auto&prev=previous[enabled?1:0];if(prev.size()==oracle.size()){size_t unstable=0;for(size_t i=0;i<oracle.size();i++)unstable+=actual[i]!=prev[i];std::printf("network70 frame=%u history=%u frame_to_frame_different=%zu\n",frame,enabled,unstable);}prev.assign(actual,actual+oracle.size());}
  std::ofstream out((dir+(enabled?L"\\gpu-network70-temporal.f32":L"\\gpu-network70.f32")).c_str(),std::ios::binary);if(!out.write(reinterpret_cast<const char*>(p),oracle.size()*4))throw std::runtime_error("readback save failed");rb->Unmap(0,&none);
  std::printf("network70 frame=%u history=%u values=%zu different=%zu\n",frame,enabled,oracle.size(),different);std::fflush(stdout);if(different&&!allow_inexact)throw std::runtime_error("extracted network differs");memory_report(frame+1);
  if(frame+1==frames&&network->Isolating())network->Isolate(submit,0);
  if(frame+1==frames&&_wgetenv(L"DLSS5_TEST_DUMP_BLOCK4")){ // test only: dump block 4's main8 (E4M3 raster) or raw tiles (f32, tile-major) for CPU comparison
   const bool p66=!wcscmp(_wgetenv(L"DLSS5_TEST_DUMP_BLOCK4"),L"2");const bool down4=!wcscmp(_wgetenv(L"DLSS5_TEST_DUMP_BLOCK4"),L"3"),dec=!wcscmp(_wgetenv(L"DLSS5_TEST_DUMP_BLOCK4"),L"4");const bool hd=!wcscmp(_wgetenv(L"DLSS5_TEST_DUMP_BLOCK4"),L"5");const bool sr=!wcscmp(_wgetenv(L"DLSS5_TEST_DUMP_BLOCK4"),L"6");const bool sf=!wcscmp(_wgetenv(L"DLSS5_TEST_DUMP_BLOCK4"),L"7");const bool pd=!wcscmp(_wgetenv(L"DLSS5_TEST_DUMP_BLOCK4"),L"8"),pm=!wcscmp(_wgetenv(L"DLSS5_TEST_DUMP_BLOCK4"),L"9");const bool b69=!wcscmp(_wgetenv(L"DLSS5_TEST_DUMP_BLOCK4"),L"10");ID3D12Resource*src=b69?network->Block69Main8():pd?network->PreDown():pm?network->PreMain8():sf?network->SharedFfnScratch():sr?network->SharedRawScratch():hd?network->Head():dec?network->Decoder69():down4?network->Block4Down():p66?network->Project66Output():network->Block4Main8()?network->Block4Main8():network->Block4Main();UINT64 bytes=src->GetDesc().Width;auto*rb4=buf(d,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
   submit.Submit([&](ID3D12GraphicsCommandList*c){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={src,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);c->CopyBufferRegion(rb4,0,src,0,bytes);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);});submit.Flush();
   void*p4=nullptr;D3D12_RANGE r4{0,size_t(bytes)},none4{};ck(rb4->Map(0,&r4,&p4));std::ofstream o4((dir+(b69?L"\\block69-main8.bin":pd?L"\\pre-down.f32":pm?L"\\pre-main8.bin":sf?L"\\sharedffn.f32":sr?L"\\sharedraw.f32":hd?L"\\head.f32":dec?L"\\decoder69.f32":down4?L"\\block4-down.f32":p66?L"\\project66.f32":network->Block4Main8()?L"\\block4-main8.bin":L"\\block4-main.f32")).c_str(),std::ios::binary);o4.write(static_cast<const char*>(p4),std::streamsize(bytes));rb4->Unmap(0,&none4);rb4->Release();std::printf("block4_dump bytes=%llu main8=%u\n",(unsigned long long)bytes,network->Block4Main8()?1u:0u);std::fflush(stdout);
  }
 }
 if(present_chain){std::printf("present linger 30s (profiler transfer)\n");std::fflush(stdout);for(int i=0;i<300;i++){ck(present_chain->Present(0,0));Sleep(100);}present_chain->Release();}if(memory_adapter)memory_adapter->Release();delete network;delete sampler;delete coordinates;delete reflect;std::printf("extracted_network70=exact frames=%u; controlled history; game integration pending\n",frames);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
