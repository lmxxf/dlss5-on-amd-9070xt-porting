#pragma once
// Every default-heap resource of the network goes through here so it can be given a residency priority
// (DLSS5_RESIDENCY_PRIORITY=high|maximum). When the game oversubscribes VRAM, Windows demotes low-priority resources to
// system memory; a demoted network buffer is read over PCIe every frame and halves the frame rate. High priority makes the
// OS evict the game's streaming textures first.
#include <d3d12.h>
#include <cstdlib>
#include <cwchar>
#include <stdexcept>
#include <cstdio>
#include <windows.h>
inline HRESULT NativeCreateCommittedResource(ID3D12Device*d,const D3D12_HEAP_PROPERTIES*hp,D3D12_HEAP_FLAGS flags,const D3D12_RESOURCE_DESC*rd,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE*clear,REFIID iid,void**out){
 HRESULT hr=d->CreateCommittedResource(hp,flags,rd,state,clear,iid,out);
 static int priority=[]{const wchar_t*v=_wgetenv(L"DLSS5_RESIDENCY_PRIORITY");if(!v||!*v||!wcscmp(v,L"0")||!wcscmp(v,L"off"))return 0;if(!wcscmp(v,L"high"))return 1;if(!wcscmp(v,L"maximum"))return 2;return -1;}();
 if(priority<0)throw std::runtime_error("invalid residency priority flag (off|high|maximum)");
 if(SUCCEEDED(hr)&&priority&&hp&&hp->Type==D3D12_HEAP_TYPE_DEFAULT&&out&&*out){
  ID3D12Device1*d1=nullptr;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&d1)))){ID3D12Pageable*p=static_cast<ID3D12Resource*>(*out);D3D12_RESIDENCY_PRIORITY pr=priority==2?D3D12_RESIDENCY_PRIORITY_MAXIMUM:D3D12_RESIDENCY_PRIORITY_HIGH;HRESULT phr=d1->SetResidencyPriority(1,&p,&pr);d1->Release();
   static unsigned ok=0,bad=0;if(SUCCEEDED(phr))ok++;else bad++;if((ok+bad)%16==1||FAILED(phr))if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){fprintf(f,"pid=%lu residency_priority ok=%u bad=%u last_hr=%08x bytes=%llu\n",GetCurrentProcessId(),ok,bad,unsigned(phr),(unsigned long long)rd->Width);fclose(f);}}
  else if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){fprintf(f,"pid=%lu residency_priority no_device1\n",GetCurrentProcessId());fclose(f);}
 }
 return hr;
}
