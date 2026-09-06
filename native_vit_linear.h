#pragma once
#include "native_split.h"
class NativeVitLinear {
 ID3D12Resource *input{},*residual{},*weights{},*output{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso{};UINT count{},outputs{};bool recorded{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("ViT linear HRESULT="+std::to_string(unsigned(h)));}
 static ID3D12Resource* buffer(ID3D12Device*d,UINT64 bytes,const std::vector<float>*data=nullptr){D3D12_HEAP_PROPERTIES hp{};hp.Type=data?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=data?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,data?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));if(data){void*p=nullptr;D3D12_RANGE none{};ck(r->Map(0,&none,&p));std::memcpy(p,data->data(),bytes);r->Unmap(0,nullptr);}return r;}
 void barrier(ID3D12GraphicsCommandList*c,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeVitLinear()=default;NativeVitLinear(const NativeVitLinear&)=delete;
 ~NativeVitLinear(){for(auto*r:{input,residual,weights,output})if(r)r->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,ID3D12Resource*skip,UINT tokens,UINT inputs,UINT out,bool expand,const std::vector<float>&coefficients,const std::wstring&dir,bool decoder=false){
  const bool game_decoder=decoder&&inputs==1024&&out==512&&tokens==640;
  const bool decoder_shape=game_decoder||(inputs==1024&&out==512&&tokens==64)||(inputs==512&&out==256&&(tokens==64||tokens==256))||(inputs==256&&out==128&&(tokens==64||tokens==1024))||(inputs==128&&out==64&&(tokens==64||tokens==4096))||(inputs==64&&out==32&&(tokens==64||tokens==16384));
  if(input||!d||!src||(!decoder&&tokens!=64&&tokens!=256&&tokens!=640)||(!expand&&!skip)||(decoder?(expand||!decoder_shape):(expand?(inputs!=1024||out!=4096):(out!=1024||(inputs!=1024&&inputs!=4096))))||coefficients.size()!=size_t(inputs)*out+(expand?0:out))throw std::runtime_error("ViT/decoder linear contract");
  UINT64 output_values=game_decoder?UINT64(60)*36*out:UINT64(tokens)*out*(decoder?4:1);
  if(decoder&&(src->GetDesc().Width<UINT64(tokens)*inputs*4||skip->GetDesc().Width<output_values*4))throw std::runtime_error("decoder buffer capacity");
  input=src;input->AddRef();residual=skip?skip:src;residual->AddRef();count=tokens;outputs=out;weights=buffer(d,coefficients.size()*4,&coefficients);output=buffer(d,output_values*4);
D3D12_ROOT_PARAMETER params[5]{};for(UINT i=0;i<3;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[4].Constants={0,0,2};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=5;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;ck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();error=nullptr;
  auto in_text=std::to_string(inputs),out_text=std::to_string(out);D3D_SHADER_MACRO macros[]={{"INPUT_CHANNELS",in_text.c_str()},{"OUTPUT_CHANNELS",out_text.c_str()},{"EXPAND",expand?"1":"0"},{"DECODER_ENTRY",decoder?"1":"0"},{nullptr,nullptr}};
  auto hr=CompileNativeShader(dir+L"\\native_vit_linear.hlsl",macros,"main",&blob,&error);if(FAILED(hr)){std::string message=error?std::string((const char*)error->GetBufferPointer(),error->GetBufferSize()):"ViT shader compilation";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));blob->Release();
 }
 void Record(ID3D12GraphicsCommandList*c){if(recorded)barrier(c,true);c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,residual->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,output->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,1,&count,0);for(UINT base=0;base<count*outputs;base+=65536){c->SetComputeRoot32BitConstants(4,1,&base,1);UINT size=std::min(UINT(65536),count*outputs-base);c->Dispatch((size+63)/64,1,1);}barrier(c,false);recorded=true;}
 ID3D12Resource* Output()const{return output;}
 UINT ChunkCount()const{return (count*outputs+65535)/65536;}
 void RecordChunk(ID3D12GraphicsCommandList*c,UINT chunk){
  if(chunk>=ChunkCount())throw std::runtime_error("linear chunk range");
  if(chunk==0&&recorded)barrier(c,true);
  c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,residual->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,output->GetGPUVirtualAddress());
  UINT base=chunk*65536,constants[]={count,base};c->SetComputeRoot32BitConstants(4,2,constants,0);c->Dispatch((std::min(UINT(65536),count*outputs-base)+63)/64,1,1);
  if(chunk+1==ChunkCount()){barrier(c,false);recorded=true;}
 }
};
