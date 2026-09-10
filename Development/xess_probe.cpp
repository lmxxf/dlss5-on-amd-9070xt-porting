// XeSS input probe (Rise of the Ronin and other Streamline/XeSS titles without an FSR dll): a ReShade addon that hooks
// libxess.dll's xessD3D12Init / xessD3D12Execute / xessSetVelocityScale / xessSetJitterScale and logs the upscaler's
// inputs (resolutions, formats, jitter, exposure, scales) to xess-probe.txt next to the addon. Read-only: every hook
// calls the original. Struct layouts follow the XeSS 1.x SDK (xess_d3d12.h).
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <MinHook.h>
#include <reshade.hpp>
struct xess_2d_t{uint32_t x,y;};struct xess_coord_t{int32_t x,y;};
struct xess_d3d12_init_params_t{xess_2d_t outputResolution;int32_t qualitySetting;uint32_t initFlags,creationNodeMask,visibleNodeMask;ID3D12DescriptorHeap*pTempBufferHeap;uint32_t bufferHeapOffset;ID3D12DescriptorHeap*pTempTextureHeap;uint32_t textureHeapOffset;ID3D12Resource*pPipelineLibrary;};
struct xess_d3d12_execute_params_t{ID3D12Resource*pColorTexture,*pVelocityTexture,*pDepthTexture,*pExposureScaleTexture,*pResponsivePixelMaskTexture,*pOutputTexture;float jitterOffsetX,jitterOffsetY,exposureScale;uint32_t resetHistory,inputWidth,inputHeight;xess_coord_t inputColorBase,inputMotionVectorBase,inputDepthBase,inputResponsiveMaskBase,outputColorBase;ID3D12DescriptorHeap*pDescriptorHeap;uint32_t descriptorHeapOffset;};
using InitFn=int(*)(void*,const xess_d3d12_init_params_t*);using ExecFn=int(*)(void*,ID3D12GraphicsCommandList*,const xess_d3d12_execute_params_t*);using ScaleFn=int(*)(void*,float,float);
static InitFn original_init;static ExecFn original_exec;static ScaleFn original_vscale,original_jscale;
static wchar_t log_path[MAX_PATH];static std::atomic<unsigned>executes{0};static HMODULE self_module;
static void logf(const char*fmt,...){FILE*f=_wfopen(log_path,L"ab");if(!f)return;va_list a;va_start(a,fmt);fprintf(f,"pid=%lu tick=%llu ",GetCurrentProcessId(),GetTickCount64());vfprintf(f,fmt,a);fputc('\n',f);va_end(a);fclose(f);}
static void describe(const char*name,ID3D12Resource*r){if(!r){logf("  %s=null",name);return;}D3D12_RESOURCE_DESC d=r->GetDesc();logf("  %s=%p dim=%u %llux%u format=%u mips=%u flags=%u",name,r,unsigned(d.Dimension),(unsigned long long)d.Width,d.Height,unsigned(d.Format),unsigned(d.MipLevels),unsigned(d.Flags));}
static int hooked_init(void*ctx,const xess_d3d12_init_params_t*p){
 if(p)logf("xessD3D12Init ctx=%p output=%ux%u quality=%d initFlags=0x%x nodes=%u/%u pipelineLibrary=%p",ctx,p->outputResolution.x,p->outputResolution.y,p->qualitySetting,p->initFlags,p->creationNodeMask,p->visibleNodeMask,p->pPipelineLibrary);
 int r=original_init(ctx,p);logf("xessD3D12Init result=%d",r);return r;
}
static int hooked_exec(void*ctx,ID3D12GraphicsCommandList*list,const xess_d3d12_execute_params_t*p){
 unsigned n=executes.fetch_add(1);
 if(p&&(n<8||n%600==0)){
  logf("xessD3D12Execute #%u ctx=%p list=%p jitter=(%g,%g) exposureScale=%g reset=%u input=%ux%u colorBase=(%d,%d) mvBase=(%d,%d) depthBase=(%d,%d) maskBase=(%d,%d) outBase=(%d,%d) heap=%p+%u",n,ctx,list,p->jitterOffsetX,p->jitterOffsetY,p->exposureScale,p->resetHistory,p->inputWidth,p->inputHeight,p->inputColorBase.x,p->inputColorBase.y,p->inputMotionVectorBase.x,p->inputMotionVectorBase.y,p->inputDepthBase.x,p->inputDepthBase.y,p->inputResponsiveMaskBase.x,p->inputResponsiveMaskBase.y,p->outputColorBase.x,p->outputColorBase.y,p->pDescriptorHeap,p->descriptorHeapOffset);
  if(n<8){describe("color",p->pColorTexture);describe("velocity",p->pVelocityTexture);describe("depth",p->pDepthTexture);describe("exposure",p->pExposureScaleTexture);describe("responsive",p->pResponsivePixelMaskTexture);describe("output",p->pOutputTexture);}
 }
 return original_exec(ctx,list,p);
}
static int hooked_vscale(void*ctx,float x,float y){logf("xessSetVelocityScale ctx=%p (%g,%g)",ctx,x,y);return original_vscale(ctx,x,y);}
static int hooked_jscale(void*ctx,float x,float y){logf("xessSetJitterScale ctx=%p (%g,%g)",ctx,x,y);return original_jscale(ctx,x,y);}
static DWORD WINAPI worker(void*){
 HMODULE m=nullptr;for(unsigned i=0;i<6000&&!m;i++){m=GetModuleHandleW(L"libxess.dll");if(!m)Sleep(100);}
 if(!m){logf("libxess.dll not loaded within 600s (XeSS not selected in the game settings?)");return 1;}
 auto s=MH_Initialize();if(s!=MH_OK&&s!=MH_ERROR_ALREADY_INITIALIZED){logf("MH_Initialize=%u",unsigned(s));return 2;}
 struct{const char*name;void*hook;void**original;}hooks[]={{"xessD3D12Init",(void*)&hooked_init,(void**)&original_init},{"xessD3D12Execute",(void*)&hooked_exec,(void**)&original_exec},{"xessSetVelocityScale",(void*)&hooked_vscale,(void**)&original_vscale},{"xessSetJitterScale",(void*)&hooked_jscale,(void**)&original_jscale}};
 for(auto&h:hooks){void*t=(void*)GetProcAddress(m,h.name);if(!t){logf("%s: not exported",h.name);continue;}auto c=MH_CreateHook(t,h.hook,h.original);auto e=c==MH_OK?MH_EnableHook(t):c;logf("%s at %p hook=%u",h.name,t,unsigned(e));}
 using VerFn=int(*)(void*);if(auto v=(int(*)(uint32_t*))GetProcAddress(m,"xessGetVersion")){uint32_t ver[3]{};v(ver);logf("xessGetVersion %u.%u.%u",ver[0],ver[1],ver[2]);}
 return 0;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(h);self_module=h;GetModuleFileNameW(h,log_path,MAX_PATH);wchar_t*slash=wcsrchr(log_path,L'\\');if(slash)wcscpy(slash+1,L"xess-probe.txt");
  if(!reshade::register_addon(h))return FALSE;
  wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);logf("attached to %ls",exe);
  HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned);
  HANDLE t=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(t)CloseHandle(t);
 }return TRUE;
}
