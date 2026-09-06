#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <fstream>
#include <cstdio>
#include <cmath>
#include "native_vit_attention.h"
#include "native_vit_qkv.h"
#include "native_vit_linear.h"
#include "native_vit_block.h"
#include "native_c32_ds.h"
#include "native_c64_shift.h"
#include "native_c32_stage.h"
#include "native_vit_gather.h"
static void ck(HRESULT hr){if(FAILED(hr))throw std::runtime_error("HRESULT="+std::to_string(unsigned(hr)));}
static std::vector<float> read(const std::wstring& path){
 std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("missing fixture");
 auto size=f.tellg();if(size<=0||size_t(size)%4)throw std::runtime_error("fixture size");
 std::vector<float> v(size_t(size)/4);f.seekg(0);if(!f.read((char*)v.data(),size))throw std::runtime_error("short read");return v;
}
static ID3D12Resource* buffer(ID3D12Device*d,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
 D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
 desc.Width=bytes;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)));return r;
}
int wmain(int argc,wchar_t**argv){try{
 if(argc!=2&&argc!=3)return 2;bool bridge_mode=argc==3&&!wcscmp(argv[2],L"bridge640");bool tail32=argc==3&&!wcscmp(argv[2],L"decoder67_69_game");bool up66=argc==3&&!wcscmp(argv[2],L"upsample66_game");bool up62=argc==3&&!wcscmp(argv[2],L"upsample62_game");bool up56=argc==3&&!wcscmp(argv[2],L"upsample56_game");bool up48=argc==3&&!wcscmp(argv[2],L"upsample48_game");bool decoder_mode=up66||up62||up56||up48||argc==3&&!wcscmp(argv[2],L"decoder39_game");bool pool_mode=argc==3&&!wcscmp(argv[2],L"pool_head");bool full_chain=argc==3&&!wcscmp(argv[2],L"chain31_38");bool block_mode=full_chain||(argc==3&&!wcscmp(argv[2],L"block31")),projection_mode=argc==3&&!wcscmp(argv[2],L"projection");bool qkv_mode=argc==3&&!wcscmp(argv[2],L"qkv"),chain_mode=argc==3&&!wcscmp(argv[2],L"qkv_attention"),expand_mode=argc==3&&!wcscmp(argv[2],L"expand"),contract_mode=decoder_mode||projection_mode||(argc==3&&!wcscmp(argv[2],L"contract"));if(argc==3&&!qkv_mode&&!chain_mode&&!expand_mode&&!contract_mode&&!block_mode&&!pool_mode&&!tail32&&!bridge_mode)return 2;
 std::wstring dir=argv[1];auto values=read(dir+L"\\input.f32"),oracle=read(dir+L"\\oracle.f32");
 UINT tokens=up66?138240:up62?34560:up56?8640:up48?2160:UINT(((decoder_mode||qkv_mode||expand_mode)?values.size():oracle.size())/1024);
 if(bridge_mode){if(values.size()!=655360||oracle.size()!=values.size())throw std::runtime_error("bridge geometry");}
 else if(tail32){if(values.size()!=960*576*32||oracle.size()!=values.size())throw std::runtime_error("C32 tail geometry");}
 else if(decoder_mode){if(up66?(values.size()!=288*480*64||oracle.size()!=576*960*32):up62?(values.size()!=144*240*128||oracle.size()!=288*480*64):up56?(values.size()!=72*120*256||oracle.size()!=144*240*128):up48?(values.size()!=36*60*512||oracle.size()!=72*120*256):(values.size()!=20*32*1024||oracle.size()!=36*60*512))throw std::runtime_error("decoder geometry");}
 else if(pool_mode){if(values.size()!=36*60*512||oracle.size()!=20*32*1024)throw std::runtime_error("pool geometry");}
 else if(contract_mode?(oracle.size()!=size_t(tokens)*1024||values.size()!=oracle.size()*(projection_mode?1:4)):(expand_mode?(values.size()!=size_t(tokens)*1024||oracle.size()!=values.size()*4):((chain_mode||block_mode)?(values.size()!=oracle.size()||oracle.size()!=size_t(tokens)*1024):(qkv_mode?(values.size()!=size_t(tokens)*1024||oracle.size()!=values.size()*3):(oracle.size()!=size_t(tokens)*1024||values.size()!=oracle.size()*3)))))throw std::runtime_error("geometry");
 for(float v:values)if(!std::isfinite(v))throw std::runtime_error("nonfinite input");
 IDXGIFactory6*factory=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));IDXGIAdapter1*adapter=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;
  DXGI_ADAPTER_DESC1 desc{};ck(a->GetDesc1(&desc));if(desc.VendorId==0x1002&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=a;std::wprintf(L"adapter=%ls\n",desc.Description);break;}a->Release();}
 if(!adapter)throw std::runtime_error("AMD missing");ID3D12Device*d=nullptr;ck(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
 auto*input=buffer(d,values.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*m=nullptr;D3D12_RANGE empty{};
 ck(input->Map(0,&empty,&m));std::memcpy(m,values.data(),values.size()*4);input->Unmap(0,nullptr);
 NativeVitGather bridge;if(bridge_mode){std::ifstream f((dir+L"\\indices.i32").c_str(),std::ios::binary|std::ios::ate);if(!f||f.tellg()!=655360*4)throw std::runtime_error("bridge map size");std::vector<UINT>map(655360);f.seekg(0);if(!f.read((char*)map.data(),map.size()*4))throw std::runtime_error("bridge map read");bridge.Create(d,input,map,dir);}
 NativeC32Downsample pool;
 if(pool_mode)pool.Create(d,input,60,36,0,read(dir+L"\\weights.f32"),dir,true,512);
 NativeVitAttention attention;NativeVitQkv qkv;NativeVitLinear linear;NativeVitBlock blocks[8];UINT block_count=full_chain?8:1;
 if(block_mode){auto*source=input;for(UINT i=0;i<block_count;i++){auto sub=full_chain?dir+L"\\block"+std::to_wstring(i+31):dir;blocks[i].Create(d,source,tokens,read(sub+L"\\expand.f32"),read(sub+L"\\contract.f32"),read(sub+L"\\qkv.f32"),read(sub+L"\\projection.f32"),dir);source=blocks[i].Output();std::printf("initialized block=%u\n",i+31);std::fflush(stdout);}}
 if(contract_mode){auto data=read(dir+L"\\residual.f32");if(data.size()!=oracle.size())throw std::runtime_error("residual geometry");for(float v:data)if(!std::isfinite(v))throw std::runtime_error("nonfinite residual");auto*skip=buffer(d,data.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);ck(skip->Map(0,&empty,&m));std::memcpy(m,data.data(),data.size()*4);skip->Unmap(0,nullptr);linear.Create(d,input,skip,tokens,up66?64:up62?128:up56?256:up48?512:(decoder_mode||projection_mode)?1024:4096,up66?32:up62?64:up56?128:up48?256:decoder_mode?512:1024,false,read(dir+L"\\weights.f32"),dir,decoder_mode);skip->Release();}
 if(expand_mode)linear.Create(d,input,nullptr,tokens,1024,4096,true,read(dir+L"\\weights.f32"),dir);
 if(qkv_mode||chain_mode)qkv.Create(d,input,tokens,read(dir+L"\\weights.f32"),dir);
 if(!qkv_mode&&!expand_mode&&!contract_mode&&!block_mode&&!pool_mode&&!tail32&&!bridge_mode)attention.Create(d,chain_mode?qkv.Output():input,tokens,dir);
 NativeC32Stage tail[3];if(tail32){auto*src=input;const UINT shifts[]={3,1,2};for(UINT i=0;i<3;i++){auto prefix=dir+L"\\block"+std::to_wstring(67+i);tail[i].Create(d,src,960,576,shifts[i],read(prefix+L"-ffn.f32"),read(prefix+L"-attention.f32"),dir);src=tail[i].Output();}}
 NativeC32Stage up32;if(up66)up32.Create(d,linear.Output(),960,576,0,read(dir+L"\\ffn.f32"),read(dir+L"\\attention.f32"),dir);
 NativeC64Shift upbody;if(up48||up56||up62)upbody.Create(d,linear.Output(),up62?480:up56?240:120,up62?288:up56?144:72,up56?1:0,read(dir+L"\\ffn.f32"),read(dir+L"\\attention.f32"),dir,false,up62?64:up56?128:256);
 auto*output=bridge_mode?bridge.Output():tail32?tail[2].Output():up66?up32.Output():(up48||up56||up62)?upbody.Output():pool_mode?pool.Output():block_mode?blocks[block_count-1].Output():(expand_mode||contract_mode)?linear.Output():qkv_mode?qkv.Output():attention.Output();
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
 ID3D12CommandAllocator*alloc=nullptr;ck(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
 ID3D12GraphicsCommandList*cmd=nullptr;ck(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,nullptr,IID_PPV_ARGS(&cmd)));
 ID3D12Fence*fence=nullptr;ck(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("event");
 UINT64 bytes=oracle.size()*4;auto*rb=buffer(d,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);std::vector<float> result(oracle.size());
 const bool staged=_wgetenv(L"DLSS5_VIT_STAGED")!=nullptr;if(staged&&!block_mode)throw std::runtime_error("staging requires block mode");
 ID3D12Fence*stage_fence=nullptr;UINT64 stage_value=0;if(staged)ck(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&stage_fence)));
 auto submit_stage=[&](UINT frame,UINT block,UINT stage){
  ck(cmd->Close());LARGE_INTEGER a,b,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);
  ID3D12CommandList*lists[]={cmd};q->ExecuteCommandLists(1,lists);ck(q->Signal(stage_fence,++stage_value));ck(stage_fence->SetEventOnCompletion(stage_value,event));
  auto wait=WaitForSingleObject(event,30000);QueryPerformanceCounter(&b);auto reason=d->GetDeviceRemovedReason();auto done=stage_fence->GetCompletedValue();
  std::printf("stage frame=%u block=%u stage=%u ms=%.3f device=0x%08x wait=%lu\n",frame,block+31,stage,1000.0*(b.QuadPart-a.QuadPart)/f.QuadPart,unsigned(reason),wait);std::fflush(stdout);
  if(wait!=WAIT_OBJECT_0)throw std::runtime_error("stage timeout");ck(reason);if(done==UINT64_MAX||done<stage_value)throw std::runtime_error("stage fence");ck(alloc->Reset());ck(cmd->Reset(alloc,nullptr));
 };
 for(UINT frame=0;frame<3;frame++){
  std::printf("begin frame=%u device=0x%08x\n",frame,unsigned(d->GetDeviceRemovedReason()));std::fflush(stdout);
  if(frame){ck(alloc->Reset());ck(cmd->Reset(alloc,nullptr));}if(bridge_mode)bridge.Record(cmd);if(pool_mode)pool.Record(cmd);if(block_mode){for(UINT i=0;i<block_count;i++){if(staged){for(UINT s=0;s<5;s++){for(UINT chunk=0;chunk<blocks[i].StageChunks(s);chunk++){blocks[i].RecordStageChunk(cmd,s,chunk);submit_stage(frame,i,s);}}}else blocks[i].Record(cmd);}}if(expand_mode||contract_mode)linear.Record(cmd);if(up48||up56||up62)upbody.Record(cmd);if(up66)up32.Record(cmd);if(tail32){for(auto&layer:tail)layer.Record(cmd);}if(qkv_mode||chain_mode)qkv.Record(cmd);if(!qkv_mode&&!expand_mode&&!contract_mode&&!block_mode&&!pool_mode&&!tail32&&!bridge_mode)attention.Record(cmd);
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
  cmd->ResourceBarrier(1,&b);cmd->CopyBufferRegion(rb,0,output,0,bytes);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);cmd->ResourceBarrier(1,&b);ck(cmd->Close());
  LARGE_INTEGER start,stop,frequency;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
  ID3D12CommandList*lists[]={cmd};q->ExecuteCommandLists(1,lists);auto signal=q->Signal(fence,frame+1);if(FAILED(signal)){std::printf("signal frame=%u hr=0x%08x device=0x%08x\n",frame,unsigned(signal),unsigned(d->GetDeviceRemovedReason()));std::fflush(stdout);}ck(signal);ck(fence->SetEventOnCompletion(frame+1,event));
  auto wait=WaitForSingleObject(event,30000);QueryPerformanceCounter(&stop);auto reason=d->GetDeviceRemovedReason();auto done=fence->GetCompletedValue();
  std::printf("submission frame=%u elapsed_ms=%.3f wait=%lu device=0x%08x fence=%llu\n",frame,1000.0*(stop.QuadPart-start.QuadPart)/frequency.QuadPart,wait,unsigned(reason),(unsigned long long)done);std::fflush(stdout);
  if(wait!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");ck(reason);if(done==UINT64_MAX||done<frame+1)throw std::runtime_error("fence");
  D3D12_RANGE range{0,SIZE_T(bytes)};ck(rb->Map(0,&range,&m));std::memcpy(result.data(),m,bytes);rb->Unmap(0,&empty);
  size_t diff=0;float maximum=0;for(size_t i=0;i<result.size();i++){if(!std::isfinite(result[i])||!std::isfinite(oracle[i]))throw std::runtime_error("nonfinite");diff+=result[i]!=oracle[i];maximum=std::max(maximum,std::abs(result[i]-oracle[i]));}
  std::printf("tokens=%u frame=%u values=%zu different=%zu max_abs=%.9g\n",tokens,frame,result.size(),diff,maximum);std::fflush(stdout);
  std::ofstream out((dir+L"\\gpu.f32").c_str(),std::ios::binary);if(!out.write((char*)result.data(),bytes))throw std::runtime_error("write");
  if(diff)throw std::runtime_error("original mismatch");
 }
 return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
