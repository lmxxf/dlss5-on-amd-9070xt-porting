#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include "native_resident_table.h"
#include "native_network_timestamps.h"

// Input: tile-major 8x8 RGBA32F. Outputs: full HWC32 and half-resolution HWC32,
// represented as FP32 values on the E4M3 lattice. No CPU readback in Record.
class NativePreblockRuntime {
 ID3D12Device* device{};
 ID3D12Resource*prefix_ffn_weights{};bool prefix_wave{};
 ID3D12Resource *ffn{},*raw{},*main{},*down{},*weights[2]{},*noise{},*temporal{};
 ID3D12RootSignature *root{},*finish_root{};
 ID3D12PipelineState* pso[4]{};
 ID3D12DescriptorHeap* heap[4]{};
 UINT width{},height{};bool recorded{},shared_raw{},wave_ffn{},wave_ffn_local{};
 static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("native preblock HRESULT="+std::to_string(unsigned(hr)));}
 ID3D12Resource* Buffer(UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
  D3D12_HEAP_PROPERTIES h{};h.Type=type;h.CreationNodeMask=h.VisibleNodeMask=1;
  D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=type==D3D12_HEAP_TYPE_DEFAULT?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
  ID3D12Resource*r=nullptr;Check(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));return r;
 }
 ID3D12RootSignature* Root(UINT srvs,UINT uavs,bool with_noise=false,bool with_temporal=false){
  D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,srvs,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,uavs,0,0,srvs}};
  D3D12_ROOT_PARAMETER p[4]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,ranges};p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,5};p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[2].Descriptor={2,0};p[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[3].Descriptor={3,0};
  D3D12_ROOT_SIGNATURE_DESC d{};d.NumParameters=with_temporal?4:with_noise?3:2;d.pParameters=p;ID3DBlob*b=nullptr,*error=nullptr;
  Check(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&b,&error));ID3D12RootSignature*r=nullptr;Check(device->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&r)));b->Release();if(error)error->Release();return r;
 }
 void Heap(UINT stage,ID3D12Resource*a,UINT64 asize,ID3D12Resource*b,UINT64 bsize,ID3D12Resource*c,UINT64 csize,bool finish){
  D3D12_DESCRIPTOR_HEAP_DESC d{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};Check(device->CreateDescriptorHeap(&d,IID_PPV_ARGS(&heap[stage])));
  auto h=heap[stage]->GetCPUDescriptorHandleForHeapStart();UINT step=device->GetDescriptorHandleIncrementSize(d.Type);
  ID3D12Resource* resources[]={a,b,c};UINT64 sizes[]={asize,bsize,csize};
  for(UINT i=0;i<3;i++){
   if(i<(finish?1u:2u)){D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Buffer.StructureByteStride=4;s.Buffer.NumElements=UINT(sizes[i]/4);if(i==0&&((stage==0&&wave_ffn_local)||(stage==3&&prefix_wave))){s.Format=DXGI_FORMAT_R32_TYPELESS;s.Buffer.StructureByteStride=0;s.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;}device->CreateShaderResourceView(resources[i],&s,h);}
   else{D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;u.Buffer.StructureByteStride=4;u.Buffer.NumElements=UINT(sizes[i]/4);device->CreateUnorderedAccessView(resources[i],nullptr,&u,h);}
   h.ptr+=step;
  }
 }
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&x);}
public:
 ~NativePreblockRuntime(){if(prefix_ffn_weights)prefix_ffn_weights->Release();for(auto*r:{ffn,raw,main,down,weights[0],weights[1],noise,temporal})if(r)r->Release();for(auto*p:pso)if(p)p->Release();for(auto*h:heap)if(h)h->Release();if(root)root->Release();if(finish_root)finish_root->Release();}
 NativePreblockRuntime()=default;NativePreblockRuntime(const NativePreblockRuntime&)=delete;NativePreblockRuntime& operator=(const NativePreblockRuntime&)=delete;
 void Create(ID3D12Device*d,ID3D12Resource*input,UINT w,UINT h,const std::vector<float>&fw,const std::vector<float>&aw,const std::wstring&shader_dir,bool live_profile,bool raw_features=false,const std::vector<float>*noise_table=nullptr,ID3D12Resource*temporal_input=nullptr){
  if(noise_table&&(raw_features||noise_table->size()!=size_t(3)*(1<<24)))throw std::runtime_error("invalid universal noise table");
  if(device||!d||!input||!w||!h||w%8||h%8||UINT64(w)*h/64>65535||fw.size()!=8736||aw.size()!=8225)throw std::runtime_error("invalid native preblock contract");
  if(temporal_input){if(!noise_table||raw_features||!live_profile||temporal_input->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||temporal_input->GetDesc().Width<UINT64(w)*h*16)throw std::runtime_error("temporal preblock contract");temporal=temporal_input;temporal->AddRef();}
  const wchar_t*prefix_flag=_wgetenv(L"DLSS5_TEST_SPLIT_PREBLOCK_FFN");if(prefix_flag&&wcscmp(prefix_flag,L"0")&&wcscmp(prefix_flag,L"1"))throw std::runtime_error("invalid split preblock flag");prefix_wave=!raw_features&&noise_table&&prefix_flag&&!wcscmp(prefix_flag,L"1");
  const wchar_t*ffn_flag=_wgetenv(L"DLSS5_TEST_WAVE_C32_FFN");if(ffn_flag&&wcscmp(ffn_flag,L"0")&&wcscmp(ffn_flag,L"1"))throw std::runtime_error("invalid wave C32 FFN flag");wave_ffn=raw_features&&ffn_flag&&!wcscmp(ffn_flag,L"1");
  if(wave_ffn||prefix_wave)for(size_t i=512;i<8704;i++){uint32_t b;std::memcpy(&b,&fw[i],4);uint32_t m=b&0x7fffffffu;if(m&&((m&0x1fffu)||(m>>23)<113||(m>>23)>142))throw std::runtime_error("C32 FFN weight not exact half");}
  const wchar_t*local_ffn=_wgetenv(L"DLSS5_TEST_WAVE_C32_FFN_LOCAL");if(local_ffn&&wcscmp(local_ffn,L"0")&&wcscmp(local_ffn,L"1"))throw std::runtime_error("invalid local C32 FFN flag");wave_ffn_local=wave_ffn&&local_ffn&&!wcscmp(local_ffn,L"1");
  device=d;width=w;height=h;UINT64 bytes=UINT64(w)*h*32*4;
  ffn=Buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);raw=Buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);main=Buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);down=Buffer(bytes/4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  const std::vector<float>* values[]={&fw,&aw};
  for(UINT i=0;i<2;i++){weights[i]=Buffer(values[i]->size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};Check(weights[i]->Map(0,&none,&p));std::memcpy(p,values[i]->data(),values[i]->size()*4);weights[i]->Unmap(0,nullptr);}
  if(noise_table){noise=Buffer(noise_table->size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};Check(noise->Map(0,&none,&p));std::memcpy(p,noise_table->data(),noise_table->size()*4);noise->Unmap(0,nullptr);}
  const wchar_t*resident_flag=_wgetenv(L"DLSS5_TEST_RESIDENT_NOISE");if(resident_flag&&wcscmp(resident_flag,L"0")&&wcscmp(resident_flag,L"1"))throw std::runtime_error("invalid resident noise flag");
  if(noise&&resident_flag&&!wcscmp(resident_flag,L"1")){auto*local=NativeResidentTable(device,noise);noise->Release();noise=local;}
  const wchar_t*weight_flag=_wgetenv(L"DLSS5_TEST_RESIDENT_C32_WEIGHTS");if(weight_flag&&wcscmp(weight_flag,L"0")&&wcscmp(weight_flag,L"1"))throw std::runtime_error("invalid resident C32 weights flag");
  if(weight_flag&&!wcscmp(weight_flag,L"1"))for(auto*&w:weights){auto*local=NativeResidentTable(device,w);w->Release();w=local;}
  if(wave_ffn_local||prefix_wave){
   std::vector<float>packed(4096+32);
   for(size_t i=0;i<8192;i++){uint32_t bits;std::memcpy(&bits,&fw[512+i],4);uint32_t m=bits&0x7fffffffu;uint16_t h=uint16_t((bits>>16)&0x8000);if(m)h|=uint16_t(((int(m>>23)-112)<<10)|((m&0x7fffff)>>13));std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&h,2);}
   std::memcpy(packed.data()+4096,fw.data()+8704,128);
   auto*u=Buffer(packed.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};Check(u->Map(0,&none,&p));std::memcpy(p,packed.data(),packed.size()*4);u->Unmap(0,nullptr);
   auto*local=NativeResidentTable(device,u);u->Release();if(prefix_wave)prefix_ffn_weights=local;else{weights[0]->Release();weights[0]=local;}
  }
  root=Root(2,1,noise!=nullptr,temporal!=nullptr);finish_root=Root(1,2);
  Heap(0,weights[0],weights[0]->GetDesc().Width,input,UINT64(w)*h*(raw_features?128:16),prefix_wave?raw:ffn,bytes,false);Heap(1,weights[1],aw.size()*4,ffn,bytes,raw,bytes,false);Heap(2,raw,bytes,main,bytes,down,bytes/4,true);
  const wchar_t*flag=_wgetenv(L"DLSS5_TEST_SHARED_C32");if(flag&&wcscmp(flag,L"0")&&wcscmp(flag,L"1"))throw std::runtime_error("invalid shared C32 flag");shared_raw=raw_features&&flag&&!wcscmp(flag,L"1");
  if(prefix_wave)Heap(3,prefix_ffn_weights,prefix_ffn_weights->GetDesc().Width,raw,bytes,ffn,bytes,false);
  const wchar_t* names[]={L"preblock_input_mix.hlsl",L"preblock_attention_core.hlsl",L"preblock_finish.hlsl"};
  auto count=std::to_string(UINT64(w)*h*32);
  const wchar_t*cache_flag=_wgetenv(L"DLSS5_TEST_CACHE_C32_INPUT");if(cache_flag&&wcscmp(cache_flag,L"0")&&wcscmp(cache_flag,L"1"))throw std::runtime_error("invalid cached C32 input flag");
  const wchar_t*pad_flag=_wgetenv(L"DLSS5_TEST_PAD_C32_LDS");if(pad_flag&&wcscmp(pad_flag,L"0")&&wcscmp(pad_flag,L"1"))throw std::runtime_error("invalid C32 LDS padding flag");
  const wchar_t*fast_flag=_wgetenv(L"DLSS5_TEST_FAST_C32_FP8");if(fast_flag&&wcscmp(fast_flag,L"0")&&wcscmp(fast_flag,L"1"))throw std::runtime_error("invalid fast C32 flag");
  D3D_SHADER_MACRO macros[]={{"TOTAL_OUTPUTS",count.c_str()},{"FULL_FFN","1"},{"NATIVE_PREFIX_ONLY",prefix_wave?"1":"0"},{"RAW_OUTPUT","1"},{"RAW_INPUT",raw_features?"1":"0"},{"DEBUG_FEATURES","0"},{"DYNAMIC_PARAMETERS","1"},{"LIVE_PROFILE",live_profile?"1":"0"},{"NOISE_SEED","0"},{"NATIVE_NOISE_TABLE",noise?"1":"0"},{"NATIVE_TEMPORAL_RGB",temporal?"1":"0"},{"NATIVE_FAST_C32_FP8",fast_flag&&!wcscmp(fast_flag,L"1")?"1":"0"},{"NATIVE_PAD_C32_LDS",pad_flag&&!wcscmp(pad_flag,L"1")?"1":"0"},{"NATIVE_CACHE_C32_INPUT",cache_flag&&!wcscmp(cache_flag,L"1")?"1":"0"},{nullptr,nullptr}};
  const wchar_t*wave_flag=_wgetenv(L"DLSS5_TEST_WAVE_C32_SCORES");if(wave_flag&&wcscmp(wave_flag,L"0")&&wcscmp(wave_flag,L"1"))throw std::runtime_error("invalid wave C32 flag");
  const wchar_t*qkv_flag=_wgetenv(L"DLSS5_TEST_WAVE_C32_QKV");if(qkv_flag&&wcscmp(qkv_flag,L"0")&&wcscmp(qkv_flag,L"1"))throw std::runtime_error("invalid wave C32 QKV flag");
  const bool wave_qkv=qkv_flag&&!wcscmp(qkv_flag,L"1");if(wave_qkv&&(!wave_flag||wcscmp(wave_flag,L"1")))throw std::runtime_error("wave QKV requires wave scores");
  const wchar_t*av_flag=_wgetenv(L"DLSS5_TEST_WAVE_C32_AV");if(av_flag&&wcscmp(av_flag,L"0")&&wcscmp(av_flag,L"1"))throw std::runtime_error("invalid wave C32 AV flag");const bool wave_av=av_flag&&!wcscmp(av_flag,L"1");if(wave_av&&!wave_qkv)throw std::runtime_error("wave AV requires wave QKV");
  const wchar_t*proj_flag=_wgetenv(L"DLSS5_TEST_WAVE_C32_PROJECTION");if(proj_flag&&wcscmp(proj_flag,L"0")&&wcscmp(proj_flag,L"1"))throw std::runtime_error("invalid wave C32 projection flag");const bool wave_projection=proj_flag&&!wcscmp(proj_flag,L"1");if(wave_projection&&!wave_av)throw std::runtime_error("wave projection requires AV");
  if(wave_qkv)for(size_t i=0;i<(wave_projection?4096:3072);i++){uint32_t b;std::memcpy(&b,&aw[i],4);uint32_t m=b&0x7fffffffu;if(m&&((m&0x1fffu)||(m>>23)<113||(m>>23)>142))throw std::runtime_error("C32 QKV weights not exact normal half");}
  if(prefix_wave){ID3DBlob*code=nullptr;Check(D3DReadFileToBlob((shader_dir+L"\\native_wave_c32_ffn_local.cso").c_str(),&code));D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=root;p.CS={code->GetBufferPointer(),code->GetBufferSize()};auto hr=device->CreateComputePipelineState(&p,IID_PPV_ARGS(&pso[3]));code->Release();Check(hr);}
  for(UINT i=0;i<3;i++){
   ID3DBlob*code=nullptr,*error=nullptr;auto path=shader_dir+L"\\"+names[i];HRESULT hr=(i==0&&wave_ffn)?D3DReadFileToBlob((shader_dir+(wave_ffn_local?L"\\native_wave_c32_ffn_local.cso":L"\\native_wave_c32_ffn.cso")).c_str(),&code):(i==1&&wave_flag&&!wcscmp(wave_flag,L"1"))?D3DReadFileToBlob((shader_dir+(wave_projection?L"\\native_wave_c32_full_attention.cso":wave_av?L"\\native_wave_c32_av.cso":wave_qkv?L"\\native_wave_c32_qkv.cso":L"\\native_wave_c32_scores.cso")).c_str(),&code):D3DCompileFromFile(path.c_str(),macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,(i==0&&shared_raw)?"raw_ffn_shared":"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
   if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"compile failed";if(error)error->Release();throw std::runtime_error(message);}
   if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=i==2?finish_root:root;p.CS={code->GetBufferPointer(),code->GetBufferSize()};Check(device->CreateComputePipelineState(&p,IID_PPV_ARGS(&pso[i])));code->Release();
  }
 }
 void Record(ID3D12GraphicsCommandList*c,UINT seed,bool local_oracle=false,bool temporal_enabled=false,NativeNetworkTimestamps*timer=nullptr,const char*label="c32_probe"){
  if(!device||!c)throw std::runtime_error("native preblock not created");
  if(recorded)for(auto*r:{ffn,raw,main,down})Barrier(c,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(temporal_enabled&&!temporal)throw std::runtime_error("temporal input not bound");
  const UINT constants[]={seed,width,height,local_oracle?1u:0u,temporal_enabled?1u:0u};const UINT groups=width*height/64;
  for(UINT stage=0;stage<3;stage++){
   c->SetDescriptorHeaps(1,&heap[stage]);c->SetComputeRootSignature(stage==2?finish_root:root);c->SetComputeRootDescriptorTable(0,heap[stage]->GetGPUDescriptorHandleForHeapStart());c->SetComputeRoot32BitConstants(1,5,constants,0);if(noise&&stage<2)c->SetComputeRootShaderResourceView(2,noise->GetGPUVirtualAddress());if(temporal&&stage<2)c->SetComputeRootShaderResourceView(3,temporal->GetGPUVirtualAddress());c->SetPipelineState(pso[stage]);if(stage==0&&wave_ffn){UINT n=groups*4;c->Dispatch(n<65535?n:65535,(n+65534)/65535,1);}else if(stage==0&&shared_raw){UINT n=groups*8;c->Dispatch(n<65535?n:65535,(n+65534)/65535,1);}else c->Dispatch(groups,1,1);
   if(stage==0&&prefix_wave){
    Barrier(c,raw,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);if(timer)timer->Mark(c,std::string(label)+"_prefix");
    c->SetDescriptorHeaps(1,&heap[3]);c->SetComputeRootSignature(root);c->SetComputeRootDescriptorTable(0,heap[3]->GetGPUDescriptorHandleForHeapStart());c->SetComputeRoot32BitConstants(1,5,constants,0);c->SetPipelineState(pso[3]);UINT n=groups*4;c->Dispatch(n<65535?n:65535,(n+65534)/65535,1);
    Barrier(c,raw,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Barrier(c,ffn,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   }else if(stage<2)Barrier(c,stage?raw:ffn,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   if(timer)timer->Mark(c,std::string(label)+"_stage"+std::to_string(stage));
  }
  Barrier(c,main,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Barrier(c,down,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);recorded=true;
 }
 ID3D12Resource* Main()const{return main;}ID3D12Resource* Downsample()const{return down;}ID3D12Resource* RawTiles()const{return raw;}
};
