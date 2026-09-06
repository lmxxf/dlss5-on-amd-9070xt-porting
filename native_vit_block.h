#pragma once
#include "native_vit_linear.h"
#include "native_vit_qkv.h"
#include "native_vit_attention.h"
class NativeVitBlock {
 NativeVitLinear expand,contract,projection;NativeVitQkv qkv;NativeVitAttention attention;
public:
 void Create(ID3D12Device*d,ID3D12Resource*input,UINT tokens,const std::vector<float>&ew,const std::vector<float>&cw,const std::vector<float>&qw,const std::vector<float>&pw,const std::wstring&dir){
  expand.Create(d,input,nullptr,tokens,1024,4096,true,ew,dir);
  contract.Create(d,expand.Output(),input,tokens,4096,1024,false,cw,dir);
  qkv.Create(d,contract.Output(),tokens,qw,dir);
  attention.Create(d,qkv.Output(),tokens,dir);
  projection.Create(d,attention.Output(),contract.Output(),tokens,1024,1024,false,pw,dir);
 }
 void Record(ID3D12GraphicsCommandList*c){expand.Record(c);contract.Record(c);qkv.Record(c);attention.Record(c);projection.Record(c);}
 void RecordStage(ID3D12GraphicsCommandList*c,UINT stage){switch(stage){case 0:expand.Record(c);break;case 1:contract.Record(c);break;case 2:qkv.Record(c);break;case 3:attention.Record(c);break;case 4:projection.Record(c);break;default:throw std::runtime_error("invalid ViT stage");}}
 ID3D12Resource* Output()const{return projection.Output();}
 UINT StageChunks(UINT stage)const{return stage==0?expand.ChunkCount():stage==1?contract.ChunkCount():stage==4?projection.ChunkCount():1;}
 void RecordStageChunk(ID3D12GraphicsCommandList*c,UINT stage,UINT chunk){if(stage==0)expand.RecordChunk(c,chunk);else if(stage==1)contract.RecordChunk(c,chunk);else if(stage==4)projection.RecordChunk(c,chunk);else{if(chunk)throw std::runtime_error("stage chunk range");RecordStage(c,stage);}}
};
