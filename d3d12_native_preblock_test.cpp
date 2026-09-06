#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cmath>
#include <cstdint>
#include "native_preblock_runtime.h"
#include "native_rgb_reflect.h"
#include "native_c32_stage.h"
#include "native_c32_ds.h"
#include "native_c64_shift.h"
#include "native_split_window.h"
#include "native_vit_gather.h"
#include "native_vit_block.h"
#include "native_actual_decoder69.h"
#include "native_post70.h"
#include "native_temporal_sample.h"
#include "native_temporal_coordinates.h"
static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D HRESULT="+std::to_string(unsigned(h)));}
static std::vector<float> read(const wchar_t*p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("input file missing");auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error("input size");std::vector<float>v(size_t(n)/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(v.data()),n))throw std::runtime_error("input truncated");return v;}
static ID3D12Resource* buffer(ID3D12Device*d,UINT64 n,D3D12_HEAP_TYPE t,D3D12_RESOURCE_STATES s){D3D12_HEAP_PROPERTIES hp{};hp.Type=t;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=n;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,s,nullptr,IID_PPV_ARGS(&r)));return r;}
static void barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&x);}
int wmain(int argc,wchar_t**argv){try{
 if(argc<8||argc>10){std::fwprintf(stderr,L"usage: %ls ffn.f32 attention.f32 input.rgba32f main.f32 down.f32 raw.f32 shader-dir [live [global]]\n",argv[0]);return 2;}
 const bool live=argc>=9&&!wcscmp(argv[8],L"live"),raw_features=argc>=9&&!wcscmp(argv[8],L"c32");if(argc>=9&&!live&&!raw_features)return 2;
 const bool global=argc==10&&!wcscmp(argv[9],L"global");if(argc==10&&!global)return 2;
 auto fw=read(argv[1]),aw=read(argv[2]),rgb=read(argv[3]);const UINT width=std::getenv("DLSS5_TEST_WIDTH")?std::atoi(std::getenv("DLSS5_TEST_WIDTH")):128,channels=raw_features?32:4;if(!width)throw std::runtime_error("width zero");UINT height=UINT(rgb.size()/channels/width);if(rgb.size()!=size_t(width)*height*channels||height%8)throw std::runtime_error("fixture geometry");
 const bool reflect_input=_wgetenv(L"DLSS5_REFLECT_VALID1080")!=nullptr;if(reflect_input){if(!live||!global||width!=1920||height!=1080)throw std::runtime_error("reflect test contract");height=1152;}
 IDXGIFactory6*factory=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));IDXGIAdapter1*adapter=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*c=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&c))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};c->GetDesc1(&desc);if(desc.VendorId==0x1002&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=c;std::wprintf(L"adapter=%ls\n",desc.Description);break;}c->Release();}
 if(!adapter)throw std::runtime_error("AMD adapter unavailable");ID3D12Device*device=nullptr;ck(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
 auto*input=buffer(device,rgb.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*m=nullptr;D3D12_RANGE none{};ck(input->Map(0,&none,&m));std::memcpy(m,rgb.data(),rgb.size()*4);input->Unmap(0,nullptr);
 std::vector<float>noise_table;const wchar_t*noise_path=_wgetenv(L"DLSS5_NOISE_TABLE");if(noise_path)noise_table=read(noise_path);
 NativeRgbReflect reflect;if(reflect_input){if(!noise_path)throw std::runtime_error("reflect test requires native noise table");reflect.Create(device,input,1920,1080,1920,1152,argv[7]);}
 const wchar_t*temporal_path=_wgetenv(L"DLSS5_TEST_TEMPORAL_RGB");ID3D12Resource*temporal_input=nullptr;
 if(temporal_path){auto values=read(temporal_path);if(values.size()!=size_t(width)*height*4||!live||raw_features)throw std::runtime_error("temporal test geometry");for(float v:values)if(!std::isfinite(v))throw std::runtime_error("nonfinite temporal test input");temporal_input=buffer(device,values.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);ck(temporal_input->Map(0,&none,&m));std::memcpy(m,values.data(),values.size()*4);temporal_input->Unmap(0,nullptr);}
 NativeTemporalSample temporal_sampler;NativeTemporalCoordinates motion_coordinates;const wchar_t*history_path=_wgetenv(L"DLSS5_TEST_TEMPORAL_HISTORY");const wchar_t*motion_path=_wgetenv(L"DLSS5_TEST_TEMPORAL_MOTION");
 const UINT temporal_height=reflect_input?1080:height;
 if(motion_path&&!history_path)throw std::runtime_error("motion test requires history");
 if(history_path){
  if(temporal_input||!live||raw_features)throw std::runtime_error("ambiguous temporal test input");auto values=read(history_path);if(values.size()!=size_t(width)*temporal_height*4)throw std::runtime_error("history fixture shape");
  auto upload=[&](const std::vector<float>&v){for(float x:v)if(!std::isfinite(x))throw std::runtime_error("nonfinite temporal fixture");auto*r=buffer(device,v.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;ck(r->Map(0,&none,&p));std::memcpy(p,v.data(),v.size()*4);r->Unmap(0,nullptr);return r;};
  auto*h=upload(values);ID3D12Resource*c=nullptr;
  if(motion_path){auto vectors=read(motion_path);if(vectors.size()!=size_t(width)*temporal_height*4)throw std::runtime_error("motion fixture shape");auto*mv=upload(vectors);const float transform[]={0,0,float(width),float(temporal_height),1.f/width,1.f/temporal_height};motion_coordinates.Create(device,mv,width,temporal_height,width,height,width,temporal_height,transform,argv[7],true);mv->Release();c=motion_coordinates.Output();c->AddRef();}
  else{auto xy=read((std::wstring(argv[7])+L"\\temporal-coordinates.f32").c_str());if(xy.size()!=size_t(width)*height*2)throw std::runtime_error("coordinate fixture shape");c=upload(xy);}
  temporal_sampler.Create(device,h,c,width,temporal_height,width*height,argv[7],motion_path!=nullptr);h->Release();c->Release();temporal_input=temporal_sampler.Output();temporal_input->AddRef();
 }
 const bool use_temporal=temporal_input!=nullptr;
 NativePreblockRuntime block;block.Create(device,reflect_input?reflect.Output():input,width,height,fw,aw,argv[7],live,raw_features,noise_path?&noise_table:nullptr,temporal_input);if(temporal_input)temporal_input->Release();
 const bool frontfinal=_wgetenv(L"DLSS5_FRONTFINAL")!=nullptr;const bool frontdecoder=frontfinal||_wgetenv(L"DLSS5_FRONTDECODER")!=nullptr;const bool frontvit=frontdecoder||_wgetenv(L"DLSS5_FRONTVIT")!=nullptr;const bool fronthead=frontvit||_wgetenv(L"DLSS5_FRONTHEAD")!=nullptr;const bool front22=fronthead||_wgetenv(L"DLSS5_FRONT22")!=nullptr;const bool front14=front22||_wgetenv(L"DLSS5_FRONT14")!=nullptr;const bool front8=front14||_wgetenv(L"DLSS5_FRONT8")!=nullptr;const bool front4=front8||_wgetenv(L"DLSS5_FRONT4")!=nullptr;if(front4&&!reflect_input)throw std::runtime_error("front4 requires reflected valid RGB");NativeC32Stage stages[4];NativeC32Downsample ds;
 if(front4){auto*source=block.Downsample();const UINT shifts[]={0,3,1,2};for(UINT i=0;i<4;i++){auto prefix=std::wstring(argv[7])+L"\\block"+std::to_wstring(i+1);stages[i].Create(device,source,960,576,shifts[i],read((prefix+L"-ffn.f32").c_str()),read((prefix+L"-attention.f32").c_str()),argv[7]);source=stages[i].Output();}ds.Create(device,stages[3].PooledWork(),960,576,2,read((std::wstring(argv[7])+L"\\block4-ds.f32").c_str()),argv[7]);}
 NativeC64Shift c64[4];NativeC32Downsample ds8;if(front8){auto*source=ds.Output();const UINT shifts[]={0,3,1,2};for(UINT i=0;i<4;i++){auto prefix=std::wstring(argv[7])+L"\\block"+std::to_wstring(i+5);c64[i].Create(device,source,480,288,shifts[i],read((prefix+L"-ffn.f32").c_str()),read((prefix+L"-attention.f32").c_str()),argv[7],i==3,64);source=c64[i].Output();}ds8.Create(device,source,480,288,0,read((std::wstring(argv[7])+L"\\block8-ds.f32").c_str()),argv[7],true,64);}
 NativeC64Shift c128[6];NativeC32Downsample ds14;if(front14){auto*source=ds8.Output();const UINT shifts[]={0,3,1,2,0,3};for(UINT i=0;i<6;i++){auto prefix=std::wstring(argv[7])+L"\\block"+std::to_wstring(i+9);c128[i].Create(device,source,240,144,shifts[i],read((prefix+L"-ffn.f32").c_str()),read((prefix+L"-attention.f32").c_str()),argv[7],i==5,128);source=c128[i].Output();}ds14.Create(device,source,240,144,0,read((std::wstring(argv[7])+L"\\block14-ds.f32").c_str()),argv[7],true,128);}
 NativeC64Shift c256[8];NativeC32Downsample ds22;if(front22){auto*source=ds14.Output();const UINT shifts[]={0,3,1,2,0,3,1,2};for(UINT i=0;i<8;i++){auto prefix=std::wstring(argv[7])+L"\\block"+std::to_wstring(i+15);c256[i].Create(device,source,120,72,shifts[i],read((prefix+L"-ffn.f32").c_str()),read((prefix+L"-attention.f32").c_str()),argv[7],i==7,256);source=c256[i].Output();}ds22.Create(device,source,120,72,0,read((std::wstring(argv[7])+L"\\block22-ds.f32").c_str()),argv[7],true,256);}
 NativeSplitWindow split[8];NativeC32Downsample head;
 if(fronthead){auto*source=ds22.Output();const UINT shifts[]={0,3,1,2,0,3,1,2};for(UINT i=0;i<8;i++){auto prefix=std::wstring(argv[7])+L"\\block"+std::to_wstring(i+23);split[i].Create(device,source,60,36,shifts[i],read((prefix+L"-ffwd.f32").c_str()),read((prefix+L"-ffwd-projection.f32").c_str()),read((prefix+L"-attention.f32").c_str()),argv[7],i==7);source=split[i].Output();}head.Create(device,source,60,36,0,read((std::wstring(argv[7])+L"\\head-matrix.f32").c_str()),argv[7],true,512);}
 D3D12_COMMAND_QUEUE_DESC qd{};ID3D12CommandQueue*q=nullptr;ck(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ID3D12CommandAllocator*allocator=nullptr;ck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));ID3D12GraphicsCommandList*c=nullptr;ck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&c)));
 ID3D12Fence*fence=nullptr;ck(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 UINT64 sizes[]={UINT64(width)*height*32*4,UINT64(width)*height*8*4,UINT64(width)*height*32*4};ID3D12Resource*outputs[]={front4?stages[3].Output():block.Main(),front4?ds.Output():block.Downsample(),block.RawTiles()};if(front4){sizes[0]=UINT64(960)*576*32*4;sizes[1]=UINT64(480)*288*64*4;}if(front8){outputs[0]=c64[3].Output();outputs[1]=ds8.Output();sizes[0]=UINT64(480)*288*64*4;sizes[1]=UINT64(240)*144*128*4;}if(front14){outputs[0]=c128[5].Output();outputs[1]=ds14.Output();sizes[0]=UINT64(240)*144*128*4;sizes[1]=UINT64(120)*72*256*4;}if(front22){outputs[0]=c256[7].Output();outputs[1]=ds22.Output();sizes[0]=UINT64(120)*72*256*4;sizes[1]=UINT64(60)*36*512*4;}ID3D12Resource*readback[3]{};std::vector<float>data[3],baseline[3];for(UINT i=0;i<3;i++){readback[i]=buffer(device,sizes[i],D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);data[i].resize(sizes[i]/4);}
 if(fronthead){outputs[0]=split[7].Output();outputs[1]=head.Output();sizes[0]=UINT64(60)*36*512*4;sizes[1]=UINT64(32)*20*1024*4;for(UINT i=0;i<2;i++){readback[i]->Release();readback[i]=buffer(device,sizes[i],D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);data[i].resize(sizes[i]/4);}}
 NativeVitGather bridge;NativeVitBlock vit[8];
 if(frontvit){auto packed=read((std::wstring(argv[7])+L"\\hwc-to-vit.i32").c_str());std::vector<UINT>map(packed.size());std::memcpy(map.data(),packed.data(),packed.size()*4);bridge.Create(device,head.Output(),map,argv[7]);auto*source=bridge.Output();for(UINT i=0;i<8;i++){auto prefix=std::wstring(argv[7])+L"\\block"+std::to_wstring(31+i)+L"-";vit[i].Create(device,source,640,read((prefix+L"expand.f32").c_str()),read((prefix+L"contract.f32").c_str()),read((prefix+L"qkv.f32").c_str()),read((prefix+L"projection.f32").c_str()),argv[7]);source=vit[i].Output();}outputs[0]=source;sizes[0]=655360ull*4;readback[0]->Release();readback[0]=buffer(device,sizes[0],D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);data[0].resize(sizes[0]/4);}
 NativeActualDecoder69 decoder;NativePost70 final_post;ID3D12Resource*post_color=nullptr;
 const wchar_t*alternate_path=_wgetenv(L"DLSS5_ALTERNATE_RGB");std::vector<float>alternate;
 if(alternate_path){if(!frontfinal)throw std::runtime_error("alternate RGB requires full final chain");alternate=read(alternate_path);if(alternate.size()!=rgb.size()||alternate==rgb)throw std::runtime_error("alternate RGB must differ with same geometry");for(float v:alternate)if(!std::isfinite(v))throw std::runtime_error("nonfinite alternate RGB");}
 if(frontdecoder){decoder.Create(device,vit[7].Output(),split[7].Output(),c256[7].Output(),c128[5].Output(),c64[3].Output(),stages[3].Output(),argv[7]);outputs[0]=decoder.Output();sizes[0]=960ull*576*32*4;}
 if(frontfinal){
  if(_wgetenv(L"DLSS5_POST_BASE_ONLY"))throw std::runtime_error("diagnostic post cannot validate final chain");
  // The post base is the SAME input RGB, reflected in HWC rather than tiled.
  std::vector<float>color(1920ull*1152*4);for(UINT y=0;y<1152;y++){UINT sy=y<1080?y:2158-y;std::memcpy(color.data()+size_t(y)*1920*4,rgb.data()+size_t(sy)*1920*4,1920*4*sizeof(float));}
  auto*base=buffer(device,color.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;ck(base->Map(0,&none,&p));std::memcpy(p,color.data(),color.size()*4);base->Unmap(0,nullptr);
  auto coeff=[&](const wchar_t*name){return read((std::wstring(argv[7])+L"\\post70-"+name+L".f32").c_str());};
  final_post.Create(device,decoder.Output(),block.Main(),base,1920,1152,coeff(L"scales"),coeff(L"ffn"),coeff(L"attention"),coeff(L"head"),argv[7]);post_color=base;base->Release();outputs[0]=final_post.Output();sizes[0]=1920ull*1152*3*4;
 }
 if(frontdecoder){readback[0]->Release();readback[0]=buffer(device,sizes[0],D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);data[0].resize(sizes[0]/4);}
 if(alternate_path){outputs[2]=decoder.Output();sizes[2]=960ull*576*32*4;readback[2]->Release();readback[2]=buffer(device,sizes[2],D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);data[2].resize(sizes[2]/4);}
 UINT64 fence_value=0;
 auto submit=[&](){ck(c->Close());ID3D12CommandList*lists[]={c};q->ExecuteCommandLists(1,lists);ck(q->Signal(fence,++fence_value));ck(fence->SetEventOnCompletion(fence_value,event));if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");ck(device->GetDeviceRemovedReason());auto completed=fence->GetCompletedValue();if(completed==UINT64_MAX||completed<fence_value)throw std::runtime_error("invalid fence completion");};
 auto flush=[&](){submit();ck(allocator->Reset());ck(c->Reset(allocator,nullptr));};
 const UINT base_seed=live?(std::getenv("DLSS5_TEST_SEED")?UINT(std::strtoul(std::getenv("DLSS5_TEST_SEED"),nullptr,0)):0):0x3f800000;
 for(UINT frame=0;frame<5;frame++){
  if(frame){ck(allocator->Reset());ck(c->Reset(allocator,nullptr));}
  if(alternate_path&&(frame==2||frame==3)){
   const auto&chosen=frame==2?alternate:rgb;void*p=nullptr;
   // Previous frame has completed its final fence before either upload changes.
   ck(input->Map(0,&none,&p));std::memcpy(p,chosen.data(),chosen.size()*4);input->Unmap(0,nullptr);
   ck(post_color->Map(0,&none,&p));for(UINT y=0;y<1152;y++){UINT sy=y<1080?y:2158-y;std::memcpy(static_cast<float*>(p)+size_t(y)*1920*4,chosen.data()+size_t(sy)*1920*4,1920*4*sizeof(float));}post_color->Unmap(0,nullptr);
  }
  UINT seed=alternate_path?base_seed:live?(base_seed^(frame==3?1u:0u)):(frame==3?0x12345678:base_seed);if(reflect_input)reflect.Record(c);if(motion_path)motion_coordinates.Record(c);if(history_path)temporal_sampler.Record(c);block.Record(c,seed,!global,use_temporal);if(front4){for(auto&stage:stages)stage.Record(c);ds.Record(c);}if(front8){for(auto&stage:c64)stage.Record(c);ds8.Record(c);}if(front14){for(auto&stage:c128)stage.Record(c);ds14.Record(c);}if(front22){for(auto&stage:c256)stage.Record(c);ds22.Record(c);}
  if(fronthead){for(auto&stage:split)stage.Record(c);head.Record(c);}
  if(frontvit){bridge.Record(c);flush();for(auto&layer:vit)for(UINT stage=0;stage<5;stage++)for(UINT chunk=0;chunk<layer.StageChunks(stage);chunk++){layer.RecordStageChunk(c,stage,chunk);flush();}}
  if(frontdecoder)for(UINT stage=0;stage<decoder.StageCount();stage++){decoder.RecordStage(c,stage);flush();}
  if(frontfinal)final_post.Record(c);
  for(UINT i=0;i<3;i++){barrier(c,outputs[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);c->CopyBufferRegion(readback[i],0,outputs[i],0,sizes[i]);barrier(c,outputs[i],D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
  submit();
  for(UINT i=0;i<3;i++){D3D12_RANGE range{0,SIZE_T(sizes[i])};ck(readback[i]->Map(0,&range,&m));std::memcpy(data[i].data(),m,sizes[i]);readback[i]->Unmap(0,&none);for(float x:data[i])if(!std::isfinite(x))throw std::runtime_error("nonfinite output");}
  if(frame==0){for(UINT i=0;i<3;i++)baseline[i]=data[i];}
  else if(alternate_path&&frame==2){for(UINT i=0;i<3;i++){
   size_t different=0;for(size_t j=0;j<data[i].size();j++)different+=data[i][j]!=baseline[i][j];
   if(!different)throw std::runtime_error("alternate RGB failed to change final/head/decoder output");
   std::printf("dynamic_delta output=%u values=%zu different=%zu\n",i,data[i].size(),different);
   std::ofstream f((std::wstring(argv[4+i])+L".alternate").c_str(),std::ios::binary);if(!f.write(reinterpret_cast<const char*>(data[i].data()),sizes[i]))throw std::runtime_error("alternate output write failed");
  }}
  else if(!alternate_path&&frame==3&&!raw_features){if(data[0]==baseline[0])throw std::runtime_error("seed has no effect");}
  else for(UINT i=0;i<3;i++)if(data[i]!=baseline[i])throw std::runtime_error("persistent-resource replay changed output");
  std::printf("frame=%u seed=%08x replay_check=pass\n",frame,seed);
  if(alternate_path)std::printf("dynamic_rgb frame=%u input=%c checked=final,head,decoder69\n",frame,frame==2?'B':'A');
 }
 if(history_path&&_wgetenv(L"DLSS5_TEST_TEMPORAL_DUMP")){
  auto dump=[&](ID3D12Resource*r,UINT64 bytes,const wchar_t*name){auto*rb=buffer(device,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);ck(allocator->Reset());ck(c->Reset(allocator,nullptr));barrier(c,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);c->CopyBufferRegion(rb,0,r,0,bytes);barrier(c,r,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);submit();void*p=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};ck(rb->Map(0,&range,&p));std::ofstream f((std::wstring(argv[7])+L"\\"+name).c_str(),std::ios::binary);if(!f.write(reinterpret_cast<const char*>(p),bytes))throw std::runtime_error("temporal diagnostic write failed");rb->Unmap(0,&none);rb->Release();};
  if(motion_path)dump(motion_coordinates.Output(),UINT64(width)*height*8,L"gpu-temporal-coordinates.f32");
  dump(temporal_sampler.Output(),UINT64(width)*height*16,L"gpu-temporal-sampled.f32");
 }
 for(UINT i=0;i<3;i++){std::ofstream f(argv[4+i],std::ios::binary);if(!f.write(reinterpret_cast<const char*>(data[i].data()),sizes[i]))throw std::runtime_error("output write failed");}
 std::printf("resident_chain=pass width=%u height=%u frames=5 intermediate_CPU_transfers=0\n",width,height);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
