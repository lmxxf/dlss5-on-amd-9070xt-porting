#pragma once
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
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("game submission HRESULT="+std::to_string(unsigned(h)));}
public:
 NativeGameSubmission()=default;NativeGameSubmission(const NativeGameSubmission&)=delete;
 ~NativeGameSubmission(){
  // A timeout is not GPU cancellation. Keep referenced command storage alive
  // rather than freeing resources that the GPU may still be executing.
  if(pending&&(!fence||fence->GetCompletedValue()<value))return;
  if(commands)commands->Release();if(allocator)allocator->Release();if(fence)fence->Release();
  if(timing_heap)timing_heap->Release();if(timing_readback)timing_readback->Release();
  if(event)CloseHandle(event);if(queue)queue->Release();if(device)device->Release();
 }
 void Create(ID3D12CommandQueue*q){
  if(queue||!q||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)throw std::runtime_error("DIRECT queue required");
  queue=q;queue->AddRef();ck(q->GetDevice(IID_PPV_ARGS(&device)));
  ck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
  ck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&commands)));
  ck(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
  event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("submission event failed");
  const wchar_t*flag=_wgetenv(L"DLSS5_TEST_SUBMISSION_TIMING");if(flag&&wcscmp(flag,L"0")&&wcscmp(flag,L"1"))throw std::runtime_error("invalid submission timing flag");
  if(flag&&!wcscmp(flag,L"1")){
   D3D12_QUERY_HEAP_DESC hd{};hd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;hd.Count=2;ck(device->CreateQueryHeap(&hd,IID_PPV_ARGS(&timing_heap)));
   D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=16;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
   ck(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timing_readback)));ck(queue->GetTimestampFrequency(&timing_frequency));if(!timing_frequency)throw std::runtime_error("zero GPU clock");
  }
 }
 template<class Record>void Submit(Record record,DWORD timeout_ms=30000){
  std::lock_guard<std::mutex>guard(mutex);
  if(!event||poisoned||!timeout_ms)throw std::runtime_error("submission unavailable");
  try{
   const auto started=std::chrono::steady_clock::now();
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
 ID3D12Device*Device()const{return device;}
 UINT64 TimestampFrequency()const{UINT64 f=0;ck(queue->GetTimestampFrequency(&f));return f;}
};
