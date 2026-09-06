#pragma once
#include <windows.h>
#include <d3d12.h>
#include <mutex>
#include <stdexcept>
#include <string>

// Own lists/allocator on a caller-provided DIRECT queue. This must be invoked
// AFTER the game has submitted the input producer. Never closes a game list.
class NativeGameSubmission {
 ID3D12Device*device{};ID3D12CommandQueue*queue{};
 ID3D12CommandAllocator*allocator{};ID3D12GraphicsCommandList*commands{};
 ID3D12Fence*fence{};HANDLE event{};UINT64 value{};
 bool poisoned{},submitted{},pending{};std::mutex mutex;
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("game submission HRESULT="+std::to_string(unsigned(h)));}
public:
 NativeGameSubmission()=default;NativeGameSubmission(const NativeGameSubmission&)=delete;
 ~NativeGameSubmission(){
  // A timeout is not GPU cancellation. Keep referenced command storage alive
  // rather than freeing resources that the GPU may still be executing.
  if(pending&&(!fence||fence->GetCompletedValue()<value))return;
  if(commands)commands->Release();if(allocator)allocator->Release();if(fence)fence->Release();
  if(event)CloseHandle(event);if(queue)queue->Release();if(device)device->Release();
 }
 void Create(ID3D12CommandQueue*q){
  if(queue||!q||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)throw std::runtime_error("DIRECT queue required");
  queue=q;queue->AddRef();ck(q->GetDevice(IID_PPV_ARGS(&device)));
  ck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
  ck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&commands)));
  ck(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
  event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("submission event failed");
 }
 template<class Record>void Submit(Record record,DWORD timeout_ms=30000){
  std::lock_guard<std::mutex>guard(mutex);
  if(!event||poisoned||!timeout_ms)throw std::runtime_error("submission unavailable");
  try{
   if(submitted){ck(allocator->Reset());ck(commands->Reset(allocator,nullptr));}
   record(commands);ck(commands->Close());ID3D12CommandList*lists[]={commands};
   ++value;pending=true;queue->ExecuteCommandLists(1,lists);ck(queue->Signal(fence,value));
   ck(fence->SetEventOnCompletion(value,event));
   if(WaitForSingleObject(event,timeout_ms)!=WAIT_OBJECT_0)throw std::runtime_error("game GPU submission timeout");
   ck(device->GetDeviceRemovedReason());auto done=fence->GetCompletedValue();
   if(done==UINT64_MAX||done<value)throw std::runtime_error("game GPU fence invalid");
   pending=false;submitted=true;
  }catch(...){poisoned=true;throw;}
 }
 ID3D12Device*Device()const{return device;}
};
