#pragma once
#include <chrono>
#include <cstring>
#include <cstdio>
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
 /* DLSS5_HIP_SPAN_PROBE=1 (diagnostic): hipEvents recorded after the input wait and before the output signal give the
    GPU span of one network enqueue; the previous frame's span and its CPU enqueue time are printed at the next Run. */
 Handle span_begin{},span_end{};bool span_probe{},span_pending{};double span_cpu{};
 static void Check(HRESULT h,const char*what){if(FAILED(h))throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(unsigned(h)));}
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){if(before==after)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};c->ResourceBarrier(1,&b);}
 void Share(Shared&s,size_t bytes,bool uav=false){
  auto&api=network->Runtime();D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
  Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_SHARED,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&s.resource)),"shared buffer");Check(device->CreateSharedHandle(s.resource,nullptr,GENERIC_ALL,nullptr,&s.handle),"buffer handle");
  hip_probe::MemoryDesc md{};md.type=5;md.handle.win32.handle=s.handle;md.size=device->GetResourceAllocationInfo(0,1,&rd).SizeInBytes;md.flags=1;api.Check(api.hipImportExternalMemory(&s.imported,&md),"import D3D12 resource");hip_probe::BufferDesc bd{};bd.size=bytes;api.Check(api.hipExternalMemoryGetMappedBuffer(&s.mapped,s.imported,&bd),"map shared resource");
 }
 void Release(Shared&s){auto&api=network->Runtime();if(s.mapped)api.hipFree(s.mapped);if(s.imported)api.hipDestroyExternalMemory(s.imported);if(s.handle)CloseHandle(s.handle);if(s.resource)s.resource->Release();s={};}
 void InputContract(ID3D12Resource*r){if(!r||r->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||r->GetDesc().Width<pixels*16)throw std::runtime_error("bridge input capacity");ID3D12Device*owner{};Check(r->GetDevice(IID_PPV_ARGS(&owner)),"input device");bool same=NativeSameDevice(owner,device);owner->Release();if(!same)throw std::runtime_error("bridge input device mismatch");}
public:
 D3D12Bridge()=default;D3D12Bridge(const D3D12Bridge&)=delete;D3D12Bridge&operator=(const D3D12Bridge&)=delete;
 ~D3D12Bridge(){
  // A failed GPU wait does not cancel queued commands. Keep backing storage alive.
  if(network&&network->Runtime().hipStreamSynchronize(network->Stream()))return;
  if(pending&&queue&&fence){auto target=++value;if(FAILED(queue->Signal(fence,target))||FAILED(fence->SetEventOnCompletion(target,event))||WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)return;}
  if(network){Release(input);Release(history);Release(output);if(semaphore)network->Runtime().hipDestroyExternalSemaphore(semaphore);delete network;}
  if(fence_handle)CloseHandle(fence_handle);if(event)CloseHandle(event);if(fence)fence->Release();if(queue)queue->Release();if(device)device->Release();
 }
 void Create(ID3D12CommandQueue*q,Options options,const std::vector<float>&noise){
  if(network||queue||!q)throw std::runtime_error("bridge already initialized/invalid queue");queue=q;queue->AddRef();Check(q->GetDevice(IID_PPV_ARGS(&device)),"queue device");pixels=size_t(options.width)*options.height;
  options.pooled=true;options.profile=false;options.dump_dir.clear();
  // Pick HIP by D3D12 adapter LUID. OptiScaler spoofs DXGI Description/VendorId to NVIDIA for DLSS, so VendorId==0x1002
  // and strcmp(Description, hipDeviceGetName) both fail; host HipRuntimeLoad matches LUID via hipGetDevicePropertiesR0600@272.
  IDXGIFactory4*factory{};IDXGIAdapter1*adapter{};Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");auto hr=factory->EnumAdapterByLuid(device->GetAdapterLuid(),IID_PPV_ARGS(&adapter));factory->Release();Check(hr,"D3D adapter");DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);adapter->Release();char dname[256]{};WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,dname,256,nullptr,nullptr);
  {Api probe(options.runtime);probe.Check(probe.hipInit(0),"hipInit");int count{};probe.Check(probe.hipGetDeviceCount(&count),"device count");
   using PropsFn=int(*)(void*,int);auto props=reinterpret_cast<PropsFn>(GetProcAddress(probe.dll,"hipGetDevicePropertiesR0600"));
   const LUID luid=device->GetAdapterLuid();int chosen=-1;std::string seen;
   for(int i=0;i<count;i++){char hname[256]{};if(probe.hipDeviceGetName(hname,256,i))continue;if(!seen.empty())seen+=" | ";seen+=std::to_string(i)+":"+hname;
    if(props){alignas(16) unsigned char properties[8192]{};if(props(properties,i)==0&&!std::memcmp(properties+272,&luid,sizeof(luid))){chosen=i;break;}}
    else if(chosen<0&&!strcmp(dname,hname))chosen=i;}
   if(chosen<0&&count==1)chosen=0;
   if(chosen<0){char vend[32]{};std::snprintf(vend,sizeof vend,"0x%X",unsigned(desc.VendorId));throw std::runtime_error(std::string("no HIP device matches D3D12 LUID (DXGI desc='")+dname+"' VendorId="+vend+" HIP: "+(seen.empty()?"none":seen)+")");}
   options.device=unsigned(chosen);hip_device=chosen;probe.Check(probe.hipSetDevice(chosen),"select device");size_t total=0;if(probe.hipMemGetInfo(&free_at_create,&total))free_at_create=0;}
  network=new Network(std::move(options));auto&api=network->Runtime();
  Share(input,pixels*16);Share(history,pixels*16);Share(output,pixels*12,true);Check(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence)),"shared fence");Check(device->CreateSharedHandle(fence,nullptr,GENERIC_ALL,nullptr,&fence_handle),"fence handle");hip_probe::SemaphoreDesc sd{};sd.type=4;sd.handle.win32.handle=fence_handle;api.Check(api.hipImportExternalSemaphore(&semaphore,&sd),"import fence");event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("bridge completion event");if(const char*v=std::getenv("DLSS5_HIP_SPAN_PROBE"))span_probe=!strcmp(v,"1");if(span_probe){api.Check(api.hipEventCreate(&span_begin),"span begin event");api.Check(api.hipEventCreate(&span_end),"span end event");fprintf(stderr,"hip_span probe enabled\n");}network->SetNoise(noise);
 }
 ID3D12Resource*Output()const{return output.resource;}
 size_t free_at_create{};int hip_device=-1;/* HIP device index chosen for the D3D12 adapter */
 void MemoryReport(FILE*f){if(!network)return;network->Runtime().hipStreamSynchronize(network->Stream());std::fprintf(f,"hip_memory device_free_before_network_MiB=%.1f shared input_MiB=%.1f history_MiB=%.1f output_MiB=%.1f\n",free_at_create/1048576.,pixels*16/1048576.,pixels*16/1048576.,pixels*12/1048576.);network->MemoryReport(f);}
#ifdef DLSS5_BENCH_BRIDGE_ISOLATE
 Network& DiagnosticNetwork(){return *network;}
#endif
 template<class Submission>void Run(Submission&submit,ID3D12Resource*rgba,ID3D12Resource*temporal,U seed){
  if(submit.Queue()!=queue)throw std::runtime_error("bridge submission queue mismatch");if(!network||failed)throw std::runtime_error("bridge unavailable");InputContract(rgba);if(temporal)InputContract(temporal);auto&api=network->Runtime();
  try{submit.Submit([&](ID3D12GraphicsCommandList*c){
   auto copy=[&](ID3D12Resource*src,Shared&dst){Barrier(c,src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(c,dst.resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);c->CopyBufferRegion(dst.resource,0,src,0,pixels*16);Barrier(c,dst.resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);Barrier(c,src,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
   copy(rgba,input);if(temporal)copy(temporal,history);if(readable)Barrier(c,output.resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
  });pending=true;Check(queue->Signal(fence,++value),"D3D input signal");hip_probe::WaitParams wait{};wait.params.fence.value=value;api.Check(api.hipWaitExternalSemaphoresAsync(&semaphore,&wait,1,network->Stream()),"HIP input wait");
  if(span_probe){if(span_pending){float ms=-1;int sync=api.hipEventSynchronize(span_end),status=api.hipEventElapsedTime(&ms,span_begin,span_end);fprintf(stderr,"hip_span gpu_ms=%.3f cpu_enqueue_ms=%.3f sync=%d status=%d\n",ms,span_cpu,sync,status);span_pending=false;}api.Check(api.hipEventRecord(span_begin,network->Stream()),"span begin");}
  auto enqueue_start=std::chrono::steady_clock::now();network->Enqueue(input.mapped,temporal?history.mapped:nullptr,output.mapped,seed);
  if(span_probe){span_cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-enqueue_start).count();api.Check(api.hipEventRecord(span_end,network->Stream()),"span end");span_pending=true;}
  hip_probe::SignalParams signal{};signal.params.fence.value=++value;api.Check(api.hipSignalExternalSemaphoresAsync(&semaphore,&signal,1,network->Stream()),"HIP output signal");Check(queue->Wait(fence,value),"D3D output wait");
  submit.Submit([&](ID3D12GraphicsCommandList*c){Barrier(c,output.resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});readable=true;
  }catch(...){failed=true;throw;}
 }
 /* Composable host API: Record* do not Execute. EnqueueHip after the producer list is submitted. graph must stay off. */
 void RecordInputCopy(ID3D12GraphicsCommandList*c,ID3D12Resource*rgba,ID3D12Resource*temporal){
  if(!c||!network||failed)throw std::runtime_error("bridge unavailable");InputContract(rgba);if(temporal)InputContract(temporal);
  auto copy=[&](ID3D12Resource*src,Shared&dst){Barrier(c,src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(c,dst.resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);c->CopyBufferRegion(dst.resource,0,src,0,pixels*16);Barrier(c,dst.resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);Barrier(c,src,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
  copy(rgba,input);if(temporal)copy(temporal,history);if(readable)Barrier(c,output.resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
 }
 void EnqueueAfterProducer(U seed,bool temporal){
  if(!network||failed)throw std::runtime_error("bridge unavailable");
  if(network->GraphEnabled())throw std::runtime_error("EnqueueHip forbids HIP graph capture");
  auto&api=network->Runtime();
  pending=true;Check(queue->Signal(fence,++value),"D3D input signal");
  hip_probe::WaitParams wait{};wait.params.fence.value=value;api.Check(api.hipWaitExternalSemaphoresAsync(&semaphore,&wait,1,network->Stream()),"HIP input wait");
  network->Enqueue(input.mapped,temporal?history.mapped:nullptr,output.mapped,seed);
  hip_probe::SignalParams signal{};signal.params.fence.value=++value;api.Check(api.hipSignalExternalSemaphoresAsync(&semaphore,&signal,1,network->Stream()),"HIP output signal");
  Check(queue->Wait(fence,value),"D3D output wait");
 }
 void RecordOutputReadable(ID3D12GraphicsCommandList*c){
  if(!c||!network||failed)throw std::runtime_error("bridge unavailable");
  Barrier(c,output.resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);readable=true;
 }
};
}
