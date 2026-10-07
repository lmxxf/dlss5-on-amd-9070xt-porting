#pragma once
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <atomic>
#include <thread>
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
 ID3D12Resource* zero_upload{};ID3D12CommandAllocator* clear_alloc{};ID3D12GraphicsCommandList* clear_cmd{};
 size_t zero_upload_bytes{};bool clear_submission_unconfirmed{};
 /* Direct input (2026-09-28): the host producer writes the network input straight into the shared buffer (UAV) and leaves it in
    COMMON; RecordInput then skips the 35 MB D3D12 copy. Requested before Create; the bytes the network reads are unchanged. */
 bool direct_input{},direct_history{};std::vector<float> post_auxiliary_row;
 using PassthroughCopyFn=int(*)(void*,size_t,const void*,size_t,size_t,size_t,int,Handle);
 PassthroughCopyFn passthrough_copy{};void*passthrough_rgb{};unsigned long long passthrough_queued{};
 Handle release_mark{};bool release_marker_requested{};unsigned long long release_marks{},release_mark_failures{};
 bool recording_leases{},recording_active{},recording_submission_unconfirmed{};UINT64 recording_completion{};
public:
 enum class Phase { Ready, InputRecorded, OutputRecordedPendingHip, HipQueued, OutputRecorded };
 Phase CurrentPhase()const{return phase;}
 bool PdlActive()const{return network&&network->PdlCalls()!=0;}
 /* DLSS5_MULTI_PASS at run time (add-on hot reload / hotkey): 0 = query only. Returns the pass count in effect (0 = no network). */
 unsigned MultiPass(unsigned set=0){if(!network)return 0;if(set)network->SetMultiPass(set);return network->MultiPass();}
 bool MultiPassSkinProtect()const{return network&&network->MultiPassSkinProtect();}
 void MultiPassSkinProtect(bool set){if(network)network->SetMultiPassSkinProtect(set);}
 bool MultiPassPredict()const{return network&&network->MultiPassPredict();}
 void MultiPassPredict(bool set){if(network)network->SetMultiPassPredict(set);}
 bool ExperimentalTemporalActive()const{return network&&network->ExperimentalTemporalActive();}
 bool SwinRunActive()const{return network&&network->SwinRunActive();}
private:
 Phase phase=Phase::Ready;bool recorded_temporal{};
 /* DLSS5_HIP_SPAN_PROBE=1 (diagnostic): hipEvents recorded after the input wait and before the output signal give the
    GPU span of one network enqueue; the previous frame's span and its CPU enqueue time are printed at the next Run. */
 Handle span_begin{},span_end{};bool span_probe{},span_pending{};double span_cpu{};
 /* Network GPU timing (2026-10-02, results/net-timing-20261002; read by LmxxfNrApi GetTimings/GetStatus): kTimingSlots
    begin/end hipEvent pairs recorded on the network stream at the SPAN_PROBE points, so a span is the HIP network only
    (no D3D12 input copy/codec pass, no handoff wait). Harvested at the next Enqueue / PollNetworkTiming with hipEventQuery
    only, never a synchronize; if the next slot's end has not completed yet that frame is simply not timed. With DLSS5_MULTI_PASS=N the
    span covers all N passes (the whole per-frame network cost), not one pass. Off until
    EnableNetworkTiming() or DLSS5_NET_TIMING=1. Any HIP error here turns timing off and never fails a frame. Bytes unchanged. */
 static constexpr unsigned kTimingSlots=4;
 Handle timing_begin[kTimingSlots]{},timing_end[kTimingSlots]{};unsigned long long timing_slot_tag[kTimingSlots]{};bool timing_busy[kTimingSlots]{};
 bool timing_on{},timing_requested{},timing_faulted{};unsigned long long timing_epoch{},timing_slot_epoch[kTimingSlots]{};unsigned timing_next{},timing_oldest{};unsigned long long timing_tag{},timing_last_tag{};float timing_last_ms{};bool timing_valid{};
 using EventQueryFn=int(*)(Handle);EventQueryFn timing_query{},post_query{};
 void TimingOff(){timing_on=false;timing_valid=false;timing_faulted=true;}
 void HarvestTiming(){
  if(!timing_on)return;auto&api=network->Runtime();
  for(unsigned n=0;n<kTimingSlots;n++){const unsigned k=timing_oldest;if(!timing_busy[k])return;
   const int q=timing_query(timing_end[k]);if(q==600/*hipErrorNotReady*/)return;if(q!=0){TimingOff();return;}
   float ms=-1;if(api.hipEventElapsedTime(&ms,timing_begin[k],timing_end[k])==0&&ms>=0&&ms<1e6f&&timing_slot_epoch[k]==timing_epoch){timing_last_ms=ms;timing_last_tag=timing_slot_tag[k];timing_valid=true;}
   timing_busy[k]=false;timing_oldest=(k+1)%kTimingSlots;}
 }
 bool TimingBegin(){ // after the input wait; returns true when this frame is being timed
  if(!timing_on||!timing_requested)return false;HarvestTiming();if(!timing_on||timing_busy[timing_next])return false;
  if(network->Runtime().hipEventRecord(timing_begin[timing_next],network->Stream())){TimingOff();return false;}return true;
 }
 void TimingEnd(){ // before the output signal
  if(!timing_on)return;const unsigned k=timing_next;
  if(network->Runtime().hipEventRecord(timing_end[k],network->Stream())){TimingOff();return;}
  // Submit the timestamp batch before the external signal/completion marker. On
  // Windows HIP, deferring this query until a later frame can collapse the span.
  // Query is non-blocking: success and not-ready both leave harvesting to the ring.
  const int q=timing_query(timing_end[k]);if(q!=0&&q!=600/*hipErrorNotReady*/){TimingOff();return;}
  timing_slot_epoch[k]=timing_epoch;timing_slot_tag[k]=timing_tag;timing_busy[k]=true;timing_next=(k+1)%kTimingSlots;
 }
 void DestroyTiming(){if(!network)return;auto&api=network->Runtime();for(unsigned k=0;k<kTimingSlots;k++){if(timing_begin[k])api.hipEventDestroy(timing_begin[k]);if(timing_end[k])api.hipEventDestroy(timing_end[k]);timing_begin[k]=timing_end[k]=nullptr;}TimingOff();}
public:
 /* Starts network timing (idempotent). false = unavailable (no hipEventQuery export or event creation failed); frames are unaffected. */
 bool EnableNetworkTiming(){
  timing_requested=true;if(timing_faulted)return false;if(timing_on)return true;if(!network||failed)return false;auto&api=network->Runtime();
  if(!timing_query)timing_query=reinterpret_cast<EventQueryFn>(GetProcAddress(api.dll,"hipEventQuery"));if(!timing_query)return false;
  for(unsigned k=0;k<kTimingSlots;k++){if(!timing_begin[k]&&api.hipEventCreate(&timing_begin[k])){DestroyTiming();return false;}if(!timing_end[k]&&api.hipEventCreate(&timing_end[k])){DestroyTiming();return false;}}
  timing_next=timing_oldest=0;timing_on=true;return true;
 }
 bool NetworkTimingEnabled()const{return timing_on&&timing_requested;}
 void PauseNetworkTiming(){timing_requested=false;timing_valid=false;}
 void SetTimingEpoch(unsigned long long epoch){if(epoch!=timing_epoch){timing_epoch=epoch;timing_valid=false;}}
 /* Tag stored with the next timed enqueue (the RE9 runtime passes LmxxfNrFrameInfo::frame_id). */
 void SetTimingTag(unsigned long long tag){timing_tag=tag;}
 /* Non-blocking: collects every completed span, then returns the most recent one. valid=false until one has completed. */
 struct NetworkTiming{bool valid;float ms;unsigned long long tag;};
 NetworkTiming PollNetworkTiming(){if(network&&!failed&&timing_requested)HarvestTiming();return {timing_requested&&timing_valid&&!failed,timing_last_ms,timing_last_tag};}
private:
 /* DLSS5_HIP_INPUT_POLL=1 (2026-09-30, results/handoff-gpu-20260930): the D3D->HIP half of the handoff waits on the GPU instead of
    through the shared fence. The queue runs a pre-recorded list whose only command is WriteBufferImmediate(MARKER_OUT) of a slot
    value into a small shared buffer, and the HIP stream waits with hipStreamWaitValue32(EQ) in its command processor. Slot values
    cycle 1..8; frame n+1's marker is behind the queue's wait on frame n's HIP output, so the EQ wait cannot miss. The HIP->D3D half
    keeps the fence (a D3D-side spin was measured slower). Safety: a start-up self-check (marker -> wait must pass within 200 ms)
    and a watchdog thread: if a marker has completed on the D3D side but the HIP wait behind it has not passed within 200 ms, the
    watchdog writes the value from a separate HIP stream, and every later frame goes back to the fence path. Bytes unchanged. */
 static constexpr unsigned kPollSlots=8;
 bool poll{},poll_inline{},poll_recorded{};unsigned poll_recorded_target{};std::atomic<bool> poll_off{false},poll_stop{false};Shared flag;ID3D12Fence*poll_fence{};UINT64 poll_value{};unsigned poll_frame{};
 ID3D12CommandAllocator*poll_alloc[kPollSlots]{};ID3D12GraphicsCommandList*poll_list[kPollSlots]{};UINT64 poll_done[kPollSlots]{};Handle poll_evt[kPollSlots]{};
 using WaitValueFn=int(*)(Handle,void*,unsigned,unsigned,unsigned);using MemsetD32Fn=int(*)(void*,int,size_t,Handle);using QueryFn=int(*)(Handle);
 WaitValueFn wait_value{};MemsetD32Fn memset_d32{};QueryFn event_query{},stream_query{};Handle poll_rescue{};std::thread poll_watch;
 std::mutex poll_mutex;struct PollArmed{bool armed{};UINT64 d3d{};Handle evt{};unsigned target{};};PollArmed poll_armed;
 void PollRescue(unsigned target,const char*why){auto&api=network->Runtime();memset_d32(flag.mapped,int(target),1,poll_rescue);api.hipStreamSynchronize(poll_rescue);poll_off=true;fprintf(stderr,"hip_input_poll: %s; released slot %u from a side stream, fence path from now on\n",why,target);}
 void PollWatch(){
  auto seen=std::chrono::steady_clock::time_point{};UINT64 seen_d3d=0;
  while(!poll_stop&&!poll_off){
   std::this_thread::sleep_for(std::chrono::milliseconds(20));PollArmed a;{std::lock_guard<std::mutex> l(poll_mutex);a=poll_armed;}
   if(!a.armed||poll_fence->GetCompletedValue()<a.d3d||event_query(a.evt)==0){seen_d3d=0;continue;}
   if(seen_d3d!=a.d3d){seen_d3d=a.d3d;seen=std::chrono::steady_clock::now();continue;}
   if(std::chrono::steady_clock::now()-seen>std::chrono::milliseconds(200)){PollRescue(a.target,"GPU wait did not see the D3D marker within 200 ms");break;}
  }
 }
 void SetupPoll(){
  auto&api=network->Runtime();auto load=[&](auto&f,const char*n){f=reinterpret_cast<std::remove_reference_t<decltype(f)>>(GetProcAddress(api.dll,n));if(!f)throw std::runtime_error(std::string("missing HIP export ")+n);};
  load(wait_value,"hipStreamWaitValue32");load(memset_d32,"hipMemsetD32Async");load(event_query,"hipEventQuery");load(stream_query,"hipStreamQuery");
  Share(flag,65536);api.Check(api.hipStreamCreate(&poll_rescue),"poll rescue stream");api.Check(api.hipMemsetAsync(flag.mapped,0,65536,network->Stream()),"poll flag clear");api.Check(api.hipStreamSynchronize(network->Stream()),"poll flag clear sync");
  Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&poll_fence)),"poll fence");const auto type=queue->GetDesc().Type;
  for(unsigned k=0;k<kPollSlots;k++){Check(device->CreateCommandAllocator(type,IID_PPV_ARGS(&poll_alloc[k])),"poll allocator");Check(device->CreateCommandList(0,type,poll_alloc[k],nullptr,IID_PPV_ARGS(&poll_list[k])),"poll list");
   ID3D12GraphicsCommandList2*c2{};Check(poll_list[k]->QueryInterface(IID_PPV_ARGS(&c2)),"poll list2");D3D12_WRITEBUFFERIMMEDIATE_PARAMETER wp{flag.resource->GetGPUVirtualAddress(),k+1};D3D12_WRITEBUFFERIMMEDIATE_MODE wm=D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT;c2->WriteBufferImmediate(1,&wp,&wm);c2->Release();Check(poll_list[k]->Close(),"poll list close");api.Check(api.hipEventCreate(&poll_evt[k]),"poll event");}
  // self-check: slot 1 marker -> HIP wait must pass
  ID3D12CommandList*l[]={poll_list[0]};queue->ExecuteCommandLists(1,l);Check(queue->Signal(poll_fence,++poll_value),"poll self-check signal");api.Check(wait_value(network->Stream(),flag.mapped,1,1,0xffffffffu),"poll self-check wait");
  auto t0=std::chrono::steady_clock::now();bool ok=false;while(std::chrono::steady_clock::now()-t0<std::chrono::milliseconds(200)){if(stream_query(network->Stream())==0){ok=true;break;}Sleep(1);}
  if(!ok)PollRescue(1,"self-check: GPU wait did not see the D3D marker");
  api.Check(api.hipStreamSynchronize(network->Stream()),"poll self-check sync");api.Check(api.hipMemsetAsync(flag.mapped,0,4,network->Stream()),"poll flag reset");api.Check(api.hipStreamSynchronize(network->Stream()),"poll flag reset sync");
  if(poll_fence->GetCompletedValue()<poll_value){Check(poll_fence->SetEventOnCompletion(poll_value,event),"poll self-check event");if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("poll self-check timeout");}
  if(poll_off)return;poll=true;poll_watch=std::thread([this]{PollWatch();});fprintf(stderr,"hip_input_poll enabled\n");
 }
 void TeardownPoll(){
  poll_stop=true;if(poll_watch.joinable())poll_watch.join();auto&api=network->Runtime();
  for(unsigned k=0;k<kPollSlots;k++){if(poll_list[k])poll_list[k]->Release();if(poll_alloc[k])poll_alloc[k]->Release();if(poll_evt[k])api.hipEventDestroy(poll_evt[k]);}
  if(poll_rescue)api.hipStreamDestroy(poll_rescue);if(poll_fence)poll_fence->Release();Release(flag);
 }
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
 bool EnsureZeroClearResources() noexcept {
  if(zero_upload&&clear_alloc&&clear_cmd)return true;
  if(!device||!pixels)return false;
  try{
   zero_upload_bytes=std::min<size_t>(pixels*12,65536);
   D3D12_HEAP_PROPERTIES up{};up.Type=D3D12_HEAP_TYPE_UPLOAD;
   D3D12_RESOURCE_DESC ud{};ud.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;ud.Width=zero_upload_bytes;ud.Height=1;ud.DepthOrArraySize=ud.MipLevels=1;ud.SampleDesc.Count=1;ud.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ud.Flags=D3D12_RESOURCE_FLAG_NONE;
   Check(device->CreateCommittedResource(&up,D3D12_HEAP_FLAG_NONE,&ud,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&zero_upload)),"zero upload buffer");
   void*mappedZero=nullptr;D3D12_RANGE r{0,0};Check(zero_upload->Map(0,&r,&mappedZero),"map zero upload buffer");
   if(!mappedZero){zero_upload->Unmap(0,nullptr);throw std::runtime_error("map zero upload buffer returned null");}
   std::memset(mappedZero,0,zero_upload_bytes);zero_upload->Unmap(0,nullptr);
   Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&clear_alloc)),"clear allocator");
   Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,clear_alloc,nullptr,IID_PPV_ARGS(&clear_cmd)),"clear command list");Check(clear_cmd->Close(),"close clear command list");
   return true;
  }catch(...){
   if(clear_cmd){clear_cmd->Release();clear_cmd=nullptr;}
   if(clear_alloc){clear_alloc->Release();clear_alloc=nullptr;}
   if(zero_upload){zero_upload->Release();zero_upload=nullptr;}
   zero_upload_bytes=0;
   return false;
  }
 }
 void InputContract(ID3D12Resource*r,size_t bytes_per_pixel=16){if(!r||r->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||r->GetDesc().Width<pixels*bytes_per_pixel)throw std::runtime_error("bridge input capacity");ID3D12Device*owner{};Check(r->GetDevice(IID_PPV_ARGS(&owner)),"input device");bool same=NativeSameDevice(owner,device);owner->Release();if(!same)throw std::runtime_error("bridge input device mismatch");}
public:
 std::string architecture,adapter_name,module_directory,device_match;int runtime_version{};
 D3D12Bridge()=default;D3D12Bridge(const D3D12Bridge&)=delete;D3D12Bridge&operator=(const D3D12Bridge&)=delete;
 // False means resources may still be referenced by unsubmitted/failed work.
 // This also lets wrappers retain their input references until consumers retire.
 bool WaitForSubmittedWork()noexcept{
  if(phase!=Phase::Ready||clear_submission_unconfirmed||recording_submission_unconfirmed||recording_active)return false;
  if(network&&(network->Runtime().hipSetDevice(hip_device)||network->Runtime().hipStreamSynchronize(network->Stream())))return false;
  if(pending&&queue&&fence){auto target=++value;if(FAILED(queue->Signal(fence,target))||FAILED(fence->SetEventOnCompletion(target,event))||WaitForSingleObject(event,30000)!=WAIT_OBJECT_0||fence->GetCompletedValue()<target)return false;}
  if(device&&FAILED(device->GetDeviceRemovedReason()))return false;
  pending=false;return true;
 }
 ~D3D12Bridge(){
  if(!WaitForSubmittedWork()||(network&&!network->CloseSubmitPulse()))return;
  if(clear_cmd)clear_cmd->Release();if(clear_alloc)clear_alloc->Release();if(zero_upload)zero_upload->Release();
  if(network){DestroyTiming();TeardownPoll();auto&api=network->Runtime();for(auto h:{release_mark,span_begin,span_end})if(h)api.hipEventDestroy(h);if(passthrough_rgb)api.hipFree(passthrough_rgb);Release(input);Release(history);Release(output);if(semaphore)network->Runtime().hipDestroyExternalSemaphore(semaphore);delete network;}
  if(fence_handle)CloseHandle(fence_handle);if(event)CloseHandle(event);if(fence)fence->Release();if(queue)queue->Release();if(device)device->Release();
 }
 void Create(ID3D12CommandQueue*q,Options options,const std::vector<float>&noise){
  if(network||queue||!q)throw std::runtime_error("bridge already initialized/invalid queue");if(q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT&&q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_COMPUTE)throw std::runtime_error("bridge requires DIRECT or COMPUTE queue");queue=q;queue->AddRef();Check(q->GetDevice(IID_PPV_ARGS(&device)),"queue device");pixels=size_t(options.width)*options.height;
  options.pooled=true;options.profile=false;options.dump_dir.clear();
  // Pick the HIP device that is the game's D3D12 adapter. Hosts with an iGPU or a second card expose several HIP devices
  // LUID is authoritative even when a host spoofs DXGI VendorId/Description.
  // Name fallback requires AMD DXGI identity and exactly one HIP device without a LUID.
  IDXGIFactory4*factory{};IDXGIAdapter1*adapter{};Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");auto hr=factory->EnumAdapterByLuid(device->GetAdapterLuid(),IID_PPV_ARGS(&adapter));factory->Release();Check(hr,"D3D adapter");DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);LARGE_INTEGER pulse_driver{};const HRESULT pulse_driver_status=adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&pulse_driver);adapter->Release();char dname[256]{};WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,dname,256,nullptr,nullptr);
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
  if((direct_history||!post_auxiliary_row.empty())&&network->ExperimentalTemporalConfigured())throw std::runtime_error("direct history/auxiliary output cannot use experimental temporal layout");
  if(!post_auxiliary_row.empty()&&!network->NativeHistorySupported())throw std::runtime_error("auxiliary post unsupported network");
  Share(input,pixels*16,direct_input);Share(history,pixels*16,direct_history);Share(output,pixels*(post_auxiliary_row.empty()?12:20),true);
  if(!post_auxiliary_row.empty())network->EnableNativePostHistory(post_auxiliary_row,{static_cast<char*>(output.mapped)+pixels*12,pixels*8,network->W,network->H,8});
  Check(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence)),"shared fence");Check(device->CreateSharedHandle(fence,nullptr,GENERIC_ALL,nullptr,&fence_handle),"fence handle");hip_probe::SemaphoreDesc sd{};sd.type=4;sd.handle.win32.handle=fence_handle;api.Check(api.hipImportExternalSemaphore(&semaphore,&sd),"import fence");event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("bridge completion event");if(const char*v=std::getenv("DLSS5_HIP_SPAN_PROBE"))span_probe=!strcmp(v,"1");if(span_probe){api.Check(api.hipEventCreate(&span_begin),"span begin event");api.Check(api.hipEventCreate(&span_end),"span end event");fprintf(stderr,"hip_span probe enabled\n");}if(release_marker_requested)api.Check(api.hipEventCreate(&release_mark),"release marker event");network->SetNoise(noise);
  {const char*v=std::getenv("DLSS5_HIP_POST_SIGNAL_QUERY");if(!(v&&!strcmp(v,"0")))post_query=reinterpret_cast<EventQueryFn>(GetProcAddress(api.dll,"hipStreamQuery"));}
  if(const char*v=std::getenv("DLSS5_NET_TIMING");v&&!strcmp(v,"1"))EnableNetworkTiming();
  if(const char*v=std::getenv("DLSS5_HIP_INPUT_POLL");options.integration.allow_input_poll&&v){if(strcmp(v,"0")&&strcmp(v,"1")&&strcmp(v,"2"))throw std::runtime_error("DLSS5_HIP_INPUT_POLL must be 0, 1 or 2");poll_inline=!strcmp(v,"2");if(strcmp(v,"0")){try{SetupPoll();}catch(const std::exception&e){poll=false;poll_off=true;fprintf(stderr,"hip_input_poll unavailable (%s); fence path\n",e.what());}}}
  // Performance scope fingerprint from the actual validated RX9070XT driver.
  // A missing/changed fingerprint falls back to old NN in auto mode.
  const bool pulse_arch=architecture=="gfx1201";
  const bool pulse_validated=SUCCEEDED(pulse_driver_status)&&static_cast<unsigned long long>(pulse_driver.QuadPart)==0x00200000791f0800ull;
  std::fprintf(stderr,"submit_pulse device_scope arch=%s runtime=%d driver_query=%08x driver=%016llx validated=%u\n",architecture.c_str(),runtime_version,unsigned(pulse_driver_status),static_cast<unsigned long long>(pulse_driver.QuadPart),unsigned(pulse_validated));
  network->pulse_bridge_diagnostics=timing_on||span_probe||poll||poll_inline;network->ConfigureSubmitPulse(pulse_arch,pulse_validated);
 }
 ID3D12Resource*Output()const{return output.resource;}
 void RequestDirectInput(){if(network)throw std::runtime_error("direct input must be requested before Create");direct_input=true;}
 ID3D12Resource*DirectInput()const{return direct_input?input.resource:nullptr;}
 // Request before Create. The producer must leave shared history in COMMON;
 // all writers/readers use the bridge's producer/consumer queue ordering.
 // Optional stream-retirement event; no allocation or enqueue when not requested.
 void RequestReleaseMarkers(){if(network||queue)throw std::runtime_error("release markers must precede Create");release_marker_requested=true;}
 unsigned long long ReleaseMarks()const{return release_marks;}
 unsigned long long ReleaseMarkFailures()const{return release_mark_failures;}
 unsigned long long HipPassthroughQueued()const{return passthrough_queued;}
 void RequestDirectHistory(){if(network||queue)throw std::runtime_error("direct history must precede Create");direct_history=true;}
 ID3D12Resource*DirectHistory()const{return direct_history?history.resource:nullptr;}
 void RequestPostAuxiliary(const std::vector<float>&row){
  if(network||queue||row.size()!=32)throw std::runtime_error("post auxiliary request contract");
  for(float v:row)if(!std::isfinite(v))throw std::runtime_error("post auxiliary nonfinite weight");
  post_auxiliary_row=row;
 }
 struct AuxiliaryOutput {ID3D12Resource*resource=nullptr;UINT64 offset=0,bytes=0;UINT width=0,height=0,pixel_stride=8;};
 AuxiliaryOutput PostAuxiliary()const{return network&&!post_auxiliary_row.empty()?AuxiliaryOutput{output.resource,pixels*12,pixels*8,network->W,network->H,8}:AuxiliaryOutput{};}
 void SetAdaptiveReuseAllowed(bool allowed){Require(Phase::Ready);network->SetAdaptiveReuseAllowed(allowed);}

 size_t free_at_create{};int hip_device=-1;/* HIP device index chosen for the D3D12 adapter */
 void MemoryReport(FILE*f){if(!network)return;network->Runtime().hipStreamSynchronize(network->Stream());std::fprintf(f,"hip_memory device_free_before_network_MiB=%.1f shared input_MiB=%.1f history_MiB=%.1f output_MiB=%.1f\n",free_at_create/1048576.,pixels*16/1048576.,pixels*16/1048576.,pixels*12/1048576.);network->MemoryReport(f);}
 bool PdlRequested()const{return network?network->PdlRequested():false;}
 bool PdlEffective()const{return network?network->PdlEffective():false;}
 std::string PdlReason()const{return network?network->PdlReason():"bridge uninitialized";}
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
  Require(Phase::Ready);if(external&&network->GraphEnabled())throw std::runtime_error("staged bridge requires HIP graph off");ListContract(c);InputContract(rgba);if(temporal)InputContract(temporal,network->ExperimentalTemporalConfigured()?8:16);
  phase=Phase::InputRecorded;recorded_temporal=temporal!=nullptr;
  try{
   auto copy=[&](ID3D12Resource*src,Shared&dst,size_t bytes_per_pixel=16){Barrier(c,src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(c,dst.resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);c->CopyBufferRegion(dst.resource,0,src,0,pixels*bytes_per_pixel);Barrier(c,dst.resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);Barrier(c,src,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
   if(!(direct_input&&rgba==input.resource))copy(rgba,input);/* direct: the producer already wrote input and left it in COMMON */if(temporal&&!(direct_history&&temporal==history.resource))copy(temporal,history,network->ExperimentalTemporalConfigured()?8:16);if(readable)Barrier(c,output.resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);readable=false;
   poll_recorded=false;if(poll&&poll_inline&&!poll_off){ID3D12GraphicsCommandList2*c2{};if(SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&c2)))){poll_recorded_target=poll_frame%kPollSlots+1;D3D12_WRITEBUFFERIMMEDIATE_PARAMETER wp{flag.resource->GetGPUVirtualAddress(),poll_recorded_target};D3D12_WRITEBUFFERIMMEDIATE_MODE wm=D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT;c2->WriteBufferImmediate(1,&wp,&wm);c2->Release();poll_recorded=true;}}
  }catch(...){failed=true;throw;}
 }
 void Enqueue(ID3D12CommandQueue*producer,U seed,bool temporal,bool external,bool hip_passthrough=false){
  if(!network||failed)throw std::runtime_error("bridge unavailable");
  const bool output_recorded=phase==Phase::OutputRecordedPendingHip;
  if(phase!=Phase::InputRecorded&&!output_recorded)throw std::runtime_error("bridge stage order");
  QueueContract(producer);if(temporal!=recorded_temporal)throw std::runtime_error("bridge temporal input mismatch");if(external&&network->GraphEnabled())throw std::runtime_error("staged bridge requires HIP graph off");
  if(hip_passthrough&&(!passthrough_copy||!passthrough_rgb||temporal))throw std::runtime_error("HIP passthrough not prepared or temporal input supplied");
  auto&api=network->Runtime();
  try{
   api.Check(api.hipSetDevice(hip_device),"select HIP device for enqueue");
   pending=true;network->pulse_bridge_diagnostics=timing_on||span_probe||poll||poll_inline;
   if(poll_inline?(poll_recorded&&!poll_off):false){const unsigned target=poll_recorded_target,slot=target-1;poll_recorded=false;
    Check(queue->Signal(poll_fence,++poll_value),"poll marker signal");
    api.Check(wait_value(network->Stream(),flag.mapped,target,1/*EQ*/,0xffffffffu),"HIP input poll wait");api.Check(api.hipEventRecord(poll_evt[slot],network->Stream()),"poll event");
    {std::lock_guard<std::mutex> lk(poll_mutex);poll_armed={true,poll_value,poll_evt[slot],target};}poll_frame++;}
   else if(poll&&!poll_inline&&!poll_off){const unsigned slot=poll_frame%kPollSlots,target=slot+1;
    if(poll_done[slot]&&poll_fence->GetCompletedValue()<poll_done[slot]){Check(poll_fence->SetEventOnCompletion(poll_done[slot],event),"poll slot event");if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("poll slot reuse timeout");}
    ID3D12CommandList*l[]={poll_list[slot]};queue->ExecuteCommandLists(1,l);Check(queue->Signal(poll_fence,++poll_value),"poll marker signal");poll_done[slot]=poll_value;
    api.Check(wait_value(network->Stream(),flag.mapped,target,1/*EQ*/,0xffffffffu),"HIP input poll wait");api.Check(api.hipEventRecord(poll_evt[slot],network->Stream()),"poll event");
    {std::lock_guard<std::mutex> lk(poll_mutex);poll_armed={true,poll_value,poll_evt[slot],target};}poll_frame++;}
   else{Check(queue->Signal(fence,++value),"D3D input signal");hip_probe::WaitParams wait{};wait.params.fence.value=value;api.Check(api.hipWaitExternalSemaphoresAsync(&semaphore,&wait,1,network->Stream()),"HIP input wait");}
   if(span_probe){if(span_pending){float ms=-1;int sync=api.hipEventSynchronize(span_end),status=api.hipEventElapsedTime(&ms,span_begin,span_end);fprintf(stderr,"hip_span gpu_ms=%.3f cpu_enqueue_ms=%.3f sync=%d status=%d\n",ms,span_cpu,sync,status);span_pending=false;}api.Check(api.hipEventRecord(span_begin,network->Stream()),"span begin");}
   const bool timed=!hip_passthrough&&TimingBegin();
   auto start=std::chrono::steady_clock::now();
   if(hip_passthrough){
    // Raster float4 -> raster float3, not a flat byte copy. Chunk the strided
    // copy below the Windows HIP 2^20-row limit (also used by MultiPassFeed).
    const size_t chunk=size_t(1)<<19;
    for(size_t offset=0;offset<pixels;offset+=chunk)
     api.Check(passthrough_copy(static_cast<char*>(passthrough_rgb)+offset*12,12,
       static_cast<const char*>(input.mapped)+offset*16,16,12,std::min(chunk,pixels-offset),3,network->Stream()),"HIP passthrough RGBA to RGB");
    // Keep the normal final device-to-shared-output copy as well as both fences.
    api.Check(api.hipMemcpyAsync(output.mapped,passthrough_rgb,pixels*12,3,network->Stream()),"HIP passthrough output copy");
   }else network->Enqueue(input.mapped,temporal?history.mapped:nullptr,output.mapped,seed);
   if(timed)TimingEnd();
   if(span_probe){span_cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();api.Check(api.hipEventRecord(span_end,network->Stream()),"span end");span_pending=true;}
   hip_probe::SignalParams signal{};signal.params.fence.value=++value;api.Check(api.hipSignalExternalSemaphoresAsync(&semaphore,&signal,1,network->Stream()),"HIP output signal");
   /* DLSS5_HIP_POST_SIGNAL_QUERY (outside-net 2026-10-02, default 1; 0 = off): one non-blocking hipStreamQuery right after the output
      signal so Windows HIP submits the whole batch (network + signal) now. Bytes unchanged; add-on ABBA -0.01..-0.12 ms. */
   if(post_query)post_query(network->Stream());
   if(release_mark){if(api.hipEventRecord(release_mark,network->Stream())==0)++release_marks;else ++release_mark_failures;}
   if(hip_passthrough)++passthrough_queued;
   Check(queue->Wait(fence,value),"D3D output wait");phase=output_recorded?Phase::OutputRecorded:Phase::HipQueued;
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
 // Optional replayable recording: the caller owns every recorded resource until Reset/Release
 // and completion, and serializes these methods. Legacy staged callers remain unchanged.
 void EnableRecordingLeases(){
  Require(Phase::Ready);
  if(pending||value||readable||network->GraphEnabled())throw std::runtime_error("recording leases require fresh graph-off bridge");
  if(poll||poll_inline)throw std::runtime_error("recording leases require input poll off");
  recording_leases=true;
 }
 // Call after the last output reader was recorded. Both shared-buffer boundaries
 // are COMMON, so discard and replay do not depend on CPU-only readable state.
 void SealRecordedOutput(ID3D12GraphicsCommandList*c){
  if(!recording_leases||recording_active)throw std::runtime_error("not a recording lease");
  Require(Phase::OutputRecordedPendingHip);ListContract(c);
  Barrier(c,output.resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
  readable=false;phase=Phase::Ready;
 }
 // BEFORE submitting the producer, order it after the previous actual consumer,
 // including executions on another same-device queue. Never wait on the CPU here.
 void BeginRecordedExecution(ID3D12CommandQueue*actual,bool temporal=false){
  if(!recording_leases||recording_active||recording_submission_unconfirmed)throw std::runtime_error("recording execution unavailable");
  Require(Phase::Ready);
  if(!actual||actual->GetDesc().Type!=queue->GetDesc().Type)throw std::runtime_error("recording queue type mismatch");
  ID3D12Device*owner{};Check(actual->GetDevice(IID_PPV_ARGS(&owner)),"recording queue device");
  bool same=NativeSameDevice(owner,device);owner->Release();if(!same)throw std::runtime_error("recording queue device mismatch");
  if(actual!=queue){
   if(recording_completion)Check(actual->Wait(fence,recording_completion),"previous recorded consumer wait");
   actual->AddRef();queue->Release();queue=actual;
  }
  recorded_temporal=temporal;recording_active=true;phase=Phase::OutputRecordedPendingHip;
 }
 // Called for each execution, with submission facts rather than recording state.
 // A missing consumer is a discard of that execution, not retirement of the lease.
 void EndRecordedExecution(ID3D12CommandQueue*actual,bool producer_submitted,bool consumer_submitted){
  if(!recording_leases||!recording_active)throw std::runtime_error("no recording execution");
  QueueContract(actual);
  if(consumer_submitted&&!producer_submitted)throw std::runtime_error("consumer without producer");
  if(producer_submitted){
   // A failed enqueue cannot be certified by a later successful queue signal.
   if(failed||phase!=Phase::OutputRecorded){recording_submission_unconfirmed=true;throw std::runtime_error("recorded HIP execution incomplete");}
   pending=true;const UINT64 target=++value;
   HRESULT hr=actual->Signal(fence,target);
   if(FAILED(hr)){recording_submission_unconfirmed=true;Check(hr,"recorded consumer signal");}
   recording_completion=target;
  }
  recording_active=false;readable=false;phase=Phase::Ready;
 }
 void PrepareStagedKernels(){
  Require(Phase::Ready);
  if(pending||value||readable||network->GraphEnabled())throw std::runtime_error("bridge preparation requires fresh graph-off session");
  auto&api=network->Runtime();
  try{api.Check(api.hipSetDevice(hip_device),"select HIP owner for preparation");api.Check(api.hipMemsetAsync(input.mapped,0,pixels*16,network->Stream()),"prepare input");network->Enqueue(input.mapped,nullptr,output.mapped,1);network->InvalidateExperimentalHistory();network->Synchronize();}
  catch(...){failed=true;throw;}
 }
 void RecordInputCopy(ID3D12GraphicsCommandList*c,ID3D12Resource*rgba,ID3D12Resource*temporal=nullptr){RecordInput(c,rgba,temporal,true);}
 void PrepareHipPassthrough(){
  Require(Phase::Ready);
  if(passthrough_rgb)return;
  auto&api=network->Runtime();
  api.Check(api.hipSetDevice(hip_device),"select HIP device for passthrough preparation");
  passthrough_copy=reinterpret_cast<PassthroughCopyFn>(GetProcAddress(api.dll,"hipMemcpy2DAsync"));
  if(!passthrough_copy)throw std::runtime_error("HIP passthrough requires hipMemcpy2DAsync");
  api.Check(api.hipMalloc(&passthrough_rgb,pixels*12),"HIP passthrough RGB buffer");
 }
 void EnqueueHipPassthroughAfterProducer(ID3D12CommandQueue*producer){Enqueue(producer,1,false,true,true);}
 void EnqueueAfterProducer(ID3D12CommandQueue*producer,U seed,bool temporal=false){Enqueue(producer,seed,temporal,true);}
 void RecordOutputReadable(ID3D12GraphicsCommandList*c){
  if(!network||failed)throw std::runtime_error("bridge unavailable");
  const bool before_enqueue=phase==Phase::InputRecorded;
  if(phase!=Phase::HipQueued&&!before_enqueue)throw std::runtime_error("bridge stage order");
  ListContract(c);
  try{Barrier(c,output.resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);readable=true;phase=before_enqueue?Phase::OutputRecordedPendingHip:Phase::OutputRecorded;}catch(...){failed=true;throw;}
 }
 private:
 bool ClearOutputAsync() noexcept {
  if(!network||failed||!output.mapped)return false;
  auto&api=network->Runtime();
  try{
   api.Check(api.hipMemsetAsync(output.mapped,0,pixels*12,network->Stream()),"clear output");
   network->Synchronize();
   return true;
  }catch(...){
   failed=true;
   return false;
  }
 }
 bool ClearOutputD3D12(ID3D12CommandQueue* targetQueue) noexcept {
  if(!network||!device||!targetQueue||!output.resource||clear_submission_unconfirmed)return false;
  // A failed HIP call can leave earlier work queued. Do not race that work with
  // a D3D12 write to the same shared buffer.
  if(network->Runtime().hipStreamSynchronize(network->Stream())!=0)return false;
  ID3D12Device* owner=nullptr;
  if(FAILED(targetQueue->GetDevice(IID_PPV_ARGS(&owner)))||!owner)return false;
  const bool sameDevice=NativeSameDevice(owner,device);owner->Release();
  if(!sameDevice)return false;
  if(!EnsureZeroClearResources())return false;
  ID3D12CommandAllocator* alloc=clear_alloc;
  ID3D12GraphicsCommandList* cmd=clear_cmd;
  ID3D12Fence* completion=nullptr;
  HANDLE completedEvent=nullptr;
  bool temp=false;
  bool submitted=false;
  const auto qType=targetQueue->GetDesc().Type;
  if(qType!=D3D12_COMMAND_LIST_TYPE_DIRECT||!alloc||!cmd){
   if(qType!=D3D12_COMMAND_LIST_TYPE_DIRECT&&qType!=D3D12_COMMAND_LIST_TYPE_COMPUTE)return false;
   if(FAILED(device->CreateCommandAllocator(qType,IID_PPV_ARGS(&alloc))))return false;
   if(FAILED(device->CreateCommandList(0,qType,alloc,nullptr,IID_PPV_ARGS(&cmd)))){alloc->Release();return false;}
   temp=true;
  }else if(FAILED(alloc->Reset())||FAILED(cmd->Reset(alloc,nullptr))){return false;}
  bool ok=false;
  try{
   if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&completion))))throw std::runtime_error("clear fence");
   completedEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
   if(!completedEvent)throw std::runtime_error("clear event");
   Barrier(cmd,output.resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
   const UINT64 total=UINT64(pixels)*12;
   for(UINT64 offset=0;offset<total;offset+=zero_upload_bytes)
    cmd->CopyBufferRegion(output.resource,offset,zero_upload,0,std::min<UINT64>(zero_upload_bytes,total-offset));
   Barrier(cmd,output.resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
   Check(cmd->Close(),"close clear command list");
   ID3D12CommandList* lists[]={cmd};
   targetQueue->ExecuteCommandLists(1,lists);
   submitted=true;clear_submission_unconfirmed=true;
   Check(targetQueue->Signal(completion,1),"signal clear completion");
   Check(completion->SetEventOnCompletion(1,completedEvent),"wait for clear completion");
   // Bound this synchronous D3D12 wait to limit a recovery stall.
   // HIP stream synchronization above is not covered by this timeout.
   ok=WaitForSingleObject(completedEvent,3000)==WAIT_OBJECT_0&&completion->GetCompletedValue()>=1&&SUCCEEDED(device->GetDeviceRemovedReason());
   if(ok)clear_submission_unconfirmed=false;
  }catch(...){ok=false;}
  // SetEventOnCompletion can still signal after a timeout. Keep its fence and
  // event alive whenever the submitted work has not been confirmed complete.
  if(!submitted||ok){if(completedEvent)CloseHandle(completedEvent);if(completion)completion->Release();}
  // If submission completion is unknown, retain command storage until the
  // session's fail-closed teardown instead of freeing a GPU-live allocator.
  if(temp&&(!submitted||ok)){cmd->Release();alloc->Release();}
  return ok;
 }
 public:
 // After producer submission and before consumer submission, clear the private
 // neural output so a normal decoder view can use original Color. The caller must drain
 // any other queue that used Output() before calling this method, and drain a
 // different consumer queue before reusing or destroying the bridge. On false,
 // do not submit the consumer or reuse the bridge.
 bool ClearOutput(ID3D12CommandQueue* targetQueue) noexcept {
  if(phase!=Phase::InputRecorded&&phase!=Phase::OutputRecordedPendingHip)return false;
  if(!targetQueue||!device)return false;
  const auto qType=targetQueue->GetDesc().Type;
  if(qType!=D3D12_COMMAND_LIST_TYPE_DIRECT&&qType!=D3D12_COMMAND_LIST_TYPE_COMPUTE)return false;
  ID3D12Device* owner=nullptr;
  if(FAILED(targetQueue->GetDevice(IID_PPV_ARGS(&owner)))||!owner)return false;
  const bool sameDevice=NativeSameDevice(owner,device);owner->Release();
  if(!sameDevice)return false;
  const bool consumer_recorded=phase==Phase::OutputRecordedPendingHip;
  if(!ClearOutputAsync()&&!ClearOutputD3D12(targetQueue))return false;
  // The stream and clear queue are confirmed complete. A pre-recorded consumer
  // can now submit; otherwise RecordOutputReadable may still be called.
  failed=false;
  // WaitForSubmittedWork must fence a later consumer on the bridge queue,
  // even when no regular HIP enqueue happened on this frame.
  pending=true;
  phase=consumer_recorded?Phase::OutputRecorded:Phase::HipQueued;
  return true;
 }
 // Acknowledges submission, not GPU completion. Queue order protects the next frame;
 // the destructor fences submitted work. Omitting this acknowledgement prevents reuse/free.
 void NotifyOutputSubmitted(ID3D12CommandQueue*consumer){Require(Phase::OutputRecorded);QueueContract(consumer);phase=Phase::Ready;}
 void NotifyOutputSubmittedIfRecorded(ID3D12CommandQueue*consumer){if(phase==Phase::OutputRecorded&&consumer){if(failed){phase=Phase::Ready;return;}NotifyOutputSubmitted(consumer);}}
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
