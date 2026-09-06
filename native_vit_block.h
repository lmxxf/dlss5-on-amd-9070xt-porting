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
 ID3D12Resource* Output()const{return projection.Output();}
};
