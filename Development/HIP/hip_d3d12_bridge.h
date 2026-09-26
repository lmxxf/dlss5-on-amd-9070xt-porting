#pragma once
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <filesystem>
#include "hip_reference_network.h"
#include "../../src/native_device_identity.h"
#include <d3d12.h>
#include <dxgi1_4.h>
namespace hip_reference {
// Experimental single-GPU bridge. Callers serialize frames and preserve SRV states.
// No Agility or experimental DirectX feature enabling is used here.
class D3D12Bridge {
 struct Shared {ID3D12Resource*resource{};HANDLE handle{};Handle imported{};void*mapped{};};
 Network*network{};ID3D12Device*device{};ID3D12CommandQueue*queue{};ID3D12Fence*fence{};
 HANDLE fence_handle{},event{};Handle semaphore{};Shared input,history,output;UINT64 value{};size_t pixels{};bool readable{},pending{},failed{};
public:
 enum class Phase { Ready, InputRecorded, OutputRecordedPendingHip, HipQueued, OutputRecorded };
 Phase CurrentPhase()const{return phase;}
private:
 Phase phase=Phase::Ready;bool recorded_temporal{};
 /* DLSS5_HIP_SPAN_PROBE=1 (diagnostic): hipEvents recorded after the input wait and before the output signal give the
    GPU span of one network enqueue; the previous frame's span and its CPU enqueue time are printed at the next Run. */
 Handle span_begin{},span_end{};bool span_probe{},span_pending{};double span_cpu{};
 static void Check(HRESULT h,const char*what){if(FAILED(h))throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(unsigned(h)));}
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){if(before==after)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};c->ResourceBarrier(1,&b);}
 /* 2026-09-26: the AMD HIP driver never returns a D3D12 buffer that was imported (hipImportExternalMemory) and mapped, even after
    hipFree + hipDestroyExternalMemory + CloseHandle + Release (results/vram-leak-20260926: 40 cycles leak the whole set, ~3 GB of
    VRAM and as much private memory). Every session/geometry change builds a new bridge, so the shared buffers are pooled per process,
    keyed by device + size + UAV, and handed back instead of destroyed. Sizes follow the network tier (3 buffers x at most 3 tiers).
    Buffers decay to COMMON after execution, matching what a fresh bridge assumes. DLSS5_HIP_SHARED_POOL=0 restores create/destroy. */
 struct PoolEntry{ID3D12Device*device{};size_t bytes{};bool uav{},busy{};Shared shared;};
 static std::mutex&PoolMutex(){static std::mutex m;return m;}
 static std::vector<PoolEntry>&Pool(){static std::vector<PoolEntry> v;return v;}
 static bool PoolEnabled(){const char*v=std::getenv("DLSS5_HIP_SHARED_POOL");return !(v&&!strcmp(v,"0"));}
 void Share(Shared&s,size_t bytes,bool uav=false){
  if(PoolEnabled()){std::lock_guard<std::mutex> lock(PoolMutex());for(auto&e:Pool())if(!e.busy&&e.device==device&&e.bytes==bytes&&e.uav==uav){e.busy=true;s=e.shared;return;}}
  ShareNew(s,bytes,uav);
  if(PoolEnabled()){std::lock_guard<std::mutex> lock(PoolMutex());Pool().push_back({device,bytes,uav,true,s});}
 }
 void ShareNew(Shared&s,size_t bytes,bool uav){
  auto&api=network->Runtime();D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
  Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_SHARED,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&s.resource)),"shared buffer");Check(device->CreateSharedHandle(s.resource,nullptr,GENERIC_ALL,nullptr,&s.handle),"buffer handle");
  hip_probe::MemoryDesc md{};md.type=5;md.handle.win32.handle=s.handle;md.size=device->GetResourceAllocationInfo(0,1,&rd).SizeInBytes;md.flags=1;api.Check(api.hipImportExternalMemory(&s.imported,&md),"import D3D12 resource");hip_probe::BufferDesc bd{};bd.size=bytes;api.Check(api.hipExternalMemoryGetMappedBuffer(&s.mapped,s.imported,&bd),"map shared resource");
 }
 void Release(Shared&s){
  if(s.resource&&PoolEnabled()){std::lock_guard<std::mutex> lock(PoolMutex());for(auto&e:Pool())if(e.shared.resource==s.resource){e.busy=false;s={};return;}}
  auto&api=network->Runtime();if(s.mapped)api.hipFree(s.mapped);if(s.imported)api.hipDestroyExternalMemory(s.imported);if(s.handle)CloseHandle(s.handle);if(s.resource)s.resource->Release();s={};}
 void InputContract(ID3D12Resource*r){if(!r||r->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||r->GetDesc().Width<pixels*16)throw std::runtime_error("bridge input capacity");ID3D12Device*owner{};Check(r->GetDevice(IID_PPV_ARGS(&owner)),"input device");bool same=NativeSameDevice(owner,device);owner->Release();if(!same)throw std::runtime_error("bridge input device mismatch");}
public:
 std::string architecture,adapter_name,module_directory,device_match;int runtime_version{};
 D3D12Bridge()=default;D3D12Bridge(const D3D12Bridge&)=delete;D3D12Bridge&operator=(const D3D12Bridge&)=delete;
 // False means resources may still be referenced by unsubmitted/failed work.
 // This also lets wrappers retain their input references until consumers retire.
 bool WaitForSubmittedWork()noexcept{
  if(phase!=Phase::Ready)return false;
  if(network&&network->Runtime().hipStreamSynchronize(network->Stream()))return false;
  if(pending&&queue&&fence){auto target=++value;if(FAILED(queue->Signal(fence,target))||FAILED(fence->SetEventOnCompletion(target,event))||WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)return false;}
  if(device&&FAILED(device->GetDeviceRemovedReason()))return false;
  pending=false;return true;
 }
 ~D3D12Bridge(){
  if(!WaitForSubmittedWork())return;
  if(network){Release(input);Release(history);Release(output);if(semaphore)network->Runtime().hipDestroyExternalSemaphore(semaphore);delete network;}
  if(fence_handle)CloseHandle(fence_handle);if(event)CloseHandle(event);if(fence)fence->Release();if(queue)queue->Release();if(device)device->Release();
 }
 void Create(ID3D12CommandQueue*q,Options options,const std::vector<float>&noise){
  if(network||queue||!q)throw std::runtime_error("bridge already initialized/invalid queue");if(q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT&&q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_COMPUTE)throw std::runtime_error("bridge requires DIRECT or COMPUTE queue");queue=q;queue->AddRef();Check(q->GetDevice(IID_PPV_ARGS(&device)),"queue device");pixels=size_t(options.width)*options.height;
  options.pooled=true;options.profile=false;options.dump_dir.clear();
  // Pick the HIP device that is the game's D3D12 adapter. Hosts with an iGPU or a second card expose several HIP devices
  // LUID is authoritative even when a host spoofs DXGI VendorId/Description.
  // Name fallback requires AMD DXGI identity and exactly one HIP device without a LUID.
  IDXGIFactory4*factory{};IDXGIAdapter1*adapter{};Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");auto hr=factory->EnumAdapterByLuid(device->GetAdapterLuid(),IID_PPV_ARGS(&adapter));factory->Release();Check(hr,"D3D adapter");DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);adapter->Release();char dname[256]{};WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,dname,256,nullptr,nullptr);
  {Api probe(options.runtime);probe.Check(probe.hipInit(0),"hipInit");probe.Check(probe.hipRuntimeGetVersion(&runtime_version),"runtime version");int count{};probe.Check(probe.hipGetDeviceCount(&count),"device count");int chosen=-1,name_match=-1,name_matches=0;std::string seen;const LUID wanted=device->GetAdapterLuid();for(int i=0;i<count;i++){char hname[256]{};if(probe.hipDeviceGetName(hname,256,i))continue;auto prop=probe.Properties(i);bool has_luid=false;for(char c:prop.luid)has_luid|=c!=0;if(has_luid&&!memcmp(prop.luid,&wanted,sizeof wanted)){chosen=i;device_match="luid";}if(!has_luid&&desc.VendorId==0x1002&&!strcmp(dname,hname)){name_match=i;name_matches++;}if(!seen.empty())seen+=" | ";seen+=std::to_string(i)+":"+hname+":"+prop.gcnArchName;}
   if(chosen<0&&name_matches==1){chosen=name_match;device_match="name";}
   if(chosen<0)throw std::runtime_error(std::string("no HIP device matches D3D12 adapter '")+dname+"' (HIP devices: "+(seen.empty()?"none":seen)+")");options.device=unsigned(chosen);hip_device=chosen;auto props=probe.Properties(chosen);architecture=std::string(props.gcnArchName,strnlen(props.gcnArchName,sizeof props.gcnArchName));architecture=architecture.substr(0,architecture.find(':'));adapter_name=dname;
probe.Check(probe.hipSetDevice(chosen),"select device");size_t total=0;if(probe.hipMemGetInfo(&free_at_create,&total))free_at_create=0;}
  if(architecture!="gfx1200"&&architecture!="gfx1201")throw std::runtime_error("unsupported HIP architecture: "+architecture);
  auto root=std::filesystem::u8path(options.modules);
  if(std::filesystem::is_directory(root/"gfx1200")||std::filesystem::is_directory(root/"gfx1201")){
   root/=architecture;if(!std::filesystem::is_directory(root))throw std::runtime_error("missing module architecture directory: "+architecture);
   options.modules=root.u8string();
  }
  module_directory=options.modules;
  network=new Network(std::move(options));auto&api=network->Runtime();
  Share(input,pixels*16);Share(history,pixels*16);Share(output,pixels*12,true);Check(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence)),"shared fence");Check(device->CreateSharedHandle(fence,nullptr,GENERIC_ALL,nullptr,&fence_handle),"fence handle");hip_probe::SemaphoreDesc sd{};sd.type=4;sd.handle.win32.handle=fence_handle;api.Check(api.hipImportExternalSemaphore(&semaphore,&sd),"import fence");event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("bridge completion event");if(const char*v=std::getenv("DLSS5_HIP_SPAN_PROBE"))span_probe=!strcmp(v,"1");if(span_probe){api.Check(api.hipEventCreate(&span_begin),"span begin event");api.Check(api.hipEventCreate(&span_end),"span end event");fprintf(stderr,"hip_span probe enabled\n");}network->SetNoise(noise);
 }
 ID3D12Resource*Output()const{return output.resource;}
 size_t free_at_create{};int hip_device=-1;/* HIP device index chosen for the D3D12 adapter */
 void MemoryReport(FILE*f){if(!network)return;network->Runtime().hipStreamSynchronize(network->Stream());std::fprintf(f,"hip_memory device_free_before_network_MiB=%.1f shared input_MiB=%.1f history_MiB=%.1f output_MiB=%.1f\n",free_at_create/1048576.,pixels*16/1048576.,pixels*16/1048576.,pixels*12/1048576.);network->MemoryReport(f);}
#ifdef DLSS5_BENCH_BRIDGE_ISOLATE
 Network& DiagnosticNetwork(){return *network;}
#endif
private:
 void Require(Phase wanted)const{if(!network||failed)throw std::runtime_error("bridge unavailable");if(phase!=wanted)throw std::runtime_error("bridge stage order");}
 void QueueContract(ID3D12CommandQueue*q)const{if(!q||!NativeSameDevice(q,queue))throw std::runtime_error("bridge submission queue mismatch");}
 void ListContract(ID3D12GraphicsCommandList*c)const{
  if(!c||c->GetType()!=queue->GetDesc().Type)throw std::runtime_error("bridge command list type mismatch");
  ID3D12Device*owner{};Check(c->GetDevice(IID_PPV_ARGS(&owner)),"command list device");bool same=NativeSameDevice(owner,device);owner->Release();if(!same)throw std::runtime_error("bridge command list device mismatch");
 }
 void RecordInput(ID3D12GraphicsCommandList*c,ID3D12Resource*rgba,ID3D12Resource*temporal,bool external){
  Require(Phase::Ready);if(external&&network->GraphEnabled())throw std::runtime_error("staged bridge requires HIP graph off");ListContract(c);InputContract(rgba);if(temporal)InputContract(temporal);
  phase=Phase::InputRecorded;recorded_temporal=temporal!=nullptr;
  try{
   auto copy=[&](ID3D12Resource*src,Shared&dst){Barrier(c,src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(c,dst.resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);c->CopyBufferRegion(dst.resource,0,src,0,pixels*16);Barrier(c,dst.resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);Barrier(c,src,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
   copy(rgba,input);if(temporal)copy(temporal,history);if(readable)Barrier(c,output.resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);readable=false;
  }catch(...){failed=true;throw;}
 }
 void Enqueue(ID3D12CommandQueue*producer,U seed,bool temporal,bool external){
  if(!network||failed)throw std::runtime_error("bridge unavailable");
  const bool output_recorded=phase==Phase::OutputRecordedPendingHip;
  if(phase!=Phase::InputRecorded&&!output_recorded)throw std::runtime_error("bridge stage order");
  QueueContract(producer);if(temporal!=recorded_temporal)throw std::runtime_error("bridge temporal input mismatch");if(external&&network->GraphEnabled())throw std::runtime_error("staged bridge requires HIP graph off");
  auto&api=network->Runtime();
  try{
   pending=true;Check(queue->Signal(fence,++value),"D3D input signal");hip_probe::WaitParams wait{};wait.params.fence.value=value;api.Check(api.hipWaitExternalSemaphoresAsync(&semaphore,&wait,1,network->Stream()),"HIP input wait");
   if(span_probe){if(span_pending){float ms=-1;int sync=api.hipEventSynchronize(span_end),status=api.hipEventElapsedTime(&ms,span_begin,span_end);fprintf(stderr,"hip_span gpu_ms=%.3f cpu_enqueue_ms=%.3f sync=%d status=%d\n",ms,span_cpu,sync,status);span_pending=false;}api.Check(api.hipEventRecord(span_begin,network->Stream()),"span begin");}
   auto start=std::chrono::steady_clock::now();network->Enqueue(input.mapped,temporal?history.mapped:nullptr,output.mapped,seed);
   if(span_probe){span_cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();api.Check(api.hipEventRecord(span_end,network->Stream()),"span end");span_pending=true;}
   hip_probe::SignalParams signal{};signal.params.fence.value=++value;api.Check(api.hipSignalExternalSemaphoresAsync(&semaphore,&signal,1,network->Stream()),"HIP output signal");Check(queue->Wait(fence,value),"D3D output wait");phase=output_recorded?Phase::OutputRecorded:Phase::HipQueued;
  }catch(...){failed=true;throw;}
 }
public:
 // Single host thread, one staged frame at a time; all lists use the queue passed to Create.
 // RecordInputCopy -> host submits producer -> EnqueueAfterProducer -> RecordOutputReadable
 // -> host records/submits consumers -> NotifyOutputSubmitted. Record* never closes/submits a list.
 // The consumer may also be recorded/closed before EnqueueAfterProducer, but must
 // only be submitted AFTER it. Recording order does not replace queue dependencies.
 // Resources must be SRV-readable before input recording. Do not replay recorded lists.
 // Optional host preparation before recording the first staged frame. Lazy
 // weight uploads synchronize the HIP stream; perform them before inserting an
 // external producer wait, rather than inside a game's Execute callback.
 void PrepareStagedKernels(){
  Require(Phase::Ready);
  if(pending||value||readable||network->GraphEnabled())throw std::runtime_error("bridge preparation requires fresh graph-off session");
  auto&api=network->Runtime();
  try{api.Check(api.hipMemsetAsync(input.mapped,0,pixels*16,network->Stream()),"prepare input");network->Enqueue(input.mapped,nullptr,output.mapped,1);network->Synchronize();}
  catch(...){failed=true;throw;}
 }
 void RecordInputCopy(ID3D12GraphicsCommandList*c,ID3D12Resource*rgba,ID3D12Resource*temporal=nullptr){RecordInput(c,rgba,temporal,true);}
 void EnqueueAfterProducer(ID3D12CommandQueue*producer,U seed,bool temporal=false){Enqueue(producer,seed,temporal,true);}
 void RecordOutputReadable(ID3D12GraphicsCommandList*c){
  if(!network||failed)throw std::runtime_error("bridge unavailable");
  const bool before_enqueue=phase==Phase::InputRecorded;
  if(phase!=Phase::HipQueued&&!before_enqueue)throw std::runtime_error("bridge stage order");
  ListContract(c);
  try{Barrier(c,output.resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);readable=true;phase=before_enqueue?Phase::OutputRecordedPendingHip:Phase::OutputRecorded;}catch(...){failed=true;throw;}
 }
 // Acknowledges submission, not GPU completion. Queue order protects the next frame;
 // the destructor fences submitted work. Omitting this acknowledgement prevents reuse/free.
 void NotifyOutputSubmitted(ID3D12CommandQueue*consumer){Require(Phase::OutputRecorded);QueueContract(consumer);phase=Phase::Ready;}
 void NotifyOutputSubmittedIfRecorded(ID3D12CommandQueue*consumer){if(phase==Phase::OutputRecorded&&consumer)NotifyOutputSubmitted(consumer);}
 void CancelUnsubmitted(){if(phase==Phase::InputRecorded||phase==Phase::OutputRecordedPendingHip){phase=Phase::Ready;readable=false;}}
 template<class Submission>void Run(Submission&submit,ID3D12Resource*rgba,ID3D12Resource*temporal,U seed){
  Require(Phase::Ready);QueueContract(submit.Queue());
  try{
   submit.Submit([&](ID3D12GraphicsCommandList*c){RecordInput(c,rgba,temporal,false);});
   Enqueue(submit.Queue(),seed,temporal!=nullptr,false);
   submit.Submit([&](ID3D12GraphicsCommandList*c){RecordOutputReadable(c);});
   NotifyOutputSubmitted(submit.Queue());
  }catch(...){failed=true;throw;}
 }
};
}
