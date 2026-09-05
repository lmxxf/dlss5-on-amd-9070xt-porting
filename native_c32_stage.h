#pragma once
#include "native_preblock_runtime.h"
// Ordinary C32 stage: zero-pad shifted windows, native FP8 body, crop to HWC.
// Does not claim to implement the special learned downsample of a DS block.
class NativeC32Stage {
 NativePreblockRuntime body;
 ID3D12Device*device{};ID3D12Resource*packed{},*output{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso[2]{};ID3D12DescriptorHeap*heap[2]{};
 UINT geometry[6]{};bool recorded{};
 static void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("C32 stage HRESULT="+std::to_string(unsigned(h)));}
 ID3D12Resource* Buffer(UINT64 n){D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=n;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource*r=nullptr;Check(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));return r;}
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&v);}
 void Heap(UINT i,ID3D12Resource*src,UINT64 sn,ID3D12Resource*dst,UINT64 dn){D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap[i])));auto h=heap[i]->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Buffer.NumElements=UINT(sn/4);s.Buffer.StructureByteStride=4;device->CreateShaderResourceView(src,&s,h);h.ptr+=device->GetDescriptorHandleIncrementSize(hd.Type);D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=UINT(dn/4);u.Buffer.StructureByteStride=4;device->CreateUnorderedAccessView(dst,nullptr,&u,h);}
public:
 NativeC32Stage()=default;NativeC32Stage(const NativeC32Stage&)=delete;
 ~NativeC32Stage(){if(packed)packed->Release();if(output)output->Release();if(root)root->Release();for(auto*p:pso)if(p)p->Release();for(auto*h:heap)if(h)h->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*source,UINT width,UINT height,UINT shift_mask,const std::vector<float>&fw,const std::vector<float>&aw,const std::wstring&dir){
  if(device||!d||!source||!width||!height||width%8||height%8||shift_mask>3)throw std::runtime_error("C32 geometry");device=d;
  geometry[0]=width;geometry[1]=height;geometry[4]=(shift_mask&1)?4:0;geometry[5]=(shift_mask&2)?4:0;geometry[2]=width+geometry[4]*2;geometry[3]=height+geometry[5]*2;
  UINT64 n=UINT64(width)*height*128,work=UINT64(geometry[2])*geometry[3]*128;packed=Buffer(work);output=Buffer(n);
  body.Create(device,packed,geometry[2],geometry[3],fw,aw,dir,false,true);
  D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,ranges};p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,6};D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=p;ID3DBlob*b=nullptr,*err=nullptr;Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&b,&err));Check(device->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&root)));b->Release();if(err)err->Release();
  Heap(0,source,n,packed,work);Heap(1,body.Main(),work,output,n);
  const char*entry[]={"pack","crop"};for(UINT i=0;i<2;i++){ID3DBlob*code=nullptr,*error=nullptr;auto path=dir+L"\\native_c32_reframe.hlsl";auto hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry[i],"cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(FAILED(hr)){std::string msg=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"C32 shader failed";if(error)error->Release();throw std::runtime_error(msg);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={code->GetBufferPointer(),code->GetBufferSize()};Check(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i])));code->Release();}
 }
 void Record(ID3D12GraphicsCommandList*c){
  if(recorded){Barrier(c,packed,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Barrier(c,output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
  auto pass=[&](UINT i,UINT groups){c->SetDescriptorHeaps(1,&heap[i]);c->SetComputeRootSignature(root);c->SetComputeRootDescriptorTable(0,heap[i]->GetGPUDescriptorHandleForHeapStart());c->SetComputeRoot32BitConstants(1,6,geometry,0);c->SetPipelineState(pso[i]);c->Dispatch(groups,1,1);};
  pass(0,geometry[2]*geometry[3]/64);Barrier(c,packed,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);body.Record(c,0);pass(1,geometry[0]*geometry[1]/64);Barrier(c,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);recorded=true;
 }
 ID3D12Resource* Output()const{return output;}
 ID3D12Resource* PooledWork()const{return body.Downsample();}
};
