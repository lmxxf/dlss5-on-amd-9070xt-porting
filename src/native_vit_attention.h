#pragma once
#include "native_pinned_resource.h"
#include "native_split.h"
class NativeVitAttention {
 ID3D12Resource *input{},*output{},*packed{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{},*pack_pso{};UINT count{};bool recorded{},wave{},half_input{},fp8_attention{},prepacked{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("ViT attention HRESULT="+std::to_string(unsigned(h)));}
 void barrier(ID3D12GraphicsCommandList*c,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeVitAttention()=default;NativeVitAttention(const NativeVitAttention&)=delete;
 ~NativeVitAttention(){if(input)input->Release();if(output)output->Release();if(packed)packed->Release();if(root)root->Release();if(pso)pso->Release();if(pack_pso)pack_pso->Release();}
 // prepacked: src is already the E4M3 Q/K/V buffer (fused ViT QKV); the pack pass is skipped.
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT tokens,const std::wstring&dir,bool prepacked_src=false){
  prepacked=prepacked_src;if(input||!d||!src||(tokens!=64&&tokens!=128&&tokens!=256&&tokens!=640))throw std::runtime_error("ViT attention supports 64/128/256/640 tokens");input=src;input->AddRef();count=tokens;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=UINT64(tokens)*1024*4;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ck(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
  D3D12_ROOT_PARAMETER params[3]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,1};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;ck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();blob=nullptr;error=nullptr;
  const wchar_t*flag=_wgetenv(L"DLSS5_TEST_WAVE_VIT_ATTENTION");if(flag&&wcscmp(flag,L"0")&&wcscmp(flag,L"1"))throw std::runtime_error("invalid wave ViT attention flag");wave=flag&&!wcscmp(flag,L"1");
  // Packed f16 Q/K/V read directly by wave matrix loads (no per-step LDS staging).
  const wchar_t*half_flag=_wgetenv(L"DLSS5_TEST_WAVE_VIT_ATTENTION_HALF");if(half_flag&&wcscmp(half_flag,L"0")&&wcscmp(half_flag,L"1"))throw std::runtime_error("invalid half ViT attention flag");half_input=wave&&half_flag&&!wcscmp(half_flag,L"1");
  if(prepacked&&!half_input)throw std::runtime_error("prepacked ViT Q/K/V needs the half wave attention");
  if(half_input){
   {const wchar_t*f8=_wgetenv(L"DLSS5_VIT_ATTN_FP8");if(f8&&wcscmp(f8,L"0")&&wcscmp(f8,L"1"))throw std::runtime_error("invalid ViT FP8 attention flag");fp8_attention=f8&&!wcscmp(f8,L"1");}
   if(prepacked){if(!fp8_attention)throw std::runtime_error("prepacked ViT Q/K/V needs the FP8 attention");packed=input;packed->AddRef();}
   else{rd.Width=UINT64(tokens)*3072*2;ck(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&packed)));}
   // FAST PATH (DLSS5_VIT_ATTN_FP8): E4M3 Q/K/V copy + FP8 attention kernel (native_wave_vit_attention_fp8.cso / pack8).
   if(!prepacked){ck(D3DReadFileToBlob((dir+(fp8_attention?L"\\native_wave_vit_attention_pack8.cso":L"\\native_wave_vit_attention_pack.cso")).c_str(),&blob));D3D12_COMPUTE_PIPELINE_STATE_DESC pk{};pk.pRootSignature=root;pk.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(d->CreateComputePipelineState(&pk,IID_PPV_ARGS(&pack_pso)));blob->Release();blob=nullptr;}
  }
  auto hr=half_input?D3DReadFileToBlob((dir+(fp8_attention?L"\\native_wave_vit_attention_fp8.cso":L"\\native_wave_vit_attention_half.cso")).c_str(),&blob):wave?D3DReadFileToBlob((dir+L"\\native_wave_vit_attention.cso").c_str(),&blob):CompileNativeShader(dir+L"\\native_vit_attention.hlsl",nullptr,"main",&blob,&error);if(FAILED(hr)){std::string message=error?std::string((const char*)error->GetBufferPointer(),error->GetBufferSize()):"attention compilation";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));blob->Release();
 }
 void Record(ID3D12GraphicsCommandList*c){if(recorded)barrier(c,true);
  if(half_input&&!prepacked){
   D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={packed,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
   if(recorded)c->ResourceBarrier(1,&b);
   c->SetComputeRootSignature(root);c->SetPipelineState(pack_pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(1,packed->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(2,1,&count,0);c->Dispatch(fp8_attention?(count*3072/4+63)/64:(count*3072/2+63)/64,1,1);
   std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);
  }
  c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,(half_input?packed:input)->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(2,1,&count,0);if(wave)c->Dispatch(count/16,32,1);else c->Dispatch((count*32+63)/64,1,1);barrier(c,false);recorded=true;}
 ID3D12Resource* Output()const{return output;}
};
