#pragma once
#include "native_pso.h"
#include "native_pinned_resource.h"
#include "native_resident_table.h"
#include "native_split.h"
class NativeVitQkv {
 ID3D12Resource *input{},*weights{},*raw{},*output{},*packed_weights{},*packed_input{};ID3D12RootSignature*root{};ID3D12PipelineState*pso[2]{},*pack_pso{};UINT count{};bool recorded{},tiled{},wave{},vit_tiled{},packed{},fused{};UINT block_m{1};ID3D12Resource*packed8{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("ViT QKV HRESULT="+std::to_string(unsigned(h)));}
 static ID3D12Resource* buffer(ID3D12Device*d,UINT64 bytes,const std::vector<float>*data=nullptr){D3D12_HEAP_PROPERTIES hp{};hp.Type=data?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=data?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource*r=nullptr;ck(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&rd,data?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));if(data){void*p=nullptr;D3D12_RANGE none{};ck(r->Map(0,&none,&p));std::memcpy(p,data->data(),bytes);r->Unmap(0,nullptr);r=NativeMaybeResident(d,r);}return r;}
 static void barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeVitQkv()=default;NativeVitQkv(const NativeVitQkv&)=delete;
 ~NativeVitQkv(){for(auto*r:{input,weights,raw,output,packed_weights,packed_input,packed8})if(r)r->Release();if(root)root->Release();for(auto*p:pso)if(p)p->Release();if(pack_pso)pack_pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT tokens,const std::vector<float>&coefficients,const std::wstring&dir){
  if(input||!d||!src||(tokens!=64&&tokens!=256&&tokens!=640)||coefficients.size()!=3145760)throw std::runtime_error("ViT QKV contract");input=src;input->AddRef();count=tokens;weights=buffer(d,coefficients.size()*4,&coefficients);raw=buffer(d,UINT64(tokens)*3072*4);output=buffer(d,UINT64(tokens)*3072*4);
  D3D12_ROOT_PARAMETER params[4]{};for(UINT i=0;i<2;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[3].Constants={0,0,1};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=4;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;ck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();
  const wchar_t*flag=_wgetenv(L"DLSS5_TEST_TILED_QKV");if(flag&&wcscmp(flag,L"0")&&wcscmp(flag,L"1"))throw std::runtime_error("invalid QKV flag");tiled=flag&&!wcscmp(flag,L"1");
  // Wave-matrix projection with f16 weights (exact halves required); normalize keeps the f32 table.
  const wchar_t*wave_flag=_wgetenv(L"DLSS5_TEST_WAVE_VIT_QKV");if(wave_flag&&wcscmp(wave_flag,L"0")&&wcscmp(wave_flag,L"1"))throw std::runtime_error("invalid wave ViT QKV flag");wave=wave_flag&&!wcscmp(wave_flag,L"1");
  if(wave){
   // 32 f32 head scales appended at byte 6291456 for the fused normalize.
   std::vector<float>packed(3145728/2+32);std::memcpy(packed.data()+3145728/2,coefficients.data()+3145728,32*4);
   for(size_t i=0;i<3145728;i++){uint32_t bits;std::memcpy(&bits,&coefficients[i],4);uint32_t m=bits&0x7fffffffu;uint16_t h=uint16_t((bits>>16)&0x8000);if(m){int e=int(m>>23)-112;if(e<=0||e>=31||(m&0x1fff))throw std::runtime_error("ViT QKV weight not exact normal half");h|=uint16_t((e<<10)|((m&0x7fffff)>>13));}std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&h,2);}
   /* FAST PATH (DLSS5_VIT_TILED): f16 weight tiles (n/16, k/32) of [k 32][j 16] at ((n/16)*32+k/32)*1024 within each 1024x1024 part; the fused_m4 kernel is built with NATIVE_VIT_TILED=1. */
   if(const wchar_t*vt=_wgetenv(L"DLSS5_VIT_TILED")){if(wcscmp(vt,L"0")&&wcscmp(vt,L"1"))throw std::runtime_error("invalid ViT tiled flag");if(!wcscmp(vt,L"1")){vit_tiled=true;unsigned char*b=reinterpret_cast<unsigned char*>(packed.data());std::vector<unsigned char>t(3145728*2);for(size_t n=0;n<3072/16;n++)for(size_t g=0;g<32;g++)for(size_t kk=0;kk<32;kk++)for(size_t j=0;j<16;j++)std::memcpy(t.data()+((n*32+g)*512+kk*16+j)*2,b+((n*16+j)*1024+g*32+kk)*2,2);std::memcpy(b,t.data(),t.size());}}
   packed_weights=buffer(d,packed.size()*4,&packed);auto*local=NativeResidentTable(d,packed_weights);packed_weights->Release();packed_weights=local;
  }
  const char*entries[]={tiled?"project_tiled":"project","normalize"};for(UINT i=0;i<2;i++){blob=nullptr;error=nullptr;auto hr=(i==0&&wave)?D3DReadFileToBlob((dir+L"\\native_wave_vit_qkv.cso").c_str(),&blob):CompileNativeShader(dir+L"\\native_vit_qkv.hlsl",nullptr,entries[i],&blob,&error);if(FAILED(hr)){std::string message=error?std::string((const char*)error->GetBufferPointer(),error->GetBufferSize()):"QKV compilation";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(&pso[i])));blob->Release();}
  PackedInput(d,dir);
 }
 void PackedInput(ID3D12Device*d,const std::wstring&dir){
  // FAST PATH: f16 operand copy of the input for the wave QKV kernel (A tiles from memory, no per-K-step LDS restaging).
  const wchar_t*pi=_wgetenv(L"DLSS5_VIT_PACKED_INPUT");if(pi&&wcscmp(pi,L"0")&&wcscmp(pi,L"1"))throw std::runtime_error("invalid ViT packed input flag");
  if(!(pi&&!wcscmp(pi,L"1")&&wave))return;
  packed=true;packed_input=buffer(d,UINT64(count)*1024*2);
  if(const wchar_t*bm=_wgetenv(L"DLSS5_VIT_BLOCK_M")){if(wcscmp(bm,L"1")&&wcscmp(bm,L"4"))throw std::runtime_error("invalid ViT block-M flag");if(!wcscmp(bm,L"4")&&count%64==0){const wchar_t*m=_wgetenv(L"DLSS5_VIT_BLOCK_M_MASK");if(!m||(wcstoul(m,nullptr,10)&4))block_m=4;}}
  const wchar_t*names[]={L"\\native_vit_pack16.cso",block_m==4?L"\\native_wave_vit_qkv_packed_m4.cso":L"\\native_wave_vit_qkv_packed.cso"};ID3D12PipelineState**targets[]={&pack_pso,&pso[0]};
  for(UINT k=0;k<2;k++){ID3DBlob*blob=nullptr;if(FAILED(D3DReadFileToBlob((dir+names[k]).c_str(),&blob)))throw std::runtime_error("ViT QKV packed shader missing");if(k)pso[0]->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};auto hr=NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(targets[k]));blob->Release();if(FAILED(hr))throw std::runtime_error("ViT QKV packed pipeline");}
  // FAST PATH (DLSS5_VIT_QKV_FUSED): projection + normalize + E4M3 store in one kernel (native_wave_vit_qkv_fused_m4.cso); the
  // attention reads Packed8() directly. Needs block_m 4 and the FP8 attention (checked by NativeVitBlock).
  const wchar_t*fu=_wgetenv(L"DLSS5_VIT_QKV_FUSED");if(fu&&wcscmp(fu,L"0")&&wcscmp(fu,L"1"))throw std::runtime_error("invalid ViT QKV fused flag");fused=fu&&!wcscmp(fu,L"1")&&block_m==4;if(vit_tiled&&!fused)throw std::runtime_error("DLSS5_VIT_TILED needs the fused block-4 ViT QKV kernel");
  if(fused){ID3DBlob*blob=nullptr;if(FAILED(D3DReadFileToBlob((dir+L"\\native_wave_vit_qkv_fused_m4.cso").c_str(),&blob)))throw std::runtime_error("ViT QKV fused shader missing");pso[0]->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};auto hr=NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(&pso[0]));blob->Release();if(FAILED(hr))throw std::runtime_error("ViT QKV fused pipeline");packed8=buffer(d,UINT64(count)*3072);weights->Release();weights=buffer(d,16);raw->Release();raw=buffer(d,16);output->Release();output=buffer(d,16);} // fused path never touches the f32 weights, raw or output
 }
 void Record(ID3D12GraphicsCommandList*c){if(recorded){barrier(c,raw,true);barrier(c,output,true);}
  if(packed){D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;t.Transition={packed_input,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};if(recorded)c->ResourceBarrier(1,&t);
   c->SetComputeRootSignature(root);c->SetPipelineState(pack_pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,packed_input->GetGPUVirtualAddress());UINT values=count*1024;c->SetComputeRoot32BitConstants(3,1,&values,0);c->Dispatch((values/2+63)/64,1,1);std::swap(t.Transition.StateBefore,t.Transition.StateAfter);c->ResourceBarrier(1,&t);}
  if(fused){if(recorded)barrier(c,packed8,true);c->SetComputeRootSignature(root);c->SetPipelineState(pso[0]);c->SetComputeRootShaderResourceView(0,packed_input->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,packed_weights->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,packed8->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(3,1,&count,0);c->Dispatch(count/(16*block_m),48,1);barrier(c,packed8,false);recorded=true;return;}
  for(UINT i=0;i<2;i++){c->SetComputeRootSignature(root);c->SetPipelineState(pso[i]);c->SetComputeRootShaderResourceView(0,(i?raw:(packed?packed_input:input))->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,((i==0&&wave)?packed_weights:weights)->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,(i?output:raw)->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(3,1,&count,0);if(i==0&&wave)c->Dispatch(count/(16*block_m),48,1);else if(i==0&&tiled)c->Dispatch(32,count/8,3);else c->Dispatch((count*(i?96:3072)+63)/64,1,1);barrier(c,i?output:raw,false);}recorded=true;}
 ID3D12Resource* Output()const{return output;}
 bool Fused()const{return fused;}
 ID3D12Resource* Packed8()const{return packed8;}
};
