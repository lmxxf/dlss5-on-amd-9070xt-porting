#pragma once
#include "native_pinned_resource.h"
#include <windows.h>
#include <d3d12.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <chrono>
#include <cstdio>

// Own lists/allocator on a caller-provided DIRECT queue. This must be invoked
// AFTER the game has submitted the input producer. Never closes a game list.
class NativeGameSubmission {
 ID3D12Device*device{};ID3D12CommandQueue*queue{};
 ID3D12CommandAllocator*allocator{};ID3D12GraphicsCommandList*commands{};
 ID3D12Fence*fence{};HANDLE event{};UINT64 value{};
 bool poisoned{},submitted{},pending{};std::mutex mutex;
 ID3D12QueryHeap*timing_heap{};ID3D12Resource*timing_readback{};UINT64 timing_frequency{};
 // Deferred mode (DLSS5_TEST_ASYNC_SUBMIT=1): a ring of allocators/lists is
 // executed back to back and only the ring slot being reused is waited on.
 // Queue order is unchanged, so every list still observes the previous one.
 // 64 slots: a game frame issues ~100 lists; with 8 slots the CPU waited on slot reuse a dozen times per frame, and each
 // wake-up costs ~0.5ms once the game has background threads (the post-cutscene 21->8 fps collapse).
 static constexpr UINT ring_slots=64;
 bool deferred{};ID3D12CommandAllocator*ring_allocators[ring_slots]{};ID3D12GraphicsCommandList*ring_lists[ring_slots]{};UINT64 ring_values[ring_slots]{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("game submission HRESULT="+std::to_string(unsigned(h)));}
 void WaitValue(UINT64 target,DWORD timeout_ms){
  if(fence->GetCompletedValue()>=target)return;
  ck(fence->SetEventOnCompletion(target,event));
  if(WaitForSingleObject(event,timeout_ms)!=WAIT_OBJECT_0)throw std::runtime_error("game GPU submission timeout");
  ck(device->GetDeviceRemovedReason());auto done=fence->GetCompletedValue();
  if(done==UINT64_MAX||done<target)throw std::runtime_error("game GPU fence invalid");
 }
public:
 NativeGameSubmission()=default;NativeGameSubmission(const NativeGameSubmission&)=delete;
 ~NativeGameSubmission(){
  // A timeout is not GPU cancellation. Keep referenced command storage alive
  // rather than freeing resources that the GPU may still be executing.
  if(pending&&(!fence||fence->GetCompletedValue()<value))return;
  if(commands)commands->Release();if(allocator)allocator->Release();
  for(UINT i=0;i<ring_slots;i++){if(ring_lists[i])ring_lists[i]->Release();if(ring_allocators[i])ring_allocators[i]->Release();}
  if(fence)fence->Release();
  if(timing_heap)timing_heap->Release();if(timing_readback)timing_readback->Release();
  if(event)CloseHandle(event);if(queue)queue->Release();if(device)device->Release();
 }
 // allow_deferred=false: initialization-only users (resident weight copies) stay synchronous and skip the 64-slot ring.
 void Create(ID3D12CommandQueue*q,bool allow_deferred=true){
  if(queue||!q||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)throw std::runtime_error("DIRECT queue required");
  queue=q;queue->AddRef();ck(q->GetDevice(IID_PPV_ARGS(&device)));
  ck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
  ck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&commands)));
  ck(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
  event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("submission event failed");
  const wchar_t*flag=_wgetenv(L"DLSS5_TEST_SUBMISSION_TIMING");if(flag&&wcscmp(flag,L"0")&&wcscmp(flag,L"1"))throw std::runtime_error("invalid submission timing flag");
  const wchar_t*async_flag=_wgetenv(L"DLSS5_TEST_ASYNC_SUBMIT");if(async_flag&&wcscmp(async_flag,L"0")&&wcscmp(async_flag,L"1"))throw std::runtime_error("invalid async submit flag");
  deferred=allow_deferred&&async_flag&&!wcscmp(async_flag,L"1");
  if(deferred&&flag&&!wcscmp(flag,L"1"))throw std::runtime_error("per-submission timing requires synchronous submission");
  if(deferred)for(UINT i=0;i<ring_slots;i++){
   ck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&ring_allocators[i])));
   ck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,ring_allocators[i],nullptr,IID_PPV_ARGS(&ring_lists[i])));
   ck(ring_lists[i]->Close());
  }
  if(flag&&!wcscmp(flag,L"1")){
   D3D12_QUERY_HEAP_DESC hd{};hd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;hd.Count=2;ck(device->CreateQueryHeap(&hd,IID_PPV_ARGS(&timing_heap)));
   D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=16;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
   ck(NativeCreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timing_readback)));ck(queue->GetTimestampFrequency(&timing_frequency));if(!timing_frequency)throw std::runtime_error("zero GPU clock");
  }
 }
 /* fence bookkeeping for asynchronous readbacks (black probe): the value signalled by the latest Submit and the GPU's completed value */
 UINT64 LastValue()const{return value;}UINT64 Completed()const{return fence?fence->GetCompletedValue():0;}
 template<class Record>void Submit(Record record,DWORD timeout_ms=30000){
  std::lock_guard<std::mutex>guard(mutex);
  if(!event||poisoned||!timeout_ms)throw std::runtime_error("submission unavailable");
  try{
   const auto started=std::chrono::steady_clock::now();
   if(deferred){
    const UINT slot=UINT(value%ring_slots);
    if(ring_values[slot])WaitValue(ring_values[slot],timeout_ms);
    auto*list=ring_lists[slot];ck(ring_allocators[slot]->Reset());ck(list->Reset(ring_allocators[slot],nullptr));
    record(list);ck(list->Close());ID3D12CommandList*lists[]={list};
    ++value;pending=true;queue->ExecuteCommandLists(1,lists);ck(queue->Signal(fence,value));ring_values[slot]=value;
    ck(device->GetDeviceRemovedReason());submitted=true;return;
   }
   if(submitted){ck(allocator->Reset());ck(commands->Reset(allocator,nullptr));}
   if(timing_heap)commands->EndQuery(timing_heap,D3D12_QUERY_TYPE_TIMESTAMP,0);
   record(commands);
   if(timing_heap){commands->EndQuery(timing_heap,D3D12_QUERY_TYPE_TIMESTAMP,1);commands->ResolveQueryData(timing_heap,D3D12_QUERY_TYPE_TIMESTAMP,0,2,timing_readback,0);}
   ck(commands->Close());ID3D12CommandList*lists[]={commands};
   ++value;pending=true;queue->ExecuteCommandLists(1,lists);ck(queue->Signal(fence,value));
   ck(fence->SetEventOnCompletion(value,event));
   if(WaitForSingleObject(event,timeout_ms)!=WAIT_OBJECT_0)throw std::runtime_error("game GPU submission timeout");
   ck(device->GetDeviceRemovedReason());auto done=fence->GetCompletedValue();
   if(done==UINT64_MAX||done<value)throw std::runtime_error("game GPU fence invalid");
   pending=false;submitted=true;
   if(timing_heap){
    double wall=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    UINT64*t=nullptr;D3D12_RANGE range{0,16},none{};ck(timing_readback->Map(0,&range,reinterpret_cast<void**>(&t)));UINT64 a=t[0],b=t[1];timing_readback->Unmap(0,&none);if(b<a)throw std::runtime_error("nonmonotonic submission clock");
    printf("submission_timing queue=%p id=%llu gpu_ms=%.6f wall_ms=%.6f\n",queue,value,1000.0*double(b-a)/timing_frequency,wall);fflush(stdout);
   }
  }catch(...){poisoned=true;throw;}
 }
 // Wait for every executed list. Required before any CPU readback in deferred
 // mode; a no-op in synchronous mode where Submit already waited.
 void Flush(DWORD timeout_ms=30000){
  std::lock_guard<std::mutex>guard(mutex);
  if(!event||poisoned)throw std::runtime_error("submission unavailable");
  if(!deferred||!pending)return;
  try{WaitValue(value,timeout_ms);pending=false;}catch(...){poisoned=true;throw;}
 }
 bool Deferred()const{return deferred;}
 ID3D12Device*Device()const{return device;}
 UINT64 TimestampFrequency()const{UINT64 f=0;ck(queue->GetTimestampFrequency(&f));return f;}
};
