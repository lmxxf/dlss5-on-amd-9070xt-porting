#pragma once
#include "native_resident_table.h"
#include "native_preblock_runtime.h"
#include "native_shader_cache.h"
#include "native_network_timestamps.h"
#include "native_matrix_workspace.h"
class NativeC64 {
 NativeMatrixWorkspace*workspace{};
 ID3D12Resource*qkv_weights{},*qkv_raw{};ID3D12PipelineState*qkv_pso{};bool matrix_qkv{},wave_scores{};
 ID3D12Resource*matrix_weights{},*matrix_input{};ID3D12PipelineState*pack_pso{};bool matrix_expand{},pack_matrix{},wave_expand{},wave_contract{},blocked_ffn{},wave_project{};ID3D12Resource*project_weights[2]{};ID3D12Resource*qkv_norm{};ID3D12PipelineState*normalize_pso{};bool direct_attention{};
 ID3D12Resource*input{};ID3D12Resource*weights[2]{};ID3D12Resource*result[3]{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso[3]{};UINT geometry[2]{},channel_count{64};bool recorded{};
 ID3D12Resource*scratch[2]{};ID3D12PipelineState*split_pso[3]{};bool split_ffn{},tiled_contract{},tiled_expand{},tiled_projection{};
 static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("C64 HRESULT="+std::to_string(unsigned(hr)));}
 static ID3D12Resource* Buffer(ID3D12Device*d,UINT64 bytes,const std::vector<float>*data=nullptr){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=data?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=data?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ID3D12Resource*r=nullptr;Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,data?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));if(data){void*p=nullptr;D3D12_RANGE empty{};Check(r->Map(0,&empty,&p));std::memcpy(p,data->data(),bytes);r->Unmap(0,nullptr);r=NativeMaybeResident(d,r);}return r;
 }
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeC64()=default;NativeC64(const NativeC64&)=delete;
 ~NativeC64(){if(qkv_weights)qkv_weights->Release();if(qkv_raw)qkv_raw->Release();if(qkv_pso)qkv_pso->Release();if(matrix_input)matrix_input->Release();if(pack_pso)pack_pso->Release();if(matrix_weights)matrix_weights->Release();for(auto*r:project_weights)if(r)r->Release();if(qkv_norm)qkv_norm->Release();if(normalize_pso)normalize_pso->Release();if(input)input->Release();for(auto*r:weights)if(r)r->Release();for(auto*r:result)if(r)r->Release();for(auto*r:scratch)if(r)r->Release();if(root)root->Release();for(auto*p:pso)if(p)p->Release();for(auto*p:split_pso)if(p)p->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT width,UINT height,const std::vector<float>&fw,const std::vector<float>&aw,const std::wstring&dir,bool raw_output=false,UINT channels=64,bool fast_fp8=false,bool split=false,bool tile_contract=false,bool tile_expand=false,bool tile_projection=false,bool use_matrix=false,bool pack_input=false,bool use_matrix_qkv=false,NativeMatrixWorkspace*shared=nullptr,bool use_wave=false,bool use_wave_contract=false,bool use_wave_scores=false){
  if(use_wave_scores&&(!use_matrix_qkv||(channels!=64&&channels!=128&&channels!=256)))throw std::runtime_error("wave scores require matrix QKV C64/C128/C256");wave_scores=use_wave_scores;
  if(use_wave_contract&&!use_wave)throw std::runtime_error("wave contract requires wave expand");wave_contract=use_wave_contract;
  // Register-blocked FFN with f16 hidden storage: same K32/H sequence per output, less traffic.
  if(const wchar_t*bf=_wgetenv(L"DLSS5_TEST_BLOCKED_FFN")){if(wcscmp(bf,L"0")&&wcscmp(bf,L"1"))throw std::runtime_error("invalid blocked FFN flag");blocked_ffn=!wcscmp(bf,L"1")&&wave_contract;}
  if(use_wave&&(!pack_input||(channels!=64&&channels!=128&&channels!=256)))throw std::runtime_error("wave expand requires packed C64/C128/C256");wave_expand=use_wave;
  if(shared&&use_matrix_qkv){shared->Validate(d,UINT64(width)*height*channels);workspace=shared;}
  if(use_matrix_qkv&&!pack_input)throw std::runtime_error("matrix QKV requires packed matrix mode");matrix_qkv=use_matrix_qkv;
  if(pack_input&&!use_matrix)throw std::runtime_error("packing requires matrix expand");pack_matrix=pack_input;
  if(use_matrix&&(!split||(channels!=64&&channels!=128&&channels!=256)))throw std::runtime_error("matrix expand requires split C64/C64/C128/C256");matrix_expand=use_matrix;
  if(tile_contract&&(!split||UINT64(width)*height/8>65535))throw std::runtime_error("tiled contract geometry");tiled_contract=tile_contract;if(tile_expand&&!tile_contract)throw std::runtime_error("tiled expand requires tiled contract");tiled_expand=tile_expand;if(tile_projection&&!tile_expand)throw std::runtime_error("tiled projection requires tiled expand");tiled_projection=tile_projection;
  if(split&&(fast_fp8||UINT64(width)*height*4*channels>0xffffffffull))throw std::runtime_error("split FFN experiment contract");split_ffn=split;
  if(input||!d||!src||!width||!height||width%8||height%8||(channels!=64&&channels!=128&&channels!=256)||fw.size()!=9*channels*channels+channels||aw.size()!=4*channels*channels+(channels/32)*4096+channels/32+channels)throw std::runtime_error("multihead contract");input=src;input->AddRef();geometry[0]=width;geometry[1]=height;channel_count=channels;
  weights[0]=Buffer(d,fw.size()*4,&fw);weights[1]=Buffer(d,aw.size()*4,&aw);for(auto&r:result)r=Buffer(d,UINT64(width)*height*channels*4);
  auto matrix_file=[&](const wchar_t*stem){return dir+L"\\"+stem+(channels==256?L"":L"_c"+std::to_wstring(channels))+L".cso";};
  if(matrix_expand){
   std::vector<float>packed((wave_contract?8:4)*channels*channels/2);
   for(UINT block=0;block<4*channels/32;block++)for(UINT g=0;g<channels/32;g++)for(UINT row=0;row<32;row++)for(UINT j=0;j<32;j++){
    float v=fw[(block*32+row)*channels+g*32+j];uint32_t bits;std::memcpy(&bits,&v,4);uint32_t mag=bits&0x7fffffffu;uint16_t half=uint16_t((bits>>16)&0x8000);
    if(mag){int exponent=int(mag>>23)-112;if(exponent<=0||exponent>=31||(mag&0x1fff))throw std::runtime_error("matrix weight not exact normal half");half|=uint16_t((exponent<<10)|((mag&0x7fffff)>>13));}
    size_t dst=(wave_expand?((block*32+row)*channels+g*32+j):(((block*(channels/32)+g)*32+row)*32+j))*2;std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+dst,&half,2);
   }
   if(wave_contract)for(size_t i=0;i<4ull*channels*channels;i++){
    float v=fw[4ull*channels*channels+i];uint32_t bits;std::memcpy(&bits,&v,4);uint32_t mag=bits&0x7fffffffu;uint16_t half=uint16_t((bits>>16)&0x8000);
    if(mag){int e=int(mag>>23)-112;if(e<=0||e>=31||(mag&0x1fff))throw std::runtime_error("contract weight not exact half");half|=uint16_t((e<<10)|((mag&0x7fffff)>>13));}
    std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+(4ull*channels*channels+i)*2,&half,2);
   }
   matrix_weights=Buffer(d,packed.size()*4,&packed);
  }
  if(const wchar_t*pf=_wgetenv(L"DLSS5_TEST_WAVE_PROJECT")){if(wcscmp(pf,L"0")&&wcscmp(pf,L"1"))throw std::runtime_error("invalid wave project flag");wave_project=!wcscmp(pf,L"1")&&wave_contract;}
  if(wave_project){
   // [0]=FFN output projection (fw 8*MATRIX, scale 9*MATRIX); [1]=attention projection (aw 3*MATRIX, scale SCALE_OFFSET+HEADS).
   const size_t matrix=size_t(channels)*channels,heads=channels/32;
   const float*sources[2]={fw.data()+8*matrix,aw.data()+3*matrix};const float*scales[2]={fw.data()+9*matrix,aw.data()+4*matrix+heads*4096+heads};
   for(UINT k=0;k<2;k++){
    std::vector<float>packed(matrix/2+channels);
    for(size_t i=0;i<matrix;i++){uint32_t bits;std::memcpy(&bits,sources[k]+i,4);uint32_t mag=bits&0x7fffffffu;uint16_t half=uint16_t((bits>>16)&0x8000);if(mag){int e=int(mag>>23)-112;if(e<=0||e>=31||(mag&0x1fff))throw std::runtime_error("projection weight not exact normal half");half|=uint16_t((e<<10)|((mag&0x7fffff)>>13));}std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&half,2);}
    std::memcpy(packed.data()+matrix/2,scales[k],channels*4);
    project_weights[k]=Buffer(d,packed.size()*4,&packed);
   }
  }
  if(matrix_qkv){
   std::vector<float>packed(3*channels*channels/2);
   for(UINT block=0;block<3*channels/32;block++)for(UINT g=0;g<channels/32;g++)for(UINT row=0;row<32;row++)for(UINT j=0;j<32;j++){
    float v=aw[(block*32+row)*channels+g*32+j];uint32_t bits;std::memcpy(&bits,&v,4);uint32_t mag=bits&0x7fffffffu;uint16_t half=uint16_t((bits>>16)&0x8000);
    if(mag){int exponent=int(mag>>23)-112;if(exponent<=0||exponent>=31||(mag&0x1fff))throw std::runtime_error("matrix weight not exact normal half");half|=uint16_t((exponent<<10)|((mag&0x7fffff)>>13));}
    size_t dst=(((block*(channels/32)+g)*32+row)*32+j)*2;std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+dst,&half,2);
   }
   qkv_weights=Buffer(d,packed.size()*4,&packed);
  }
  D3D12_ROOT_PARAMETER params[5]{};for(UINT i=0;i<3;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[4].Constants={0,0,2};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=5;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();
  if(pack_matrix){
   if(workspace){matrix_input=workspace->packed;matrix_input->AddRef();}else matrix_input=Buffer(d,UINT64(width)*height*channels*2);
   blob=nullptr;Check(D3DReadFileToBlob(matrix_file(L"native_matrix_pack").c_str(),&blob));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};auto hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pack_pso));blob->Release();Check(hr);
  }
  if(matrix_qkv){if(workspace){qkv_raw=workspace->qkv;qkv_raw->AddRef();}else qkv_raw=Buffer(d,UINT64(width)*height*channels*12);blob=nullptr;Check(D3DReadFileToBlob(matrix_file(L"native_matrix_qkv").c_str(),&blob));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};auto hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&qkv_pso));blob->Release();Check(hr);}
  const wchar_t*pad=_wgetenv(L"DLSS5_TEST_PAD_MULTIHEAD_LDS");if(pad&&wcscmp(pad,L"0")&&wcscmp(pad,L"1"))throw std::runtime_error("invalid multihead LDS flag");
  const char*entry[]={"ffn","attention",tiled_projection?"tiled_attention_project":"projection"};auto path=dir+L"\\native_c64.hlsl";auto channel_text=std::to_string(channels);D3D_SHADER_MACRO macros[]={{"RAW_OUTPUT",raw_output?"1":"0"},{"CHANNELS",channel_text.c_str()},{"NATIVE_PRECOMPUTED_QKV",matrix_qkv?"1":"0"},{"NATIVE_FAST_FP8",fast_fp8?"1":"0"},{"NATIVE_PAD_MULTIHEAD_LDS",pad&&!wcscmp(pad,L"1")?"1":"0"},{nullptr,nullptr}};
  // Direct attention: normalized f16 window-major Q/K/V, wave loads, no LDS staging.
  if(const wchar_t*da=_wgetenv(L"DLSS5_TEST_DIRECT_ATTENTION")){if(wcscmp(da,L"0")&&wcscmp(da,L"1"))throw std::runtime_error("invalid direct attention flag");direct_attention=!wcscmp(da,L"1")&&matrix_qkv&&wave_scores;}
  if(direct_attention){
   if(workspace){workspace->Validate(d,UINT64(width)*height*channels);qkv_norm=workspace->norm;qkv_norm->AddRef();}else qkv_norm=Buffer(d,UINT64(width)*height*3*channels*2);
   ID3DBlob*nb=nullptr;Check(D3DReadFileToBlob(matrix_file(L"native_wave_attention_normalize").c_str(),&nb));D3D12_COMPUTE_PIPELINE_STATE_DESC np{};np.pRootSignature=root;np.CS={nb->GetBufferPointer(),nb->GetBufferSize()};auto nh=d->CreateComputePipelineState(&np,IID_PPV_ARGS(&normalize_pso));nb->Release();Check(nh);
  }
  const wchar_t*av_flag=_wgetenv(L"DLSS5_TEST_WAVE_MULTIHEAD_AV");if(av_flag&&wcscmp(av_flag,L"0")&&wcscmp(av_flag,L"1"))throw std::runtime_error("invalid multihead AV flag");const bool wave_av=wave_scores&&av_flag&&!wcscmp(av_flag,L"1");
  for(UINT i=0;i<3;i++){if(split_ffn&&i==0)continue;macros[0].Definition=(raw_output&&i==2)?"1":"0";blob=nullptr;error=nullptr;HRESULT hr=(i==2&&wave_project)?D3DReadFileToBlob(matrix_file(raw_output?L"native_wave_project_raw":L"native_wave_project").c_str(),&blob):(i==1&&direct_attention)?D3DReadFileToBlob(matrix_file(L"native_wave_attention_direct").c_str(),&blob):(i==1&&wave_scores)?D3DReadFileToBlob(matrix_file(wave_av?L"native_wave_av":L"native_wave_scores").c_str(),&blob):CompileNativeShader(path,macros,entry[i],&blob,&error);if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"C64 shader failed";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};Check(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i])));blob->Release();}
  if(split_ffn){
   scratch[0]=Buffer(d,UINT64(width)*height*channels*16);scratch[1]=Buffer(d,UINT64(width)*height*channels*4);
   const char*names[]={tiled_expand?"tiled_ffn_expand":"split_ffn_expand",tiled_contract?"tiled_ffn_contract":"split_ffn_contract",tiled_projection?"tiled_ffn_project":"split_ffn_project"};macros[0].Definition="0";
   for(UINT i=0;i<3;i++){blob=nullptr;error=nullptr;auto hr=(i==2&&wave_project)?D3DReadFileToBlob(matrix_file(L"native_wave_project").c_str(),&blob):(i==1&&wave_contract)?D3DReadFileToBlob(matrix_file(blocked_ffn?L"native_wave_ffn_contract_blocked":L"native_wave_contract").c_str(),&blob):(i==0&&matrix_expand)?D3DReadFileToBlob(matrix_file(wave_expand?(blocked_ffn?L"native_wave_ffn_expand_blocked":L"native_wave_expand"):pack_matrix?L"native_matrix_expand_packed":L"native_matrix_expand").c_str(),&blob):CompileNativeShader(path,macros,names[i],&blob,&error);if(error)error->Release();Check(hr);D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&split_pso[i]));blob->Release();Check(hr);}
  }
 }
 void Record(ID3D12GraphicsCommandList*c,NativeNetworkTimestamps*timer=nullptr,const char*label="core"){
  if(recorded)for(auto*r:result)Barrier(c,r,true);
  if(split_ffn){
   if(recorded)for(auto*r:scratch)Barrier(c,r,true);
   if(pack_matrix){
    if(workspace?workspace->packed_readable:recorded)Barrier(c,matrix_input,true);
    c->SetComputeRootSignature(root);c->SetPipelineState(pack_pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,matrix_input->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);
    UINT n=geometry[0]*geometry[1]*channel_count/128;c->Dispatch(std::min(n,65535u),(n+65534)/65535,1);Barrier(c,matrix_input,false);if(workspace)workspace->packed_readable=true;
    if(timer)timer->Mark(c,std::string(label)+"_input_pack");
   }
   for(UINT i=0;i<3;i++){auto*src=i?scratch[i-1]:(pack_matrix?matrix_input:input);auto*dst=i==2?result[0]:scratch[i];c->SetComputeRootSignature(root);c->SetPipelineState(split_pso[i]);c->SetComputeRootShaderResourceView(0,src->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,(i==2&&wave_project)?project_weights[0]->GetGPUVirtualAddress():((i==0&&matrix_expand)||(i==1&&wave_contract)?matrix_weights:weights[0])->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,input->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,dst->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);UINT64 groups=(UINT64(geometry[0])*geometry[1]*channel_count*(i==0?4:1)+63)/64;if(i==2&&wave_project)c->Dispatch(geometry[0]*geometry[1]/16,channel_count/64,1);else if(i==1&&wave_contract)c->Dispatch(geometry[0]*geometry[1]/16,channel_count/(blocked_ffn?64:16),1);else if(i==0&&wave_expand)c->Dispatch(geometry[0]*geometry[1]/16,4*channel_count/(blocked_ffn?64:16),1);else if(i==0&&matrix_expand)c->Dispatch((geometry[0]*geometry[1]+31)/32,4*channel_count/32,1);else if(i==0&&tiled_expand)c->Dispatch(channel_count*4/32,geometry[0]*geometry[1]/8,1);else if((i==1&&tiled_contract)||(i==2&&tiled_projection))c->Dispatch(channel_count/32,geometry[0]*geometry[1]/8,1);else c->Dispatch(UINT(std::min<UINT64>(groups,65535)),UINT((groups+65534)/65535),1);Barrier(c,dst,false);if(timer)timer->Mark(c,std::string(label)+"_ffn_part"+std::to_string(i));}
  }
  if(matrix_qkv){
   Barrier(c,matrix_input,true);if(workspace?workspace->qkv_readable:recorded)Barrier(c,qkv_raw,true);
   c->SetComputeRootSignature(root);c->SetPipelineState(pack_pso);c->SetComputeRootShaderResourceView(0,result[0]->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,matrix_input->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);
   UINT n=geometry[0]*geometry[1]*channel_count/128;c->Dispatch(std::min(n,65535u),(n+65534)/65535,1);Barrier(c,matrix_input,false);if(timer)timer->Mark(c,std::string(label)+"_qkv_pack");
   c->SetPipelineState(qkv_pso);c->SetComputeRootShaderResourceView(0,matrix_input->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,qkv_weights->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,qkv_raw->GetGPUVirtualAddress());c->Dispatch((geometry[0]*geometry[1]+31)/32,channel_count/32,3);Barrier(c,qkv_raw,false);if(workspace)workspace->qkv_readable=true;if(timer)timer->Mark(c,std::string(label)+"_qkv_matrix");
  }
  if(direct_attention){
   if(workspace?workspace->norm_readable:recorded)Barrier(c,qkv_norm,true);
   c->SetComputeRootSignature(root);c->SetPipelineState(normalize_pso);c->SetComputeRootShaderResourceView(0,qkv_raw->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights[1]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,qkv_raw->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,qkv_norm->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);
   UINT n=(geometry[0]*geometry[1]*(channel_count/32)+63)/64;c->Dispatch(std::min(n,65535u),(n+65534)/65535,1);Barrier(c,qkv_norm,false);if(workspace)workspace->norm_readable=true;if(timer)timer->Mark(c,std::string(label)+"_qkv_normalize");
  }
  for(UINT i=split_ffn?1:0;i<3;i++){c->SetComputeRootSignature(root);c->SetPipelineState(pso[i]);c->SetComputeRootShaderResourceView(0,(i?result[i-1]:input)->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,((i==2&&wave_project)?project_weights[1]:weights[i?1:0])->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,(i==2?result[0]:(i==1&&direct_attention)?qkv_norm:(matrix_qkv?qkv_raw:input))->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,result[i]->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);if(i==2&&wave_project)c->Dispatch(geometry[0]*geometry[1]/16,channel_count/64,1);else if(i==2&&tiled_projection)c->Dispatch(channel_count/32,geometry[0]*geometry[1]/8,1);else c->Dispatch(geometry[0]*geometry[1]/64,i==1?channel_count/32:1,1);Barrier(c,result[i],false);if(timer)timer->Mark(c,std::string(label)+(i==0?"_ffn":i==1?"_attention":"_projection"));}recorded=true;
 }
 ID3D12Resource* Output()const{return result[2];}
};
