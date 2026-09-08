#pragma once
#include <d3d12.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdio>
class NativeNetworkTimestamps {
 ID3D12QueryHeap*heap{};ID3D12Resource*readback{};std::vector<std::string>labels;
 static void check(HRESULT h){if(FAILED(h))throw std::runtime_error("network timestamp HRESULT="+std::to_string(unsigned(h)));}
public:
 ~NativeNetworkTimestamps(){if(heap)heap->Release();if(readback)readback->Release();}
 void Create(ID3D12Device*d){
  D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=128;check(d->CreateQueryHeap(&q,IID_PPV_ARGS(&heap)));
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=128*8;r.Height=1;r.DepthOrArraySize=r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
 }
 void Reset(){labels.clear();}
 void Mark(ID3D12GraphicsCommandList*c,const std::string&label){if(!heap)return;if(labels.size()>=128)throw std::runtime_error("timestamp capacity");c->EndQuery(heap,D3D12_QUERY_TYPE_TIMESTAMP,UINT(labels.size()));labels.push_back(label);}
 void Resolve(ID3D12GraphicsCommandList*c){if(heap)c->ResolveQueryData(heap,D3D12_QUERY_TYPE_TIMESTAMP,0,UINT(labels.size()),readback,0);}
 // Raw intervals (ms) between consecutive marks, no printing (game-side probe).
 bool Intervals(UINT64 frequency,std::vector<double>&out){
  out.clear();if(!heap||!frequency||labels.size()<2)return false;
  UINT64*p=nullptr;D3D12_RANGE range{0,labels.size()*8},none{};check(readback->Map(0,&range,reinterpret_cast<void**>(&p)));
  bool ok=true;for(size_t i=1;i<labels.size();i++){if(p[i]<p[i-1])ok=false;out.push_back(1000.0*double(p[i]-p[i-1])/frequency);}
  readback->Unmap(0,&none);return ok;
 }
 void Report(UINT64 frequency){
  if(!heap)return;if(!frequency||labels.size()<2)throw std::runtime_error("timestamp frequency/count");
  UINT64*p=nullptr;D3D12_RANGE range{0,labels.size()*8},none{};check(readback->Map(0,&range,reinterpret_cast<void**>(&p)));
  for(size_t i=1;i<labels.size();i++){if(p[i]<p[i-1]){readback->Unmap(0,&none);throw std::runtime_error("nonmonotonic timestamps");}printf("network_gpu_interval stage=%s ms=%.6f\n",labels[i].c_str(),1000.0*double(p[i]-p[i-1])/frequency);}
  printf("network_gpu_interval total_ms=%.6f includes_inter_submission_gaps=1\n",1000.0*double(p[labels.size()-1]-p[0])/frequency);fflush(stdout);readback->Unmap(0,&none);
 }
};
