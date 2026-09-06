#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <fstream>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include "native_split_window.h"
#include "native_c32_ds.h"
#include "native_vit_linear.h"
#include "native_vit_qkv.h"
#include "native_vit_attention.h"
#include "native_vit_block.h"
#include "native_vit_gather.h"
#include "native_c64_shift.h"
static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D HRESULT="+std::to_string(unsigned(h)));}
static std::vector<float> read(const std::wstring&p){std::ifstream f(p.c_str(),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("missing fixture");auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error("fixture size");std::vector<float>v(size_t(n)/4);f.seekg(0);if(!f.read((char*)v.data(),n))throw std::runtime_error("short fixture");return v;}
static ID3D12Resource* buffer(ID3D12Device*d,UINT64 n,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=n;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)));return r;}
int wmain(int argc,wchar_t**argv){try{
 if(argc!=2&&argc!=3)return 2;
 const bool up62=argc==3&&!wcscmp(argv[2],L"upsample62"),up56=argc==3&&!wcscmp(argv[2],L"upsample56"),wide=argc==3&&!wcscmp(argv[2],L"upsample48wide"),upsample=up62||up56||wide||(argc==3&&!wcscmp(argv[2],L"upsample48")),decoder=upsample||(argc==3&&!wcscmp(argv[2],L"decoder39")),bridge_mode=argc==3&&!wcscmp(argv[2],L"bridge"),chain_mode=argc==3&&!wcscmp(argv[2],L"vit_chain"),block_mode=bridge_mode||chain_mode||(argc==3&&!wcscmp(argv[2],L"block31")),attention_mode=argc==3&&!wcscmp(argv[2],L"qkv_attention"),qkv_mode=attention_mode||(argc==3&&!wcscmp(argv[2],L"qkv")),linear=argc==3&&!qkv_mode&&!block_mode,expand=linear&&!wcscmp(argv[2],L"expand"),contract=linear&&!wcscmp(argv[2],L"contract");
 if(linear&&!decoder&&!expand&&!contract&&wcscmp(argv[2],L"projection"))return 2;
 const UINT tokens=up62?4096:up56?1024:wide?256:64,inputs=up62?128:up56?256:upsample?512:contract?4096:1024,outputs=up62?64:up56?128:upsample?256:decoder?512:expand?4096:1024;
 std::wstring dir=argv[1];auto file=[&](const wchar_t*n){return dir+L"\\"+n;};auto values=read(file(L"input.f32")),oracle=read(file(L"oracle.f32"));
 if(values.size()!=((block_mode||qkv_mode)?64*1024:linear?tokens*inputs:8*8*512)||oracle.size()!=(decoder?4*tokens*outputs:(block_mode||attention_mode)?64*1024:qkv_mode?64*3072:linear?64*outputs:4*4*1024))throw std::runtime_error("fixture geometry");
 const bool switch_input=_wgetenv(L"DLSS5_VIT_SWITCH_INPUT")!=nullptr;if(switch_input&&!chain_mode)throw std::runtime_error("input switching requires ViT chain mode");
 std::vector<float>alternate,alternate_oracle;if(switch_input){alternate=read(file(L"input-alt.f32"));alternate_oracle=read(file(L"oracle-alt.f32"));if(alternate.size()!=values.size()||alternate_oracle.size()!=oracle.size()||alternate==values||alternate_oracle==oracle)throw std::runtime_error("invalid alternate fixture");}
 IDXGIFactory6*factory=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));IDXGIAdapter1*adapter=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=a;std::wprintf(L"adapter=%ls\n",desc.Description);break;}a->Release();}if(!adapter)throw std::runtime_error("AMD missing");ID3D12Device*device=nullptr;ck(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
 auto*input=buffer(device,values.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*m=nullptr;D3D12_RANGE empty{};ck(input->Map(0,&empty,&m));std::memcpy(m,values.data(),values.size()*4);input->Unmap(0,nullptr);
 NativeSplitWindow split;NativeC32Downsample head;NativeVitLinear operation;NativeVitQkv qkv;NativeVitAttention attention;NativeVitBlock block,chain[8];NativeVitGather bridge;
 if(bridge_mode){std::ifstream f(file(L"indices.i32").c_str(),std::ios::binary|std::ios::ate);if(!f||f.tellg()!=65536*4)throw std::runtime_error("bridge map size");std::vector<UINT>map(65536);f.seekg(0);if(!f.read((char*)map.data(),map.size()*4))throw std::runtime_error("bridge map read");bridge.Create(device,input,map,dir);}
 else if(chain_mode){auto*source=input;for(UINT i=0;i<8;i++){auto prefix=L"block"+std::to_wstring(i+31)+L"-";chain[i].Create(device,source,64,read(file((prefix+L"expand.f32").c_str())),read(file((prefix+L"contract.f32").c_str())),read(file((prefix+L"qkv.f32").c_str())),read(file((prefix+L"projection.f32").c_str())),dir);source=chain[i].Output();}}
 else if(block_mode)block.Create(device,input,64,read(file(L"expand.f32")),read(file(L"contract.f32")),read(file(L"qkv.f32")),read(file(L"projection.f32")),dir);
 else if(qkv_mode){qkv.Create(device,input,64,read(file(L"weights.f32")),dir);if(attention_mode)attention.Create(device,qkv.Output(),64,dir);}
 else if(linear){ID3D12Resource*skip=nullptr;if(!expand){auto data=read(file(L"residual.f32"));if(data.size()!=tokens*outputs*(decoder?4:1))throw std::runtime_error("residual shape");skip=buffer(device,data.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);ck(skip->Map(0,&empty,&m));std::memcpy(m,data.data(),data.size()*4);skip->Unmap(0,nullptr);}operation.Create(device,input,skip,tokens,inputs,outputs,expand,read(file(L"weights.f32")),dir,decoder);if(skip)skip->Release();}
 else{split.Create(device,input,8,8,2,read(file(L"ffwd.f32")),read(file(L"ffwd-projection.f32")),read(file(L"attention.f32")),dir,true);head.Create(device,split.Output(),8,8,0,read(file(L"head.f32")),dir,true,512);}
 NativeC64Shift upsample_body;
 if(upsample){auto shift=read(file(L"shift.f32"));if(shift.size()!=1||!std::isfinite(shift[0])||shift[0]<0||shift[0]>3||std::floor(shift[0])!=shift[0])throw std::runtime_error("upsample shift");UINT extent=up62?128:up56?64:wide?32:16;upsample_body.Create(device,operation.Output(),extent,extent,UINT(shift[0]),read(file(L"ffn.f32")),read(file(L"attention.f32")),dir,false,outputs);}
 auto*result_resource=upsample?upsample_body.Output():bridge_mode?bridge.Output():chain_mode?chain[7].Output():block_mode?block.Output():attention_mode?attention.Output():qkv_mode?qkv.Output():linear?operation.Output():head.Output();
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ID3D12CommandAllocator*allocator=nullptr;ck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));ID3D12GraphicsCommandList*cmd=nullptr;ck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&cmd)));ID3D12Fence*fence=nullptr;ck(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 const UINT64 bytes=oracle.size()*4;auto*rb=buffer(device,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);std::vector<float>result(oracle.size()),baseline;
 const UINT frames=switch_input?5:3;
 for(UINT frame=0;frame<frames;frame++){
  if(switch_input&&(frame==2||frame==3)){const auto&data=frame==2?alternate:values;ck(input->Map(0,&empty,&m));std::memcpy(m,data.data(),data.size()*4);input->Unmap(0,nullptr);}
  if(frame){ck(allocator->Reset());ck(cmd->Reset(allocator,nullptr));}if(bridge_mode)bridge.Record(cmd);else if(chain_mode){for(auto&layer:chain)layer.Record(cmd);}else if(block_mode)block.Record(cmd);else if(qkv_mode){qkv.Record(cmd);if(attention_mode)attention.Record(cmd);}else if(linear)operation.Record(cmd);else{split.Record(cmd);head.Record(cmd);}
  if(upsample)upsample_body.Record(cmd);
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={result_resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};cmd->ResourceBarrier(1,&barrier);cmd->CopyBufferRegion(rb,0,result_resource,0,bytes);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);cmd->ResourceBarrier(1,&barrier);ck(cmd->Close());ID3D12CommandList*lists[]={cmd};q->ExecuteCommandLists(1,lists);ck(q->Signal(fence,frame+1));ck(fence->SetEventOnCompletion(frame+1,event));if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");ck(device->GetDeviceRemovedReason());auto done=fence->GetCompletedValue();if(done==UINT64_MAX||done<frame+1)throw std::runtime_error("fence failure");
  D3D12_RANGE range{0,SIZE_T(bytes)};ck(rb->Map(0,&range,&m));std::memcpy(result.data(),m,bytes);rb->Unmap(0,&empty);size_t different=0;float maximum=0;
  const auto&expected=switch_input&&frame==2?alternate_oracle:oracle;
  for(size_t i=0;i<result.size();i++){if(!std::isfinite(result[i])||!std::isfinite(expected[i]))throw std::runtime_error("nonfinite");different+=result[i]!=expected[i];maximum=std::max(maximum,std::abs(result[i]-expected[i]));}
  std::printf("frame=%u values=%zu different=%zu max_error=%.9g\n",frame,result.size(),different,maximum);std::fflush(stdout);if(different)throw std::runtime_error("original output mismatch");if(!frame)baseline=result;else if(switch_input&&frame==2){if(result==baseline)throw std::runtime_error("alternate input had no effect");}else if(result!=baseline)throw std::runtime_error("replay differs");
 }
 std::ofstream out(file(L"gpu.f32").c_str(),std::ios::binary);if(!out.write((char*)result.data(),bytes))throw std::runtime_error("output write");std::printf("%s=exact frames=%u intermediate_CPU_transfers=0\n",bridge_mode?"native_vit_bridge":chain_mode?"native_vit_chain31_38":block_mode?"native_vit_block31":attention_mode?"native_vit_qkv_attention":qkv_mode?"native_vit_qkv":linear?"native_vit_linear":"block30_pool_head",frames);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
