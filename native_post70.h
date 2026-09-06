#pragma once
#include "native_c32_stage.h"
#include "native_shader_cache.h"
// Pixel-aligned RGB base, texture-mask1/rgb-mode1 only. No optional blend textures.
class NativePost70 {
 NativeC32Stage body;
 ID3D12Resource *main_input{},*skip_input{},*color_input{},*merged{},*output{},*coefficients[2]{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso[2]{};
 UINT geometry[3]{};bool recorded{};
 static void ck(HRESULT hr){if(FAILED(hr))throw std::runtime_error("post70 HRESULT="+std::to_string(unsigned(hr)));}
 static ID3D12Resource*buffer(ID3D12Device*d,UINT64 bytes,const std::vector<float>*data=nullptr){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=data?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=data?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,data?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));
  if(data){void*p=nullptr;D3D12_RANGE none{};ck(r->Map(0,&none,&p));std::memcpy(p,data->data(),bytes);r->Unmap(0,nullptr);}return r;
 }
 static void barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,bool begin){
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);
 }
public:
 NativePost70()=default;NativePost70(const NativePost70&)=delete;
 ~NativePost70(){for(auto*r:{main_input,skip_input,color_input,merged,output,coefficients[0],coefficients[1]})if(r)r->Release();if(root)root->Release();for(auto*p:pso)if(p)p->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*main,ID3D12Resource*skip,ID3D12Resource*color,UINT width,UINT height,const std::vector<float>&scales,const std::vector<float>&ffn,const std::vector<float>&attention,const std::vector<float>&head,const std::wstring&dir,float input_scale=.03125f){
  if(main_input||!d||!main||!skip||!color||width<16||height<16||width>512||height>512||width%16||height%16||scales.size()!=64||head.size()!=96)throw std::runtime_error("post70 contract");
  UINT64 pixels=UINT64(width)*height;
  if(main->GetDesc().Width<pixels*8*4||skip->GetDesc().Width<pixels*32*4||color->GetDesc().Width<pixels*4*4)throw std::runtime_error("post70 input capacity");
  main_input=main;skip_input=skip;color_input=color;for(auto*r:{main_input,skip_input,color_input})r->AddRef();geometry[0]=width;geometry[1]=height;std::memcpy(&geometry[2],&input_scale,4);
  merged=buffer(d,pixels*32*4);output=buffer(d,pixels*3*4);coefficients[0]=buffer(d,scales.size()*4,&scales);coefficients[1]=buffer(d,head.size()*4,&head);
  body.Create(d,merged,width,height,0,ffn,attention,dir,true);
  D3D12_ROOT_PARAMETER params[5]{};for(UINT i=0;i<3;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}
  params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[4].Constants={0,0,3};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=5;desc.pParameters=params;
  ID3DBlob*blob=nullptr,*error=nullptr;ck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();
  const char*entries[]={"merge","finish"};
  const char*diagnostic=std::getenv("DLSS5_POST_BASE_ONLY");
  if(diagnostic&&std::strcmp(diagnostic,"1")&&std::strcmp(diagnostic,"2"))throw std::runtime_error("post diagnostic mode must be1 or2");
  D3D_SHADER_MACRO macros[]={{"POST_BASE_ONLY",diagnostic?diagnostic:"0"},{nullptr,nullptr}};
  for(UINT i=0;i<2;i++){blob=nullptr;error=nullptr;auto hr=CompileNativeShader(dir+L"\\native_post70.hlsl",macros,entries[i],&blob,&error);if(FAILED(hr)){std::string message=error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"post70 compile";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i])));blob->Release();}
 }
 void Record(ID3D12GraphicsCommandList*c){
  if(!output||!c)throw std::runtime_error("post70 not created");if(recorded){barrier(c,merged,true);barrier(c,output,true);}
  auto pass=[&](UINT i,ID3D12Resource*src,ID3D12Resource*extra,ID3D12Resource*dst,UINT planes){c->SetComputeRootSignature(root);c->SetPipelineState(pso[i]);c->SetComputeRootShaderResourceView(0,src->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,coefficients[i]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,extra->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,dst->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,3,geometry,0);c->Dispatch(geometry[0]*geometry[1]/64,planes,1);};
  pass(0,main_input,skip_input,merged,32);barrier(c,merged,false);body.Record(c);pass(1,body.Output(),color_input,output,3);barrier(c,output,false);recorded=true;
 }
 ID3D12Resource*Output()const{return output;}
};
