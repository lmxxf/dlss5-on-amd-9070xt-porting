#pragma once
#include "native_pinned_resource.h"
#include "native_game_submission.h"
#include <vector>
// Initialization-only copy of immutable CPU upload data to GPU-local storage.
// On a failed/timeout submission retain resources: failure is not cancellation.
/* Batched initialization copies. The first version created a queue + submission per table and waited for each copy; with the game
   and Magpie keeping the GPU busy every wait costs a frame, and ~1000 tables made that most of the 20-30 s start-up (DevHistory 09-11).
   Now every copy is recorded on one open list of a per-device copy batch and the wait happens once: NativeResidentFlush() at the end
   of the network's Create and again before the first Run (nothing at initialization reads a resident table on the GPU). */
struct NativeResidentBatch{
 ID3D12Device*device{};ID3D12CommandQueue*queue{};ID3D12CommandAllocator*allocator{};ID3D12GraphicsCommandList*list{};ID3D12Fence*fence{};HANDLE event{};UINT64 value{};bool open{};UINT64 bytes{};
 std::vector<ID3D12Resource*>uploads,residents; /* both ends of every pending copy stay alive until the batch executed */
 static void check(HRESULT h){if(FAILED(h))throw std::runtime_error("resident batch HRESULT="+std::to_string(unsigned(h)));}
 void Bind(ID3D12Device*d){
  if(device==d)return;
  Flush();if(list)list->Release();if(allocator)allocator->Release();if(fence)fence->Release();if(event)CloseHandle(event);if(queue)queue->Release();list=nullptr;allocator=nullptr;fence=nullptr;event=nullptr;queue=nullptr;
  D3D12_COMMAND_QUEUE_DESC qd{};check(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
  check(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
  check(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&list)));check(list->Close());
  check(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("resident batch event");
  device=d;
 }
 ID3D12GraphicsCommandList*Open(){if(!open){check(allocator->Reset());check(list->Reset(allocator,nullptr));open=true;}return list;}
 void Flush(){
  if(!open)return;check(list->Close());ID3D12CommandList*lists[]={list};queue->ExecuteCommandLists(1,lists);check(queue->Signal(fence,++value));
  if(fence->GetCompletedValue()<value){check(fence->SetEventOnCompletion(value,event));if(WaitForSingleObject(event,60000)!=WAIT_OBJECT_0)throw std::runtime_error("resident batch timeout");}
  for(auto*u:uploads)u->Release();uploads.clear();for(auto*r:residents)r->Release();residents.clear();bytes=0;open=false;
 }
};
inline NativeResidentBatch&NativeResidentBatchState(){static NativeResidentBatch batch;return batch;}
inline void NativeResidentFlush(){NativeResidentBatchState().Flush();}
inline ID3D12Resource* NativeResidentTable(ID3D12Device*d,ID3D12Resource*upload){
 auto desc=upload->GetDesc();if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER)throw std::runtime_error("resident table requires buffer");
 desc.Flags=D3D12_RESOURCE_FLAG_NONE;
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
 ID3D12Resource*resident=nullptr;
 auto check=[](HRESULT h){if(FAILED(h))throw std::runtime_error("resident table HRESULT="+std::to_string(unsigned(h)));};
 check(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resident)));
 auto&batch=NativeResidentBatchState();batch.Bind(d);
 auto*c=batch.Open();
 c->CopyBufferRegion(resident,0,upload,0,desc.Width);
 D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
 b.Transition={resident,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);
 upload->AddRef();batch.uploads.push_back(upload);resident->AddRef();batch.residents.push_back(resident);batch.bytes+=desc.Width; /* a caller may release the resident before the flush (NativeVitLinear re-packs its weights) */
 if(batch.uploads.size()>=256||batch.bytes>=768ull*1024*1024)batch.Flush(); /* bound the upload heaps kept alive */
 return resident;
}
// DLSS5_TEST_RESIDENT_WEIGHTS=1: replace an initialization upload buffer with a
// GPU-local copy. Off by default so ordinary/game callers keep legacy placement.
inline ID3D12Resource* NativeMaybeResident(ID3D12Device*d,ID3D12Resource*upload){
 const wchar_t*flag=_wgetenv(L"DLSS5_TEST_RESIDENT_WEIGHTS");
 if(!flag||!wcscmp(flag,L"0"))return upload;
 if(wcscmp(flag,L"1"))throw std::runtime_error("invalid resident weights flag");
 auto*local=NativeResidentTable(d,upload);upload->Release();return local;
}
