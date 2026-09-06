#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <atomic>
#include "reshade.hpp"
extern "C" __declspec(dllexport) const char*NAME="Native DLSS5 Present Contract (read-only)";
extern "C" __declspec(dllexport) const char*DESCRIPTION="Records display geometry and color space; never modifies pixels or submits GPU commands.";
static std::atomic<unsigned long long>frames{0};
static void present(reshade::api::command_queue*q,reshade::api::swapchain*s,
 const reshade::api::rect*,const reshade::api::rect*,uint32_t,const reshade::api::rect*){
 if(!q||!s||q->get_device()->get_api()!=reshade::api::device_api::d3d12)return;
 const auto n=++frames;if(n!=1&&n%300)return;
 auto resource=s->get_current_back_buffer();if(!resource.handle)return;
 auto desc=s->get_device()->get_resource_desc(resource);
 auto*queue=reinterpret_cast<ID3D12CommandQueue*>(q->get_native());ID3D12Device*device=nullptr;
 HRESULT hr=queue?queue->GetDevice(IID_PPV_ARGS(&device)):E_POINTER;
 LUID luid{};if(device){luid=device->GetAdapterLuid();device->Release();}
 if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-present-contract.txt)",L"ab")){
  std::fprintf(f,"pid=%lu frame=%llu swapchain=%p queue=%p size=%ux%u format=%u colorspace=%u buffers=%u index=%u queue_device_hr=%08x luid=%08lx:%08lx read_only=1\n",GetCurrentProcessId(),n,s,queue,desc.texture.width,desc.texture.height,unsigned(desc.texture.format),unsigned(s->get_color_space()),s->get_back_buffer_count(),s->get_current_back_buffer_index(),unsigned(hr),static_cast<unsigned long>(luid.HighPart),luid.LowPart);std::fclose(f);
 }
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(h);wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
  if(!wcsstr(path,L"SB-Win64-Shipping.exe"))return FALSE;
  if(!reshade::register_addon(h))return FALSE;
  reshade::register_event<reshade::addon_event::present>(present);
 }else if(reason==DLL_PROCESS_DETACH){reshade::unregister_event<reshade::addon_event::present>(present);reshade::unregister_addon(h);}
 return TRUE;
}
