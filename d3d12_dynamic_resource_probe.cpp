#include <windows.h>
#include <atomic>
#include <cstdio>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <vector>
#include "reshade.hpp"
#include "MinHook.h"

extern "C" __declspec(dllexport) const char *NAME="DLSS5 AMD Dynamic Resource Probe";
extern "C" __declspec(dllexport) const char *DESCRIPTION="Logs full-resolution D3D12 textures and live present cadence.";
namespace {
constexpr wchar_t kLog[]=LR"(D:\DLSSNR-Lab\logs\dynamic-resource-probe.txt)";
SRWLOCK g_lock=SRWLOCK_INIT;std::atomic<unsigned long long>g_frames{0};
std::atomic<reshade::api::swapchain*>g_main{nullptr};
std::atomic<bool>g_guides_logged{false};
std::atomic<reshade::api::effect_runtime*>g_runtime{nullptr};
ID3D12Resource*g_dynamic_texture=nullptr,*g_external_upload=nullptr;reshade::api::pipeline_layout g_dynamic_layout{};reshade::api::pipeline g_dynamic_pipeline{};reshade::api::resource_view g_dynamic_uav{};bool g_dynamic_ready=false,g_external_available=false,g_external_loaded=false;
FILETIME g_external_write_time{};
std::atomic<ID3D12Device*>g_main_device{nullptr};std::atomic<bool>g_hook_installed{false};
std::atomic<ID3D12Resource*>g_motion{nullptr};
std::atomic<ID3D12Resource*>g_depth{nullptr};
using CreateCommittedResourceFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
CreateCommittedResourceFn g_create_committed=nullptr;
struct FfxHeader{uint64_t type;FfxHeader*pNext;};
struct FfxDimensions2D{uint32_t width,height;};struct FfxFloat2{float x,y;};
struct FfxResourceDesc{uint32_t type,format,width,height,depth,mipCount,flags,usage;};
struct FfxResource{void*resource;FfxResourceDesc description;uint32_t state;};
struct FfxDispatchUpscale{FfxHeader header;void*commandList;FfxResource color,depth,motionVectors,exposure,reactive,transparency,output;FfxFloat2 jitterOffset,motionVectorScale;FfxDimensions2D renderSize,upscaleSize;bool enableSharpening;uint8_t pad0[3];float sharpness,frameTimeDelta,preExposure;bool reset;uint8_t pad1[3];float cameraNear,cameraFar,cameraFovAngleVertical,viewSpaceToMetersFactor;uint32_t flags;};
static_assert(sizeof(FfxResource)==48&&offsetof(FfxDispatchUpscale,color)==24&&offsetof(FfxDispatchUpscale,motionVectors)==120&&offsetof(FfxDispatchUpscale,output)==312);
using FfxDispatchFn=uint32_t(*)(void**,const FfxHeader*);FfxDispatchFn g_ffx_dispatch=nullptr;std::atomic<unsigned long long>g_ffx_frames{0};
std::atomic<ID3D12Resource*>g_ffx_color{nullptr},g_ffx_depth{nullptr},g_ffx_motion{nullptr},g_ffx_output{nullptr};
void log(const char*f,...){AcquireSRWLockExclusive(&g_lock);if(FILE*x=_wfopen(kLog,L"ab")){va_list a;va_start(a,f);vfprintf(x,f,a);va_end(a);fclose(x);}ReleaseSRWLockExclusive(&g_lock);}
HRESULT STDMETHODCALLTYPE hook_create_committed(ID3D12Device*self,const D3D12_HEAP_PROPERTIES*heap,D3D12_HEAP_FLAGS heap_flags,const D3D12_RESOURCE_DESC*desc,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE*clear,REFIID iid,void**out){
 const HRESULT hr=g_create_committed(self,heap,heap_flags,desc,state,clear,iid,out);
 if(SUCCEEDED(hr)&&self==g_main_device.load()&&desc&&desc->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&desc->Width>=960&&desc->Height>=544){log("create_texture resource=%p heap=%u width=%llu height=%u array=%u mips=%u format=%u flags=0x%x state=0x%x\n",out?*out:nullptr,heap?(unsigned)heap->Type:999u,(unsigned long long)desc->Width,desc->Height,desc->DepthOrArraySize,desc->MipLevels,(unsigned)desc->Format,(unsigned)desc->Flags,(unsigned)state);if(desc->Width==2564&&desc->Height==1444&&desc->Format==DXGI_FORMAT_R16G16_FLOAT&&out)g_motion.store(static_cast<ID3D12Resource*>(*out));if(desc->Width==2564&&desc->Height==1444&&desc->Format==DXGI_FORMAT_R32G8X24_TYPELESS&&out)g_depth.store(static_cast<ID3D12Resource*>(*out));}
 return hr;
}
uint32_t hook_ffx_dispatch(void**context,const FfxHeader*header){
 const auto n=++g_ffx_frames;if(header&&(header->type&0x00ffffffu)==0x00010001u){const auto*d=reinterpret_cast<const FfxDispatchUpscale*>(header);g_ffx_color.store(static_cast<ID3D12Resource*>(d->color.resource));g_ffx_depth.store(static_cast<ID3D12Resource*>(d->depth.resource));g_ffx_motion.store(static_cast<ID3D12Resource*>(d->motionVectors.resource));g_ffx_output.store(static_cast<ID3D12Resource*>(d->output.resource));if(n==1||n%120==0)log("ffx_upscale frame=%llu context=%p cmd=%p render=%ux%u upscale=%ux%u jitter=%g,%g mvscale=%g,%g color=%p[%u,%ux%u,s%u] depth=%p[%u,%ux%u,s%u] motion=%p[%u,%ux%u,s%u] output=%p[%u,%ux%u,s%u]\n",n,context,d->commandList,d->renderSize.width,d->renderSize.height,d->upscaleSize.width,d->upscaleSize.height,d->jitterOffset.x,d->jitterOffset.y,d->motionVectorScale.x,d->motionVectorScale.y,d->color.resource,d->color.description.format,d->color.description.width,d->color.description.height,d->color.state,d->depth.resource,d->depth.description.format,d->depth.description.width,d->depth.description.height,d->depth.state,d->motionVectors.resource,d->motionVectors.description.format,d->motionVectors.description.width,d->motionVectors.description.height,d->motionVectors.state,d->output.resource,d->output.description.format,d->output.description.width,d->output.description.height,d->output.state);}
 else if(header&&(n==1||n%120==0))log("ffx_dispatch frame=%llu type=0x%llx context=%p\n",n,(unsigned long long)header->type,context);return g_ffx_dispatch(context,header);
}
DWORD WINAPI ffx_hook_worker(void*){for(unsigned i=0;i<600;i++){if(HMODULE m=GetModuleHandleW(L"amd_fidelityfx_dx12.dll")){void*target=reinterpret_cast<void*>(GetProcAddress(m,"ffxDispatch"));const auto a=MH_Initialize();const auto b=target?MH_CreateHook(target,reinterpret_cast<void*>(&hook_ffx_dispatch),reinterpret_cast<void**>(&g_ffx_dispatch)):MH_ERROR_NOT_EXECUTABLE;const auto c=b==MH_OK?MH_EnableHook(target):b;log("ffxDispatch hook=%d/%d/%d module=%p target=%p\n",(int)a,(int)b,(int)c,m,target);return 0;}Sleep(100);}log("ffxDispatch hook timeout\n");return 1;}
void on_init_device(reshade::api::device*d){
 log("device api=%u native=%p\n",(unsigned)d->get_api(),d->get_native());
 if(d->get_api()==reshade::api::device_api::d3d12&&!g_hook_installed.exchange(true)){auto*dev=reinterpret_cast<ID3D12Device*>(static_cast<uintptr_t>(d->get_native()));void**vtable=*reinterpret_cast<void***>(dev);const auto a=MH_Initialize();const auto b=MH_CreateHook(vtable[27],reinterpret_cast<void*>(&hook_create_committed),reinterpret_cast<void**>(&g_create_committed));const auto c=b==MH_OK?MH_EnableHook(vtable[27]):b;log("CreateCommittedResource hook=%d/%d/%d target=%p\n",(int)a,(int)b,(int)c,vtable[27]);}
}
void on_init_swapchain(reshade::api::swapchain*s,bool resize){
 log("init_swapchain swapchain=%p native=%p resize=%u\n",s,s->get_native(),resize?1u:0u);
 auto*sc=reinterpret_cast<IDXGISwapChain3*>(static_cast<uintptr_t>(s->get_native()));ID3D12Device*dev=nullptr;if(SUCCEEDED(sc->GetDevice(IID_PPV_ARGS(&dev)))){g_main_device.store(dev);log("main_device=%p\n",dev);dev->Release();}
 if(resize)g_main.store(s);
}
void log_guides(reshade::api::effect_runtime*runtime){
 if(g_guides_logged.load())return;
 if(runtime->find_texture_variable(nullptr,"AMD_MV").handle==0)return;
 g_guides_logged.store(true);
 auto*dev=runtime->get_device();
 for(const char*name:{"AMD_Color","AMD_Depth","AMD_MV"}){
  const auto variable=runtime->find_texture_variable(nullptr,name);
  reshade::api::resource_view srv{},srgb{};runtime->get_texture_binding(variable,&srv,&srgb);
  const auto resource=srv.handle?dev->get_resource_from_view(srv):reshade::api::resource{};
  const auto desc=resource.handle?dev->get_resource_desc(resource):reshade::api::resource_desc{};
  log("guide name=%s variable=0x%llx srv=0x%llx resource=0x%llx type=%u width=%u height=%u format=%u\n",name,(unsigned long long)variable.handle,(unsigned long long)srv.handle,(unsigned long long)resource.handle,(unsigned)desc.type,desc.texture.width,desc.texture.height,(unsigned)desc.texture.format);
 }
}
void on_finish_effects(reshade::api::effect_runtime*runtime,reshade::api::command_list*,reshade::api::resource_view,reshade::api::resource_view){log_guides(runtime);}
void on_reshade_present(reshade::api::effect_runtime*runtime){log_guides(runtime);}
void on_init_effect_runtime(reshade::api::effect_runtime*runtime){g_runtime.store(runtime);log("init_effect_runtime runtime=%p\n",runtime);}
void on_reloaded_effects(reshade::api::effect_runtime*runtime){
 log("reloaded_effects runtime=%p\n",runtime);
}
bool init_dynamic_pass(reshade::api::device*api_dev,ID3D12Device*dev,const D3D12_RESOURCE_DESC&source){
 D3D12_FEATURE_DATA_FORMAT_SUPPORT support{source.Format};
 if(FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)==0){log("dynamic_pass unsupported format=%u support2=0x%x\n",(unsigned)source.Format,(unsigned)support.Support2);return false;}
 D3D12_RESOURCE_DESC td=source;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
 HRESULT hr=dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&g_dynamic_texture));
 ID3DBlob*err=nullptr;
 static const char shader[]="RWTexture2D<float4> image:register(u0);[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){uint w,h;image.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;float4 c=image[id.xy];image[id.xy]=float4(c.r,min(c.g*0.55+0.08,1.0),min(c.b*0.55+0.08,1.0),c.a);}";
 ID3DBlob*cs=nullptr;if(SUCCEEDED(hr))hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&cs,&err);
 const reshade::api::descriptor_range range{0,0,0,1,reshade::api::shader_stage::compute,1,reshade::api::descriptor_type::unordered_access_view};const reshade::api::pipeline_layout_param param{range};
 if(SUCCEEDED(hr)&&!api_dev->create_pipeline_layout(1,&param,&g_dynamic_layout))hr=E_FAIL;
 reshade::api::shader_desc shader_desc{};if(cs){shader_desc.code=cs->GetBufferPointer();shader_desc.code_size=cs->GetBufferSize();}
 const reshade::api::pipeline_subobject subobject{reshade::api::pipeline_subobject_type::compute_shader,1,&shader_desc};if(SUCCEEDED(hr)&&!api_dev->create_pipeline(g_dynamic_layout,1,&subobject,&g_dynamic_pipeline))hr=E_FAIL;
 const reshade::api::resource texture{reinterpret_cast<uint64_t>(g_dynamic_texture)};if(SUCCEEDED(hr)&&!api_dev->create_resource_view(texture,reshade::api::resource_usage::unordered_access,reshade::api::resource_view_desc(static_cast<reshade::api::format>(source.Format)),&g_dynamic_uav))hr=E_FAIL;
 if(SUCCEEDED(hr)){if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\dlss5-output-r10.bin)",L"rb")){const UINT64 bytes=source.Width*source.Height*4;D3D12_RESOURCE_DESC upload_desc{};upload_desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;upload_desc.Width=bytes;upload_desc.Height=1;upload_desc.DepthOrArraySize=upload_desc.MipLevels=1;upload_desc.SampleDesc.Count=1;upload_desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES upload_heap{};upload_heap.Type=D3D12_HEAP_TYPE_UPLOAD;if(SUCCEEDED(dev->CreateCommittedResource(&upload_heap,D3D12_HEAP_FLAG_NONE,&upload_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&g_external_upload)))){void*mapped=nullptr;D3D12_RANGE none{0,0};if(SUCCEEDED(g_external_upload->Map(0,&none,&mapped))&&fread(mapped,1,(size_t)bytes,f)==bytes){g_external_upload->Unmap(0,nullptr);g_external_available=true;WIN32_FILE_ATTRIBUTE_DATA data{};if(GetFileAttributesExW(LR"(D:\DLSSNR-Lab\dlss5-output-r10.bin)",GetFileExInfoStandard,&data))g_external_write_time=data.ftLastWriteTime;}}fclose(f);}}
 if(cs)cs->Release();if(err){if(FAILED(hr))log("dynamic_pass compiler=%s\n",(const char*)err->GetBufferPointer());err->Release();}
 log("dynamic_pass init hr=0x%08x format=%u size=%llux%u support2=0x%x\n",(unsigned)hr,(unsigned)source.Format,(unsigned long long)source.Width,source.Height,(unsigned)support.Support2);return SUCCEEDED(hr);
}
bool run_dynamic_pass(reshade::api::command_queue*qwrap,ID3D12Resource*source,const D3D12_RESOURCE_DESC&desc,unsigned long long frame){
 ID3D12Device*dev=nullptr;if(FAILED(source->GetDevice(IID_PPV_ARGS(&dev))))return false;
 if(!g_dynamic_ready)g_dynamic_ready=init_dynamic_pass(qwrap->get_device(),dev,desc);
 if(!g_dynamic_ready){dev->Release();return false;}
 if(!g_external_available){dev->Release();return false;}
 if(g_external_available&&frame%120==0){WIN32_FILE_ATTRIBUTE_DATA data{};if(GetFileAttributesExW(LR"(D:\DLSSNR-Lab\dlss5-output-r10.bin)",GetFileExInfoStandard,&data)&&CompareFileTime(&data.ftLastWriteTime,&g_external_write_time)!=0){if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\dlss5-output-r10.bin)",L"rb")){void*mapped=nullptr;D3D12_RANGE none{0,0};const size_t bytes=(size_t)(desc.Width*desc.Height*4);if(SUCCEEDED(g_external_upload->Map(0,&none,&mapped))){const bool complete=fread(mapped,1,bytes,f)==bytes;g_external_upload->Unmap(0,nullptr);if(complete){g_external_write_time=data.ftLastWriteTime;g_external_loaded=false;log("dynamic external update detected frame=%llu\n",frame);}}fclose(f);}}}
 auto*cmd=qwrap->get_immediate_command_list();const reshade::api::resource src{reinterpret_cast<uint64_t>(source)},tmp{reinterpret_cast<uint64_t>(g_dynamic_texture)};
 if(g_external_available){if(!g_external_loaded){const reshade::api::resource upload{reinterpret_cast<uint64_t>(g_external_upload)};cmd->copy_buffer_to_texture(upload,0,(uint32_t)desc.Width,desc.Height,tmp,0,nullptr);cmd->barrier(tmp,reshade::api::resource_usage::copy_dest,reshade::api::resource_usage::copy_source);g_external_loaded=true;log("dynamic external DLSS5 output loaded frame=%llu\n",frame);}cmd->barrier(src,reshade::api::resource_usage::present,reshade::api::resource_usage::copy_dest);cmd->copy_texture_region(tmp,0,nullptr,src,0,nullptr);cmd->barrier(src,reshade::api::resource_usage::copy_dest,reshade::api::resource_usage::present);dev->Release();return true;}
 cmd->barrier(src,reshade::api::resource_usage::present,reshade::api::resource_usage::copy_source);if(frame>600)cmd->barrier(tmp,reshade::api::resource_usage::copy_source,reshade::api::resource_usage::copy_dest);cmd->copy_texture_region(src,0,nullptr,tmp,0,nullptr);cmd->barrier(tmp,reshade::api::resource_usage::copy_dest,reshade::api::resource_usage::unordered_access);
 cmd->bind_pipeline(reshade::api::pipeline_stage::compute_shader,g_dynamic_pipeline);reshade::api::descriptor_table_update update{{},0,0,1,reshade::api::descriptor_type::unordered_access_view,&g_dynamic_uav};cmd->push_descriptors(reshade::api::shader_stage::compute,g_dynamic_layout,0,update);cmd->dispatch((UINT(desc.Width)+7)/8,(desc.Height+7)/8,1);
 cmd->barrier(tmp,reshade::api::resource_usage::unordered_access,reshade::api::resource_usage::copy_source);cmd->barrier(src,reshade::api::resource_usage::copy_source,reshade::api::resource_usage::copy_dest);cmd->copy_texture_region(tmp,0,nullptr,src,0,nullptr);cmd->barrier(src,reshade::api::resource_usage::copy_dest,reshade::api::resource_usage::present);if(frame==600)log("dynamic frame=%llu recorded via ReShade API\n",frame);dev->Release();return true;
}
bool capture_resource(reshade::api::command_queue*qwrap,ID3D12Resource*buffer,unsigned long long frame,D3D12_RESOURCE_DESC desc,const wchar_t*label,reshade::api::resource_usage old_usage){
 ID3D12Device*dev=nullptr;if(FAILED(buffer->GetDevice(IID_PPV_ARGS(&dev))))return false;
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 row_bytes=0,total=0;
 dev->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&row_bytes,&total);
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
 D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ID3D12Resource*readback=nullptr;
 HRESULT hr=dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback));
 if(SUCCEEDED(hr)){
  auto*cmd=qwrap->get_immediate_command_list();
  reshade::api::resource src{reinterpret_cast<uint64_t>(buffer)},dst{reinterpret_cast<uint64_t>(readback)};
  cmd->barrier(src,old_usage,reshade::api::resource_usage::copy_source);
  cmd->copy_texture_to_buffer(src,0,nullptr,dst,0,(uint32_t)desc.Width,desc.Height);
  cmd->barrier(src,reshade::api::resource_usage::copy_source,old_usage);
  qwrap->flush_immediate_command_list();qwrap->wait_idle();
 }
 unsigned long long hash=1469598103934665603ull;
 if(SUCCEEDED(hr)){void*p=nullptr;D3D12_RANGE range{0,(SIZE_T)total};hr=readback->Map(0,&range,&p);if(SUCCEEDED(hr)){wchar_t path[256];swprintf(path,256,LR"(D:\DLSSNR-Lab\logs\%ls-%llu.bin)",label,frame);if(FILE*f=_wfopen(path,L"wb")){fwrite(p,1,(size_t)total,f);fclose(f);}for(size_t i=0;i<(size_t)total;i++){hash^=((unsigned char*)p)[i];hash*=1099511628211ull;}D3D12_RANGE empty{0,0};readback->Unmap(0,&empty);}}
 const HRESULT removed=dev->GetDeviceRemovedReason();
 log("capture frame=%llu queue=%p hr=0x%08x removed=0x%08x width=%llu height=%u format=%u row_pitch=%u bytes=%llu fnv64=%016llx\n",frame,qwrap,(unsigned)hr,(unsigned)removed,(unsigned long long)desc.Width,desc.Height,(unsigned)desc.Format,footprint.Footprint.RowPitch,(unsigned long long)total,hash);
 if(readback)readback->Release();dev->Release();return SUCCEEDED(hr);
}
void on_present(reshade::api::command_queue*qwrap,reshade::api::swapchain*s,const reshade::api::rect*,const reshade::api::rect*,uint32_t,const reshade::api::rect*){
 if(s!=g_main.load())return;
 auto n=++g_frames;const bool report=n==1||n%120==0;if(report||n>=600){
  if(report)log("main_present frame=%llu swapchain=%p native=%p\n",n,s,s->get_native());
  auto *sc=reinterpret_cast<IDXGISwapChain3 *>(static_cast<uintptr_t>(s->get_native()));
  const UINT index=sc->GetCurrentBackBufferIndex();
  ID3D12Resource *buffer=nullptr;
  const HRESULT hr=sc->GetBuffer(index,IID_PPV_ARGS(&buffer));
  if(SUCCEEDED(hr)){
   const auto desc=buffer->GetDesc();
   if(report)log("present frame=%llu swapchain=%p native=%p current=%u resource=%p width=%llu height=%u format=%u flags=0x%x\n",n,s,sc,index,buffer,(unsigned long long)desc.Width,desc.Height,(unsigned)desc.Format,(unsigned)desc.Flags);
   if(n==240||n==480||n%600==0)capture_resource(qwrap,buffer,n,desc,L"dynamic-frame",reshade::api::resource_usage::present);
   if(n%600==0)if(auto*color=g_ffx_color.load())capture_resource(qwrap,color,n,color->GetDesc(),L"ffx-color",reshade::api::resource_usage::shader_resource_non_pixel);
   if(n%600==0)if(auto*depth=g_ffx_depth.load())capture_resource(qwrap,depth,n,depth->GetDesc(),L"ffx-depth",reshade::api::resource_usage::shader_resource_non_pixel);
   if(n%600==0)if(auto*motion=g_ffx_motion.load())capture_resource(qwrap,motion,n,motion->GetDesc(),L"ffx-motion",reshade::api::resource_usage::shader_resource_non_pixel);
   if(n>=600)run_dynamic_pass(qwrap,buffer,desc,n);
   buffer->Release();
  }else log("GetBuffer current=%u hr=0x%08x\n",index,(unsigned)hr);
 }
 if(n==300)if(auto*runtime=g_runtime.load())log_guides(runtime);
}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(h);
  HMODULE pinned=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&on_present),&pinned))return FALSE;
  DeleteFileW(kLog);
  if(!reshade::register_addon(h))return FALSE;
  reshade::register_event<reshade::addon_event::init_device>(on_init_device);
  reshade::register_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
  reshade::register_event<reshade::addon_event::present>(on_present);
  if(HANDLE thread=CreateThread(nullptr,0,ffx_hook_worker,nullptr,0,nullptr))CloseHandle(thread);
 }else if(reason==DLL_PROCESS_DETACH){
  reshade::unregister_event<reshade::addon_event::present>(on_present);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
  reshade::unregister_event<reshade::addon_event::init_device>(on_init_device);
  reshade::unregister_addon(h);
 }
 return TRUE;
}
