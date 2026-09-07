#pragma once
#include "native_resident_table.h"
#include "native_split.h"
class NativeVitLinear {
 ID3D12Resource *input{},*residual{},*weights{},*output{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso{};UINT count{},outputs{},chunk_values{65536};bool recorded{},tiled_expand{},wave_expand{},wave_reduce{},blocked{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("ViT linear HRESULT="+std::to_string(unsigned(h)));}
 static ID3D12Resource* buffer(ID3D12Device*d,UINT64 bytes,const std::vector<float>*data=nullptr){D3D12_HEAP_PROPERTIES hp{};hp.Type=data?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=data?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,data?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));if(data){void*p=nullptr;D3D12_RANGE none{};ck(r->Map(0,&none,&p));std::memcpy(p,data->data(),bytes);r->Unmap(0,nullptr);r=NativeMaybeResident(d,r);}return r;}
 void barrier(ID3D12GraphicsCommandList*c,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeVitLinear()=default;NativeVitLinear(const NativeVitLinear&)=delete;
 ~NativeVitLinear(){for(auto*r:{input,residual,weights,output})if(r)r->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,ID3D12Resource*skip,UINT tokens,UINT inputs,UINT out,bool expand,const std::vector<float>&coefficients,const std::wstring&dir,bool decoder=false){
  const bool game_decoder=decoder&&inputs==1024&&out==512&&tokens==640;
  const bool game_up48=decoder&&inputs==512&&out==256&&tokens==2160;
  const bool game_up56=decoder&&inputs==256&&out==128&&tokens==8640;
  const bool game_up62=decoder&&inputs==128&&out==64&&tokens==34560;
  const bool game_up66=decoder&&inputs==64&&out==32&&tokens==138240;
  const bool decoder_shape=game_decoder||game_up48||game_up56||game_up62||game_up66||(inputs==1024&&out==512&&tokens==64)||(inputs==512&&out==256&&(tokens==64||tokens==256))||(inputs==256&&out==128&&(tokens==64||tokens==1024))||(inputs==128&&out==64&&(tokens==64||tokens==4096))||(inputs==64&&out==32&&(tokens==64||tokens==16384));
  if(input||!d||!src||(!decoder&&tokens!=64&&tokens!=256&&tokens!=640)||(!expand&&!skip)||(decoder?(expand||!decoder_shape):(expand?(inputs!=1024||out!=4096):(out!=1024||(inputs!=1024&&inputs!=4096))))||coefficients.size()!=size_t(inputs)*out+(expand?0:out))throw std::runtime_error("ViT/decoder linear contract");
  UINT64 output_values=game_decoder?UINT64(60)*36*out:UINT64(tokens)*out*(decoder?4:1);
  if(decoder&&(src->GetDesc().Width<UINT64(tokens)*inputs*4||skip->GetDesc().Width<output_values*4))throw std::runtime_error("decoder buffer capacity");
  input=src;input->AddRef();residual=skip?skip:src;residual->AddRef();count=tokens;outputs=out;weights=buffer(d,coefficients.size()*4,&coefficients);output=buffer(d,output_values*4);
  const wchar_t*resident=_wgetenv(L"DLSS5_TEST_RESIDENT_VIT_LINEAR");if(resident&&wcscmp(resident,L"0")&&wcscmp(resident,L"1"))throw std::runtime_error("invalid resident ViT weights flag");
  if(!decoder&&resident&&!wcscmp(resident,L"1")){auto*local=NativeResidentTable(d,weights);weights->Release();weights=local;}
  const wchar_t*wave_flag=_wgetenv(L"DLSS5_TEST_WAVE_VIT_EXPAND");if(wave_flag&&wcscmp(wave_flag,L"0")&&wcscmp(wave_flag,L"1"))throw std::runtime_error("invalid wave ViT expand flag");wave_expand=expand&&wave_flag&&!wcscmp(wave_flag,L"1");
  const wchar_t*reduce_flag=_wgetenv(L"DLSS5_TEST_WAVE_VIT_REDUCE");if(reduce_flag&&wcscmp(reduce_flag,L"0")&&wcscmp(reduce_flag,L"1"))throw std::runtime_error("invalid wave reduce flag");wave_reduce=!expand&&!decoder&&reduce_flag&&!wcscmp(reduce_flag,L"1");
  const wchar_t*chunk_flag=_wgetenv(L"DLSS5_TEST_VIT_EXPAND_CHUNK2");if(chunk_flag&&wcscmp(chunk_flag,L"0")&&wcscmp(chunk_flag,L"1"))throw std::runtime_error("invalid ViT expand chunk flag");if(wave_expand&&chunk_flag&&!wcscmp(chunk_flag,L"1"))chunk_values=131072;
  // Register-blocked expand/reduce pair sharing an f16 hidden layer; whole-stage single dispatch.
  if(const wchar_t*bf=_wgetenv(L"DLSS5_TEST_BLOCKED_VIT")){if(wcscmp(bf,L"0")&&wcscmp(bf,L"1"))throw std::runtime_error("invalid blocked ViT flag");blocked=!wcscmp(bf,L"1")&&(wave_expand||wave_reduce);if(blocked)chunk_values=tokens*out;}
  // Wave-matrix decoder entry/upsample projection (f16 weights, exact halves).
  bool wave_decoder=false;if(decoder){if(const wchar_t*wd=_wgetenv(L"DLSS5_TEST_WAVE_DECODER_LINEAR")){if(wcscmp(wd,L"0")&&wcscmp(wd,L"1"))throw std::runtime_error("invalid wave decoder flag");wave_decoder=!wcscmp(wd,L"1")&&tokens%16==0;}}
  if(wave_decoder){
   size_t matrix_values=size_t(inputs)*out;std::vector<float>packed(matrix_values/2+out);
   for(size_t i=0;i<matrix_values;i++){uint32_t bits;std::memcpy(&bits,&coefficients[i],4);uint32_t m=bits&0x7fffffffu;uint16_t h=uint16_t((bits>>16)&0x8000);if(m){int e=int(m>>23)-112;if(e<=0||e>=31||(m&0x1fff))throw std::runtime_error("decoder weight not exact half");h|=uint16_t((e<<10)|((m&0x7fffff)>>13));}std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&h,2);}
   std::memcpy(packed.data()+matrix_values/2,coefficients.data()+matrix_values,out*4);
   weights->Release();weights=buffer(d,packed.size()*4,&packed);
   blocked=true;chunk_values=tokens*out;
  }
  if(wave_expand||wave_reduce){
   size_t matrix_values=size_t(inputs)*out;std::vector<float>packed(matrix_values/2+(wave_reduce?out:0));
   for(size_t i=0;i<matrix_values;i++){uint32_t bits;std::memcpy(&bits,&coefficients[i],4);uint32_t m=bits&0x7fffffffu;uint16_t h=uint16_t((bits>>16)&0x8000);if(m){int e=int(m>>23)-112;if(e<=0||e>=31||(m&0x1fff))throw std::runtime_error("ViT expand weight not exact half");h|=uint16_t((e<<10)|((m&0x7fffff)>>13));}std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&h,2);}
   if(wave_reduce)std::memcpy(packed.data()+matrix_values/2,coefficients.data()+matrix_values,out*4);
   weights->Release();weights=nullptr;weights=buffer(d,packed.size()*4,&packed);
   const wchar_t*local_flag=_wgetenv(L"DLSS5_TEST_RESIDENT_WAVE_VIT_EXPAND");if(local_flag&&wcscmp(local_flag,L"0")&&wcscmp(local_flag,L"1"))throw std::runtime_error("invalid resident wave expand flag");
   if(wave_reduce||(local_flag&&!wcscmp(local_flag,L"1"))){auto*local=NativeResidentTable(d,weights);weights->Release();weights=local;}
  }
D3D12_ROOT_PARAMETER params[5]{};for(UINT i=0;i<3;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[4].Constants={0,0,2};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=5;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;ck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();error=nullptr;
  const wchar_t*flag=_wgetenv(L"DLSS5_TEST_TILED_VIT_EXPAND");if(flag&&wcscmp(flag,L"0")&&wcscmp(flag,L"1"))throw std::runtime_error("invalid tiled expand flag");tiled_expand=expand&&flag&&!wcscmp(flag,L"1");
  auto in_text=std::to_string(inputs),out_text=std::to_string(out);D3D_SHADER_MACRO macros[]={{"INPUT_CHANNELS",in_text.c_str()},{"OUTPUT_CHANNELS",out_text.c_str()},{"EXPAND",expand?"1":"0"},{"DECODER_ENTRY",decoder?"1":"0"},{nullptr,nullptr}};
  if(wave_expand&&tiled_expand)throw std::runtime_error("conflicting ViT expand modes");
  auto hr=wave_decoder?D3DReadFileToBlob((dir+L"\\native_wave_decoder_"+std::to_wstring(inputs)+L"_"+std::to_wstring(out)+L".cso").c_str(),&blob):blocked?D3DReadFileToBlob((dir+(wave_expand?L"\\native_wave_vit_expand_blocked.cso":inputs==4096?L"\\native_wave_vit_reduce_blocked.cso":L"\\native_wave_vit_reduce_blocked_1024.cso")).c_str(),&blob):wave_reduce?D3DReadFileToBlob((dir+L"\\native_wave_vit_reduce_"+std::to_wstring(inputs)+L".cso").c_str(),&blob):wave_expand?D3DReadFileToBlob((dir+L"\\native_wave_vit_expand.cso").c_str(),&blob):CompileNativeShader(dir+L"\\native_vit_linear.hlsl",macros,tiled_expand?"expand_tiled":"main",&blob,&error);if(FAILED(hr)){std::string message=error?std::string((const char*)error->GetBufferPointer(),error->GetBufferSize()):"ViT shader compilation";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));blob->Release();
 }
 void Record(ID3D12GraphicsCommandList*c){if(recorded)barrier(c,true);c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,residual->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,output->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,1,&count,0);for(UINT base=0;base<count*outputs;base+=chunk_values){c->SetComputeRoot32BitConstants(4,1,&base,1);UINT size=std::min(UINT(chunk_values),count*outputs-base);if(blocked)c->Dispatch(size/outputs/16,outputs/(outputs>=64?64:32),1);else if(wave_expand||wave_reduce)c->Dispatch(size/outputs/16,outputs/16,1);else if(tiled_expand)c->Dispatch(outputs/32,size/outputs/8,1);else c->Dispatch((size+63)/64,1,1);}barrier(c,false);recorded=true;}
 ID3D12Resource* Output()const{return output;}
 UINT ChunkCount()const{return (count*outputs+chunk_values-1)/chunk_values;}
 void RecordChunk(ID3D12GraphicsCommandList*c,UINT chunk){
  if(chunk>=ChunkCount())throw std::runtime_error("linear chunk range");
  if(chunk==0&&recorded)barrier(c,true);
  c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,residual->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,output->GetGPUVirtualAddress());
  UINT base=chunk*chunk_values,constants[]={count,base};c->SetComputeRoot32BitConstants(4,2,constants,0);if(blocked)c->Dispatch(std::min(UINT(chunk_values),count*outputs-base)/outputs/16,outputs/(outputs>=64?64:32),1);else if(wave_expand||wave_reduce)c->Dispatch(std::min(UINT(chunk_values),count*outputs-base)/outputs/16,outputs/16,1);else if(tiled_expand)c->Dispatch(outputs/32,std::min(UINT(chunk_values),count*outputs-base)/outputs/8,1);else c->Dispatch((std::min(UINT(chunk_values),count*outputs-base)+63)/64,1,1);
  if(chunk+1==ChunkCount()){barrier(c,false);recorded=true;}
 }
};
