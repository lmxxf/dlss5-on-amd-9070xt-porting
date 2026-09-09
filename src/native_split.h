#pragma once
#include "native_pinned_resource.h"
#include <cmath>
#include "native_resident_table.h"
#include "native_preblock_runtime.h"
#include "native_shader_cache.h"
#include "native_network_timestamps.h"
#include "native_matrix_workspace.h"
class NativeSplit {
 NativeMatrixWorkspace*workspace{};ID3D12Resource*qkv_weights{};ID3D12PipelineState*aux_pso[2]{};ID3D12PipelineState*direct_pso[3]{};ID3D12Resource*qkv_weights8{};bool direct_attention{};bool matrix_attention{},wave_ffwd{},parallel_ffwd{};
 ID3D12Resource*input{};ID3D12Resource*weights[3]{};ID3D12Resource*result[4]{};ID3D12Resource*project_weights[2]{};bool wave_project{},copy8{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso[4]{};UINT geometry[2]{};bool recorded{},tiled_projection{},shared_ffwd{};
 static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("C64 HRESULT="+std::to_string(unsigned(hr)));}
 static ID3D12Resource* Buffer(ID3D12Device*d,UINT64 bytes,const std::vector<float>*data=nullptr){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=data?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=data?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ID3D12Resource*r=nullptr;Check(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&rd,data?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));if(data){void*p=nullptr;D3D12_RANGE empty{};Check(r->Map(0,&empty,&p));std::memcpy(p,data->data(),bytes);r->Unmap(0,nullptr);r=NativeMaybeResident(d,r);}return r;
 }
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeSplit()=default;NativeSplit(const NativeSplit&)=delete;
 ~NativeSplit(){for(auto*p:direct_pso)if(p)p->Release();if(qkv_weights8)qkv_weights8->Release();if(qkv_weights)qkv_weights->Release();for(auto*r:project_weights)if(r)r->Release();for(auto*p:aux_pso)if(p)p->Release();if(input)input->Release();for(auto*r:weights)if(r)r->Release();for(auto*r:result)if(r)r->Release();if(root)root->Release();for(auto*p:pso)if(p)p->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT width,UINT height,const std::vector<float>&fw,const std::vector<float>&fp,const std::vector<float>&aw,const std::wstring&dir,bool raw_output=false,NativeMatrixWorkspace*shared=nullptr){
  if(input||!d||!src||!width||!height||width%8||height%8||fw.size()!=524288||fp.size()!=262656||aw.size()!=1114640)throw std::runtime_error("split contract");
  input=src;input->AddRef();geometry[0]=width;geometry[1]=height;
  const wchar_t*tile=_wgetenv(L"DLSS5_TEST_SPLIT_PROJECTION");if(tile&&wcscmp(tile,L"0")&&wcscmp(tile,L"1"))throw std::runtime_error("invalid split projection flag");tiled_projection=tile&&!wcscmp(tile,L"1");
  if(tiled_projection&&UINT64(width)*height/8>65535)throw std::runtime_error("split tiled extent");
  const wchar_t*share=_wgetenv(L"DLSS5_TEST_SPLIT_FFWD");if(share&&wcscmp(share,L"0")&&wcscmp(share,L"1"))throw std::runtime_error("invalid shared FFWD flag");shared_ffwd=share&&!wcscmp(share,L"1");if(shared_ffwd&&UINT64(width)*height>65535)throw std::runtime_error("shared FFWD extent");
  weights[0]=Buffer(d,fw.size()*4,&fw);weights[1]=Buffer(d,fp.size()*4,&fp);weights[2]=Buffer(d,aw.size()*4,&aw);
  for(auto&r:result)r=Buffer(d,UINT64(width)*height*512*4);
  const wchar_t*wave_flag=_wgetenv(L"DLSS5_TEST_WAVE_SPLIT_FFWD");if(wave_flag&&wcscmp(wave_flag,L"0")&&wcscmp(wave_flag,L"1"))throw std::runtime_error("invalid wave split FFWD flag");wave_ffwd=wave_flag&&!wcscmp(wave_flag,L"1");
  const wchar_t*parallel_flag=_wgetenv(L"DLSS5_TEST_PARALLEL_SPLIT_FFWD");if(parallel_flag&&wcscmp(parallel_flag,L"0")&&wcscmp(parallel_flag,L"1"))throw std::runtime_error("invalid parallel split FFWD flag");parallel_ffwd=parallel_flag&&!wcscmp(parallel_flag,L"1");wave_ffwd=wave_ffwd||parallel_ffwd;
  if(wave_ffwd){
   if(UINT64(width)*height/16>65535)throw std::runtime_error("wave split FFWD extent");
   std::vector<float>packed(fw.size()/2);for(size_t i=0;i<fw.size();i++){uint32_t bits;std::memcpy(&bits,&fw[i],4);uint32_t m=bits&0x7fffffffu;uint16_t h=uint16_t((bits>>16)&0x8000);if(m){int e=int(m>>23)-112;if(e<=0||e>=31||(m&0x1fff))throw std::runtime_error("split FFWD weights not exact half");h|=uint16_t((e<<10)|((m&0x7fffff)>>13));}std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&h,2);}
   auto*u=Buffer(d,packed.size()*4,&packed);auto*local=NativeResidentTable(d,u);u->Release();weights[0]->Release();weights[0]=local;
  }
  D3D12_ROOT_PARAMETER params[6]{};for(UINT i=0;i<3;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[4].Constants={0,0,2};params[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[5].Descriptor.ShaderRegister=1;D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=6;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();
  const wchar_t*matrix_flag=_wgetenv(L"DLSS5_TEST_MATRIX_SPLIT_ATTENTION");if(matrix_flag&&wcscmp(matrix_flag,L"0")&&wcscmp(matrix_flag,L"1"))throw std::runtime_error("invalid split matrix attention flag");
  matrix_attention=matrix_flag&&!wcscmp(matrix_flag,L"1");
  if(matrix_attention){
   if(!shared)throw std::runtime_error("split matrix attention requires graph workspace");shared->Validate(d,UINT64(width)*height*512);workspace=shared;
   std::vector<float>packed(3*512*512/2);
   for(UINT block=0;block<48;block++)for(UINT g=0;g<16;g++)for(UINT row=0;row<32;row++)for(UINT j=0;j<32;j++){
    float v=aw[(block*32+row)*512+g*32+j];uint32_t bits;std::memcpy(&bits,&v,4);uint32_t m=bits&0x7fffffffu;uint16_t h=uint16_t((bits>>16)&0x8000);if(m){int e=int(m>>23)-112;if(e<=0||e>=31||(m&0x1fff))throw std::runtime_error("split QKV weight not exact half");h|=uint16_t((e<<10)|((m&0x7fffff)>>13));}
    size_t dst=(((block*16+g)*32+row)*32+j)*2;std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+dst,&h,2);
   }
   auto*upload=Buffer(d,packed.size()*4,&packed);qkv_weights=NativeResidentTable(d,upload);upload->Release();
   const wchar_t*names[]={L"native_matrix_pack_c512.cso",L"native_matrix_qkv_c512.cso"};
   for(UINT i=0;i<2;i++){blob=nullptr;Check(D3DReadFileToBlob((dir+L"\\"+names[i]).c_str(),&blob));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};auto hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&aux_pso[i]));blob->Release();Check(hr);}
  }
  // FAST PATH: direct attention path for C512 (FP8 pack -> fused QKV+normalize -> direct attention with FP8 Q/K/V and FP8 output -> projection with direct FP8 A loads).
  if(const wchar_t*da=_wgetenv(L"DLSS5_SPLIT_DIRECT_ATTENTION")){if(wcscmp(da,L"0")&&wcscmp(da,L"1"))throw std::runtime_error("invalid split direct attention flag");const wchar_t*f8=_wgetenv(L"DLSS5_FP8_OPERANDS");direct_attention=!wcscmp(da,L"1")&&matrix_attention&&f8&&!wcscmp(f8,L"1");}
  if(direct_attention){
   std::vector<float>packed8(3ull*512*512/4);unsigned char*out8=reinterpret_cast<unsigned char*>(packed8.data());
   for(size_t i=0;i<3ull*512*512;i++){float v=aw[i];uint32_t b;std::memcpy(&b,&v,4);uint32_t a=b&0x7fffffffu;uint8_t sg=uint8_t((b>>24)&0x80u);if(!a){out8[i]=sg;continue;}float m=std::fabs(v);if(m<0.015625f){float q=m*512.f;if(q!=std::floor(q)||q>7)throw std::runtime_error("split QKV weight not FP8-representable");out8[i]=uint8_t(sg|uint8_t(q));continue;}int e=int(a>>23)-127+7;if(e<1||e>15||(a&0xfffff)||(e==15&&((a>>20)&7)==7))throw std::runtime_error("split QKV weight not FP8-representable");out8[i]=uint8_t(sg|(e<<3)|((a>>20)&7));}
   auto*upload=Buffer(d,packed8.size()*4,&packed8);qkv_weights8=NativeResidentTable(d,upload);upload->Release();
   const wchar_t*names[]={L"native_matrix_pack_fp8_c512.cso",L"native_wave_qkv_normalize_fp8qkv_c512.cso",L"native_wave_attention_direct_fp8qkv_fp8act_c512.cso"};
   for(UINT i=0;i<3;i++){ID3DBlob*blob=nullptr;Check(D3DReadFileToBlob((dir+L"\\"+names[i]).c_str(),&blob));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};auto hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&direct_pso[i]));blob->Release();Check(hr);}
  }
  // Wave-matrix FFWD/attention projections: [0]=fp (matrix 0, scale 262144), [1]=aw (matrix 3*MATRIX, scale 4*MATRIX+16*4096+16).
  if(const wchar_t*wp=_wgetenv(L"DLSS5_TEST_WAVE_SPLIT_PROJECT")){if(wcscmp(wp,L"0")&&wcscmp(wp,L"1"))throw std::runtime_error("invalid wave split project flag");wave_project=!wcscmp(wp,L"1");}
  // FAST PATH (DLSS5_SPLIT_COPY8): the stage-1 projection also writes the E4M3 copy the attention path reads; the pack dispatch is skipped.
  {const wchar_t*c8=_wgetenv(L"DLSS5_SPLIT_COPY8");if(c8&&wcscmp(c8,L"0")&&wcscmp(c8,L"1"))throw std::runtime_error("invalid split copy8 flag");copy8=wave_project&&c8&&!wcscmp(c8,L"1");}
  if(wave_project){
   const size_t matrix=512ull*512;const float*sources[2]={fp.data(),aw.data()+3*matrix};const float*scales[2]={fp.data()+matrix,aw.data()+4*matrix+16*4096+16};
   for(UINT k=0;k<2;k++){
    const wchar_t*f8=_wgetenv(L"DLSS5_FP8_OPERANDS");const bool fp8=f8&&!wcscmp(f8,L"1");
    std::vector<float>packed(fp8?matrix/4+512:matrix/2+512);
    if(fp8){unsigned char*o8=reinterpret_cast<unsigned char*>(packed.data());for(size_t i=0;i<matrix;i++){float v=sources[k][i];uint32_t b;std::memcpy(&b,&v,4);uint32_t a=b&0x7fffffffu;uint8_t sg=uint8_t((b>>24)&0x80u);if(!a){o8[i]=sg;continue;}float m=std::fabs(v);if(m<0.015625f){float q=m*512.f;if(q!=std::floor(q)||q>7)throw std::runtime_error("split projection weight not FP8-representable");o8[i]=uint8_t(sg|uint8_t(q));continue;}int e=int(a>>23)-127+7;if(e<1||e>15||(a&0xfffff)||(e==15&&((a>>20)&7)==7))throw std::runtime_error("split projection weight not FP8-representable");o8[i]=uint8_t(sg|(e<<3)|((a>>20)&7));}std::memcpy(packed.data()+matrix/4,scales[k],512*4);}
    else{for(size_t i=0;i<matrix;i++){uint32_t bits;std::memcpy(&bits,sources[k]+i,4);uint32_t mag=bits&0x7fffffffu;uint16_t half=uint16_t((bits>>16)&0x8000);if(mag){int e=int(mag>>23)-112;if(e<=0||e>=31||(mag&0x1fff))throw std::runtime_error("split projection weight not exact half");half|=uint16_t((e<<10)|((mag&0x7fffff)>>13));}std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&half,2);}
    std::memcpy(packed.data()+matrix/2,scales[k],512*4);}
    project_weights[k]=Buffer(d,packed.size()*4,&packed);
    if(k==0){weights[1]->Release();weights[1]=Buffer(d,16);} // the f32 ffwd-projection copy is only read by the non-wave path
   }
  }
  const wchar_t*pad=_wgetenv(L"DLSS5_TEST_PAD_MULTIHEAD_LDS");if(pad&&wcscmp(pad,L"0")&&wcscmp(pad,L"1"))throw std::runtime_error("invalid multihead LDS flag");
  const char*entry[]={shared_ffwd?"ffwd_shared":"ffwd",tiled_projection?"tiled_split_project":"ffwd_projection","attention",tiled_projection?"tiled_attention_project":"projection"};D3D_SHADER_MACRO macros[]={{"RAW_OUTPUT","0"},{"CHANNELS","512"},{"NATIVE_PAD_MULTIHEAD_LDS",pad&&!wcscmp(pad,L"1")?"1":"0"},{nullptr,nullptr}};
  for(UINT i=0;i<4;i++){macros[0].Definition=raw_output&&i==3?"1":"0";auto path=dir+((i==0||(i==1&&!tiled_projection))?L"\\native_split.hlsl":L"\\native_c64.hlsl");blob=nullptr;error=nullptr;HRESULT hr=((i==1||i==3)&&wave_project)?D3DReadFileToBlob((dir+((i==3&&raw_output)?(direct_attention?L"\\native_wave_project_raw_fp8act_c512.cso":L"\\native_wave_project_raw_c512.cso"):(i==3&&direct_attention)?L"\\native_wave_project_fp8act_c512.cso":(i==1&&copy8)?L"\\native_wave_project_copy8_c512.cso":L"\\native_wave_project_c512.cso")).c_str(),&blob):(i==0&&wave_ffwd)?D3DReadFileToBlob((dir+(parallel_ffwd?L"\\native_wave_split_ffwd_parallel.cso":L"\\native_wave_split_ffwd.cso")).c_str(),&blob):(i==2&&matrix_attention)?D3DReadFileToBlob((dir+L"\\native_wave_split_attention.cso").c_str(),&blob):CompileNativeShader(path,macros,entry[i],&blob,&error);if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"split shader failed";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};Check(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i])));blob->Release();}

 }
 void Record(ID3D12GraphicsCommandList*c,NativeNetworkTimestamps*timer=nullptr){
  if(recorded)for(auto*r:result)Barrier(c,r,true);
  for(UINT i=0;i<4;i++){
   if(i==2&&direct_attention){
    if(workspace->packed_readable&&!copy8)Barrier(c,workspace->packed,true);if(workspace->norm_readable)Barrier(c,workspace->norm,true);
    if(!copy8){c->SetComputeRootSignature(root);c->SetPipelineState(direct_pso[0]);c->SetComputeRootShaderResourceView(0,result[1]->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,workspace->packed->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);
    {UINT n=geometry[0]*geometry[1]*2;c->Dispatch(std::min(n,65535u),(n+65534)/65535,1);}Barrier(c,workspace->packed,false);workspace->packed_readable=true;}else c->SetComputeRootSignature(root);if(timer)timer->Mark(c,"split_qkv_pack");
    c->SetPipelineState(direct_pso[1]);c->SetComputeRootShaderResourceView(0,workspace->packed->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,qkv_weights8->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,weights[2]->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,workspace->norm->GetGPUVirtualAddress());c->Dispatch(geometry[0]*geometry[1]/16,24,1);Barrier(c,workspace->norm,false);workspace->norm_readable=true;if(timer)timer->Mark(c,"split_qkv_matrix");
    c->SetPipelineState(direct_pso[2]);c->SetComputeRootShaderResourceView(0,result[1]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights[2]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,workspace->norm->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,result[2]->GetGPUVirtualAddress());c->Dispatch(geometry[0]*geometry[1]/64,16,1);Barrier(c,result[2],false);if(timer)timer->Mark(c,"split_stage2");
    continue;
   }
   if(i==2&&matrix_attention){
    if(workspace->packed_readable)Barrier(c,workspace->packed,true);if(workspace->qkv_readable)Barrier(c,workspace->qkv,true);
    c->SetComputeRootSignature(root);c->SetPipelineState(aux_pso[0]);c->SetComputeRootShaderResourceView(0,result[1]->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,workspace->packed->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);
    UINT n=geometry[0]*geometry[1]*4;c->Dispatch(std::min(n,65535u),(n+65534)/65535,1);Barrier(c,workspace->packed,false);workspace->packed_readable=true;
    if(timer)timer->Mark(c,"split_qkv_pack");
    c->SetPipelineState(aux_pso[1]);c->SetComputeRootShaderResourceView(0,workspace->packed->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,qkv_weights->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,workspace->qkv->GetGPUVirtualAddress());c->Dispatch((geometry[0]*geometry[1]+31)/32,16,3);Barrier(c,workspace->qkv,false);workspace->qkv_readable=true;
    if(timer)timer->Mark(c,"split_qkv_matrix");
   }
   c->SetComputeRootSignature(root);c->SetPipelineState(pso[i]);c->SetComputeRootShaderResourceView(0,(i?result[i-1]:input)->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,(((i==1||i==3)&&wave_project)?project_weights[i==1?0:1]:weights[i<2?i:2])->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,(i==3?result[1]:(i==2&&matrix_attention?workspace->qkv:input))->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,result[i]->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);if(i==1&&copy8){if(workspace->packed_readable)Barrier(c,workspace->packed,true);c->SetComputeRootUnorderedAccessView(5,workspace->packed->GetGPUVirtualAddress());}if((i==1||i==3)&&wave_project)c->Dispatch(geometry[0]*geometry[1]/16,8,1);else if(i==0&&wave_ffwd)c->Dispatch(geometry[0]*geometry[1]/16,parallel_ffwd?8:1,1);else if(i==0&&shared_ffwd)c->Dispatch(geometry[0]*geometry[1],1,1);else if(tiled_projection&&(i==1||i==3))c->Dispatch(16,geometry[0]*geometry[1]/8,1);else c->Dispatch(geometry[0]*geometry[1]/64,i==2?16:1,1);Barrier(c,result[i],false);if(i==1&&copy8){Barrier(c,workspace->packed,false);workspace->packed_readable=true;}if(timer)timer->Mark(c,"split_stage"+std::to_string(i));}recorded=true;
 }
 ID3D12Resource* Stage(UINT i)const{if(i>=4)throw std::runtime_error("split stage index");return result[i];}
 ID3D12Resource* Output()const{return result[3];}ID3D12Resource* Input()const{return input;}
};
