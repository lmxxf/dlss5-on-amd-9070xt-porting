#pragma once
#include "native_pinned_resource.h"
#include "native_game_submission.h"
// Initialization-only copy of immutable CPU upload data to GPU-local storage.
// On a failed/timeout submission retain resources: failure is not cancellation.
inline ID3D12Resource* NativeResidentTable(ID3D12Device*d,ID3D12Resource*upload){
 auto desc=upload->GetDesc();if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER)throw std::runtime_error("resident table requires buffer");
 desc.Flags=D3D12_RESOURCE_FLAG_NONE;
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
 ID3D12Resource*resident=nullptr;ID3D12CommandQueue*q=nullptr;
 auto check=[](HRESULT h){if(FAILED(h))throw std::runtime_error("resident table HRESULT="+std::to_string(unsigned(h)));};
 check(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resident)));
 D3D12_COMMAND_QUEUE_DESC qd{};check(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
 auto*submission=new NativeGameSubmission;upload->AddRef();
 submission->Create(q);
 submission->Submit([&](ID3D12GraphicsCommandList*c){
  c->CopyBufferRegion(resident,0,upload,0,desc.Width);
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition={resident,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);
 });
 submission->Flush();delete submission;q->Release();upload->Release();return resident;
}
// DLSS5_TEST_RESIDENT_WEIGHTS=1: replace an initialization upload buffer with a
// GPU-local copy. Off by default so ordinary/game callers keep legacy placement.
inline ID3D12Resource* NativeMaybeResident(ID3D12Device*d,ID3D12Resource*upload){
 const wchar_t*flag=_wgetenv(L"DLSS5_TEST_RESIDENT_WEIGHTS");
 if(!flag||!wcscmp(flag,L"0"))return upload;
 if(wcscmp(flag,L"1"))throw std::runtime_error("invalid resident weights flag");
 auto*local=NativeResidentTable(d,upload);upload->Release();return local;
}
