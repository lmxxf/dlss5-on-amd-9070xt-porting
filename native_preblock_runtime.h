#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>

// Input: tile-major 8x8 RGBA32F. Outputs: full HWC32 and half-resolution HWC32,
// represented as FP32 values on the E4M3 lattice. No CPU readback in Record.
class NativePreblockRuntime {
 ID3D12Device* device{};
 ID3D12Resource *ffn{},*raw{},*main{},*down{},*weights[2]{},*noise{};
 ID3D12RootSignature *root{},*finish_root{};
 ID3D12PipelineState* pso[3]{};
 ID3D12DescriptorHeap* heap[3]{};
 UINT width{},height{};bool recorded{};
 static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("native preblock HRESULT="+std::to_string(unsigned(hr)));}
 ID3D12Resource* Buffer(UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
  D3D12_HEAP_PROPERTIES h{};h.Type=type;h.CreationNodeMask=h.VisibleNodeMask=1;
  D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=type==D3D12_HEAP_TYPE_DEFAULT?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
  ID3D12Resource*r=nullptr;Check(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));return r;
 }
 ID3D12RootSignature* Root(UINT srvs,UINT uavs,bool with_noise=false){
  D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,srvs,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,uavs,0,0,srvs}};
  D3D12_ROOT_PARAMETER p[3]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,ranges};p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,4};p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[2].Descriptor={2,0};
  D3D12_ROOT_SIGNATURE_DESC d{};d.NumParameters=with_noise?3:2;d.pParameters=p;ID3DBlob*b=nullptr,*error=nullptr;
  Check(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&b,&error));ID3D12RootSignature*r=nullptr;Check(device->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&r)));b->Release();if(error)error->Release();return r;
 }
 void Heap(UINT stage,ID3D12Resource*a,UINT64 asize,ID3D12Resource*b,UINT64 bsize,ID3D12Resource*c,UINT64 csize,bool finish){
  D3D12_DESCRIPTOR_HEAP_DESC d{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};Check(device->CreateDescriptorHeap(&d,IID_PPV_ARGS(&heap[stage])));
  auto h=heap[stage]->GetCPUDescriptorHandleForHeapStart();UINT step=device->GetDescriptorHandleIncrementSize(d.Type);
  ID3D12Resource* resources[]={a,b,c};UINT64 sizes[]={asize,bsize,csize};
  for(UINT i=0;i<3;i++){
   if(i<(finish?1u:2u)){D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Buffer.StructureByteStride=4;s.Buffer.NumElements=UINT(sizes[i]/4);device->CreateShaderResourceView(resources[i],&s,h);}
   else{D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;u.Buffer.StructureByteStride=4;u.Buffer.NumElements=UINT(sizes[i]/4);device->CreateUnorderedAccessView(resources[i],nullptr,&u,h);}
   h.ptr+=step;
  }
 }
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&x);}
public:
 ~NativePreblockRuntime(){for(auto*r:{ffn,raw,main,down,weights[0],weights[1],noise})if(r)r->Release();for(auto*p:pso)if(p)p->Release();for(auto*h:heap)if(h)h->Release();if(root)root->Release();if(finish_root)finish_root->Release();}
 NativePreblockRuntime()=default;NativePreblockRuntime(const NativePreblockRuntime&)=delete;NativePreblockRuntime& operator=(const NativePreblockRuntime&)=delete;
 void Create(ID3D12Device*d,ID3D12Resource*input,UINT w,UINT h,const std::vector<float>&fw,const std::vector<float>&aw,const std::wstring&shader_dir,bool live_profile,bool raw_features=false,const std::vector<float>*noise_table=nullptr){
  if(noise_table&&(raw_features||noise_table->size()!=size_t(3)*(1<<24)))throw std::runtime_error("invalid universal noise table");
  if(device||!d||!input||!w||!h||w%8||h%8||UINT64(w)*h/64>65535||fw.size()!=8736||aw.size()!=8225)throw std::runtime_error("invalid native preblock contract");
  device=d;width=w;height=h;UINT64 bytes=UINT64(w)*h*32*4;
  ffn=Buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);raw=Buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);main=Buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);down=Buffer(bytes/4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  const std::vector<float>* values[]={&fw,&aw};
  for(UINT i=0;i<2;i++){weights[i]=Buffer(values[i]->size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};Check(weights[i]->Map(0,&none,&p));std::memcpy(p,values[i]->data(),values[i]->size()*4);weights[i]->Unmap(0,nullptr);}
  if(noise_table){noise=Buffer(noise_table->size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};Check(noise->Map(0,&none,&p));std::memcpy(p,noise_table->data(),noise_table->size()*4);noise->Unmap(0,nullptr);}
  root=Root(2,1,noise!=nullptr);finish_root=Root(1,2);
  Heap(0,weights[0],fw.size()*4,input,UINT64(w)*h*(raw_features?128:16),ffn,bytes,false);Heap(1,weights[1],aw.size()*4,ffn,bytes,raw,bytes,false);Heap(2,raw,bytes,main,bytes,down,bytes/4,true);
  const wchar_t* names[]={L"preblock_input_mix.hlsl",L"preblock_attention_core.hlsl",L"preblock_finish.hlsl"};
  auto count=std::to_string(UINT64(w)*h*32);
  D3D_SHADER_MACRO macros[]={{"TOTAL_OUTPUTS",count.c_str()},{"FULL_FFN","1"},{"RAW_OUTPUT","1"},{"RAW_INPUT",raw_features?"1":"0"},{"DEBUG_FEATURES","0"},{"DYNAMIC_PARAMETERS","1"},{"LIVE_PROFILE",live_profile?"1":"0"},{"NOISE_SEED","0"},{"NATIVE_NOISE_TABLE",noise?"1":"0"},{nullptr,nullptr}};
  for(UINT i=0;i<3;i++){
   ID3DBlob*code=nullptr,*error=nullptr;auto path=shader_dir+L"\\"+names[i];HRESULT hr=D3DCompileFromFile(path.c_str(),macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
   if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"compile failed";if(error)error->Release();throw std::runtime_error(message);}
   if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=i==2?finish_root:root;p.CS={code->GetBufferPointer(),code->GetBufferSize()};Check(device->CreateComputePipelineState(&p,IID_PPV_ARGS(&pso[i])));code->Release();
  }
 }
 void Record(ID3D12GraphicsCommandList*c,UINT seed,bool local_oracle=false){
  if(!device||!c)throw std::runtime_error("native preblock not created");
  if(recorded)for(auto*r:{ffn,raw,main,down})Barrier(c,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  const UINT constants[]={seed,width,height,local_oracle?1u:0u};const UINT groups=width*height/64;
  for(UINT stage=0;stage<3;stage++){
   c->SetDescriptorHeaps(1,&heap[stage]);c->SetComputeRootSignature(stage==2?finish_root:root);c->SetComputeRootDescriptorTable(0,heap[stage]->GetGPUDescriptorHandleForHeapStart());c->SetComputeRoot32BitConstants(1,4,constants,0);if(noise&&stage<2)c->SetComputeRootShaderResourceView(2,noise->GetGPUVirtualAddress());c->SetPipelineState(pso[stage]);c->Dispatch(groups,1,1);
   if(stage<2)Barrier(c,stage?raw:ffn,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
  Barrier(c,main,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Barrier(c,down,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);recorded=true;
 }
 ID3D12Resource* Main()const{return main;}ID3D12Resource* Downsample()const{return down;}ID3D12Resource* RawTiles()const{return raw;}
};
