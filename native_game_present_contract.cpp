#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <atomic>
#include "reshade.hpp"
extern "C" __declspec(dllexport) const char*NAME="Native DLSS5 Present Contract (read-only)";
extern "C" __declspec(dllexport) const char*DESCRIPTION="Records display geometry and color space; never modifies pixels or submits GPU commands.";
static std::atomic<unsigned long long>frames{0};
static std::atomic<reshade::api::swapchain*>main_swapchain{nullptr};
static void mark(const char*step){if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-present-contract.txt)",L"ab")){std::fprintf(f,"pid=%lu step=%s\n",GetCurrentProcessId(),step);std::fclose(f);}}
static void init(reshade::api::swapchain*s,bool resize){if(resize){main_swapchain.store(s);mark("main_resize_registered");}}
static void destroy(reshade::api::swapchain*s,bool){auto*expected=s;main_swapchain.compare_exchange_strong(expected,nullptr);}
static void present(reshade::api::command_queue*q,reshade::api::swapchain*s,
 const reshade::api::rect*,const reshade::api::rect*,uint32_t,const reshade::api::rect*){
 if(!q||!s||s!=main_swapchain.load()||q->get_device()->get_api()!=reshade::api::device_api::d3d12)return;
 const auto n=++frames;if(n!=1&&n%300)return;
 mark("before_native_backbuffer");
 auto*sc=reinterpret_cast<IDXGISwapChain3*>(s->get_native());if(!sc)return;
 ID3D12Resource*back=nullptr;const UINT index=sc->GetCurrentBackBufferIndex();if(FAILED(sc->GetBuffer(index,IID_PPV_ARGS(&back)))){mark("get_buffer_failed");return;}
 auto desc=back->GetDesc();back->Release();mark("before_colorspace");auto colorspace=s->get_color_space();mark("after_colorspace");
 auto*queue=reinterpret_cast<ID3D12CommandQueue*>(q->get_native());ID3D12Device*device=nullptr;
 HRESULT hr=queue?queue->GetDevice(IID_PPV_ARGS(&device)):E_POINTER;
 LUID luid{};if(device){luid=device->GetAdapterLuid();device->Release();}
 if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-present-contract.txt)",L"ab")){
  std::fprintf(f,"pid=%lu frame=%llu swapchain=%p queue=%p size=%llux%u format=%u colorspace=%u index=%u queue_device_hr=%08x luid=%08lx:%08lx read_only=1\n",GetCurrentProcessId(),n,s,queue,static_cast<unsigned long long>(desc.Width),desc.Height,unsigned(desc.Format),unsigned(colorspace),index,unsigned(hr),static_cast<unsigned long>(luid.HighPart),luid.LowPart);std::fclose(f);
 }
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(h);wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
  if(!wcsstr(path,L"SB-Win64-Shipping.exe"))return FALSE;
  if(!reshade::register_addon(h))return FALSE;
  reshade::register_event<reshade::addon_event::present>(present);
  reshade::register_event<reshade::addon_event::init_swapchain>(init);reshade::register_event<reshade::addon_event::destroy_swapchain>(destroy);
 }else if(reason==DLL_PROCESS_DETACH){reshade::unregister_event<reshade::addon_event::present>(present);reshade::unregister_event<reshade::addon_event::init_swapchain>(init);reshade::unregister_event<reshade::addon_event::destroy_swapchain>(destroy);reshade::unregister_addon(h);}
 return TRUE;
}
