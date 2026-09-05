#pragma once
#include "native_c64.h"
class NativeC64Shift {
 NativeC64 body;ID3D12Resource*input{},*padded{},*output{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso[2]{};UINT geometry[6]{};bool recorded{};
 static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("C64 shift HRESULT="+std::to_string(unsigned(hr)));}
 static ID3D12Resource* Buffer(ID3D12Device*d,UINT64 bytes){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource*r=nullptr;Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));return r;}
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeC64Shift()=default;NativeC64Shift(const NativeC64Shift&)=delete;
 ~NativeC64Shift(){if(input)input->Release();if(padded)padded->Release();if(output)output->Release();if(root)root->Release();for(auto*p:pso)if(p)p->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT width,UINT height,UINT shift,const std::vector<float>&fw,const std::vector<float>&aw,const std::wstring&dir){
  if(input||!d||!src||!width||!height||width%8||height%8||shift>3)throw std::runtime_error("C64 shift contract");input=src;input->AddRef();geometry[0]=width;geometry[1]=height;geometry[4]=(shift&1)?4:0;geometry[5]=(shift&2)?4:0;geometry[2]=width+geometry[4]*2;geometry[3]=height+geometry[5]*2;
  padded=Buffer(d,UINT64(geometry[2])*geometry[3]*64*4);output=Buffer(d,UINT64(width)*height*64*4);body.Create(d,padded,geometry[2],geometry[3],fw,aw,dir);
  D3D12_ROOT_PARAMETER params[3]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,6};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();
  const char*entry[]={"pack","crop"};auto path=dir+L"\\native_c64_shift.hlsl";
  for(UINT i=0;i<2;i++){blob=nullptr;error=nullptr;auto hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry[i],"cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"C64 shift shader failed";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};Check(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i])));blob->Release();}
 }
 void Record(ID3D12GraphicsCommandList*c){
  if(recorded){Barrier(c,padded,true);Barrier(c,output,true);}
  auto pass=[&](UINT i,ID3D12Resource*src,ID3D12Resource*dst,UINT pixels){c->SetComputeRootSignature(root);c->SetPipelineState(pso[i]);c->SetComputeRootShaderResourceView(0,src->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(1,dst->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(2,6,geometry,0);c->Dispatch((pixels+63)/64,1,1);};
  pass(0,input,padded,geometry[2]*geometry[3]);Barrier(c,padded,false);body.Record(c);pass(1,body.Output(),output,geometry[0]*geometry[1]);Barrier(c,output,false);recorded=true;
 }
 ID3D12Resource* Output()const{return output;}
};
