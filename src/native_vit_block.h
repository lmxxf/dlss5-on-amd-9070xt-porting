#pragma once
#include "native_pso.h"
#include "native_vit_linear.h"
#include "native_vit_qkv.h"
#include "native_vit_attention.h"
class NativeVitBlock {
 NativeVitLinear expand,contract,projection;ID3D12Resource*block_input{};NativeVitQkv qkv;NativeVitAttention attention;
 /* FAST PATH (DLSS5_VIT_FUSED_FFN): expand+contract in one dispatch (native_wave_vit_ffn_fused.hlsl); the hidden layer stays in LDS. Bit-exact. */
 ID3D12RootSignature*fused_root{};ID3D12PipelineState*fused_pso{};bool fused{},fused_recorded{};UINT token_count{};
 static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("ViT fused FFN HRESULT="+std::to_string(unsigned(hr)));}
 void CreateFused(ID3D12Device*d,const std::wstring&dir){
  const wchar_t*f=_wgetenv(L"DLSS5_VIT_FUSED_FFN");if(f&&wcscmp(f,L"0")&&wcscmp(f,L"1"))throw std::runtime_error("invalid ViT fused FFN flag");fused=f&&!wcscmp(f,L"1");if(!fused)return;
  if(!expand.HasPackedInput()||!expand.WaveTiled()||!contract.WaveTiled()||!contract.SplitK()||token_count%32)throw std::runtime_error("ViT fused FFN needs the packed tiled E4M3 chain (DLSS5_VIT_PACKED_INPUT, DLSS5_VIT_TILED, DLSS5_VIT_SPLIT_K)");
  D3D12_ROOT_PARAMETER params[6]{};for(UINT i=0;i<4;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[5].Constants={0,0,1};
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=6;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&fused_root)));blob->Release();if(error)error->Release();
  blob=nullptr;Check(D3DReadFileToBlob((dir+L"\\native_wave_vit_ffn_fused.cso").c_str(),&blob));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=fused_root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};Check(NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(&fused_pso)));blob->Release();
 }
 void RecordFused(ID3D12GraphicsCommandList*c){
  if(fused_recorded)contract.OutputBarrier(c,true);
  expand.RecordPack(c);expand.MarkRecorded();
  c->SetComputeRootSignature(fused_root);c->SetPipelineState(fused_pso);c->SetComputeRootShaderResourceView(0,expand.PackedInputBuffer()->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,expand.Weights()->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,block_input->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(3,contract.Weights()->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(4,contract.Output()->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(5,1,&token_count,0);
  /* DLSS5_VIT_FUSED_TOK32: 32-token groups (kernel built with NATIVE_VIT_FUSED_TOK32=1), else 16 */
  static const bool tok32=_wgetenv(L"DLSS5_VIT_FUSED_TOK32")&&!wcscmp(_wgetenv(L"DLSS5_VIT_FUSED_TOK32"),L"1");
  c->Dispatch(token_count/(tok32?32:16),1,1);contract.OutputBarrier(c,false);fused_recorded=true;
 }
public:
 ~NativeVitBlock(){if(fused_pso)fused_pso->Release();if(fused_root)fused_root->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*input,UINT tokens,const std::vector<float>&ew,const std::vector<float>&cw,const std::vector<float>&qw,const std::vector<float>&pw,const std::wstring&dir){
  NativeInitTick("vit: enter");block_input=input;expand.Create(d,input,nullptr,tokens,1024,4096,true,ew,dir);NativeInitTick("vit: expand");
  contract.Create(d,expand.Output(),input,tokens,4096,1024,false,cw,dir);NativeInitTick("vit: contract");
  qkv.Create(d,contract.Output(),tokens,qw,dir);NativeInitTick("vit: qkv");
  if(qkv.Fused())attention.Create(d,qkv.Packed8(),tokens,dir,true);else attention.Create(d,qkv.Output(),tokens,dir);
  NativeInitTick("vit: attention");projection.Create(d,attention.Output(),contract.Output(),tokens,1024,1024,false,pw,dir);NativeInitTick("vit: projection");
  token_count=tokens;CreateFused(d,dir);
 }
 void Record(ID3D12GraphicsCommandList*c){if(fused)RecordFused(c);else{expand.Record(c);contract.Record(c);}qkv.Record(c);attention.Record(c);projection.Record(c);}
 void RecordStage(ID3D12GraphicsCommandList*c,UINT stage){switch(stage){case 0:if(fused)RecordFused(c);else expand.Record(c);break;case 1:if(!fused)contract.Record(c);break;case 2:qkv.Record(c);break;case 3:attention.Record(c);break;case 4:projection.Record(c);break;default:throw std::runtime_error("invalid ViT stage");}}
 ID3D12Resource* Output()const{return projection.Output();}ID3D12Resource* Input()const{return block_input;}
 UINT StageChunks(UINT stage)const{return stage==0?(fused?1u:expand.ChunkCount()):stage==1?(fused?1u:contract.ChunkCount()):stage==4?projection.ChunkCount():1;}
 void RecordStageChunk(ID3D12GraphicsCommandList*c,UINT stage,UINT chunk){if(fused&&stage<2){if(chunk)throw std::runtime_error("stage chunk range");RecordStage(c,stage);}else if(stage==0)expand.RecordChunk(c,chunk);else if(stage==1)contract.RecordChunk(c,chunk);else if(stage==4)projection.RecordChunk(c,chunk);else{if(chunk)throw std::runtime_error("stage chunk range");RecordStage(c,stage);}}
};
