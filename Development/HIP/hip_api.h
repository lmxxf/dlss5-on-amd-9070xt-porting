#pragma once
// Minimal Windows dynamic HIP ABI used by probes, from ROCm/HIP rocm-7.1.1 hip_runtime_api.h.
// Copyright (c) 2015-2023 Advanced Micro Devices, Inc. MIT license:
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute,
// sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions: The above copyright notice and this
// permission notice shall be included in all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include "hip_device_properties.h"
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string>
namespace hip_probe {
using Handle=void*;
struct MemoryDesc {int type;union {int fd;struct {void*handle;const void*name;} win32;const void*nvSciBufObject;} handle;unsigned long long size;unsigned flags;unsigned reserved[16];};
struct BufferDesc {unsigned long long offset,size;unsigned flags;unsigned reserved[16];};
struct SemaphoreDesc {int type;union {int fd;struct {void*handle;const void*name;} win32;const void*nvSciSyncObj;} handle;unsigned flags;unsigned reserved[16];};
struct SignalParams {struct {struct {unsigned long long value;} fence;union {void*fence;unsigned long long reserved;} nvSciSync;struct {unsigned long long key;} keyedMutex;unsigned reserved[12];} params;unsigned flags;unsigned reserved[16];};
struct WaitParams {struct {struct {unsigned long long value;} fence;union {void*fence;unsigned long long reserved;} nvSciSync;struct {unsigned long long key;unsigned timeoutMs;} keyedMutex;unsigned reserved[10];} params;unsigned flags;unsigned reserved[16];};
// Virtual memory management (sparse weight mappings): hipMemAllocationProp / hipMemAccessDesc of rocm-7.x.
struct MemAllocationProp {unsigned type;unsigned requestedHandleType;struct {unsigned type;int id;} location;void*win32HandleMetaData;struct {unsigned char compressionType,gpuDirectRDMACapable;unsigned short usage;unsigned char reserved[4];} allocFlags;};
struct MemAccessDesc {struct {unsigned type;int id;} location;unsigned flags;};
static_assert(sizeof(MemoryDesc)==104&&sizeof(BufferDesc)==88&&sizeof(SemaphoreDesc)==96&&sizeof(SignalParams)==144&&sizeof(WaitParams)==144,"HIP external resource ABI");
struct Api {
 HMODULE dll{};
 #define HIP_FN(name,args) using name##Fn=int(*)args;name##Fn name{}
 HIP_FN(hipGetDevicePropertiesR0600,(DevicePropertiesR0600*,int));HIP_FN(hipModuleLoadData,(Handle*,const void*));
 HIP_FN(hipInit,(unsigned));HIP_FN(hipRuntimeGetVersion,(int*));HIP_FN(hipGetDeviceCount,(int*));HIP_FN(hipDeviceGetName,(char*,int,int));HIP_FN(hipSetDevice,(int));
 HIP_FN(hipMemGetInfo,(size_t*,size_t*));HIP_FN(hipMalloc,(void**,size_t));HIP_FN(hipHostMalloc,(void**,size_t,unsigned));HIP_FN(hipFree,(void*));HIP_FN(hipMemcpy,(void*,const void*,size_t,int));HIP_FN(hipMemcpyAsync,(void*,const void*,size_t,int,Handle));HIP_FN(hipMemsetAsync,(void*,int,size_t,Handle));
 HIP_FN(hipEventCreate,(Handle*));HIP_FN(hipEventRecord,(Handle,Handle));HIP_FN(hipEventElapsedTime,(float*,Handle,Handle));HIP_FN(hipEventDestroy,(Handle));HIP_FN(hipEventSynchronize,(Handle));
 HIP_FN(hipDeviceSynchronize,());HIP_FN(hipStreamCreate,(Handle*));HIP_FN(hipStreamSynchronize,(Handle));HIP_FN(hipStreamDestroy,(Handle));
 HIP_FN(hipImportExternalMemory,(Handle*,const MemoryDesc*));HIP_FN(hipExternalMemoryGetMappedBuffer,(void**,Handle,const BufferDesc*));HIP_FN(hipDestroyExternalMemory,(Handle));
 HIP_FN(hipImportExternalSemaphore,(Handle*,const SemaphoreDesc*));HIP_FN(hipSignalExternalSemaphoresAsync,(const Handle*,const SignalParams*,unsigned,Handle));HIP_FN(hipWaitExternalSemaphoresAsync,(const Handle*,const WaitParams*,unsigned,Handle));HIP_FN(hipDestroyExternalSemaphore,(Handle));
 // Optional graph API, loaded only when requested.
 HIP_FN(hipStreamBeginCapture,(Handle,int));HIP_FN(hipStreamEndCapture,(Handle,Handle*));
 HIP_FN(hipGraphInstantiate,(Handle*,Handle,Handle*,char*,size_t));HIP_FN(hipGraphLaunch,(Handle,Handle));
 HIP_FN(hipGraphDestroy,(Handle));HIP_FN(hipGraphExecDestroy,(Handle));
 // Optional VMM API (EnableVmm): address reservation + physical chunks mapped at chosen offsets.
 HIP_FN(hipMemAddressReserve,(void**,size_t,size_t,void*,unsigned long long));HIP_FN(hipMemAddressFree,(void*,size_t));HIP_FN(hipMemCreate,(void**,size_t,const MemAllocationProp*,unsigned long long));HIP_FN(hipMemRelease,(void*));HIP_FN(hipMemMap,(void*,size_t,size_t,void*,unsigned long long));HIP_FN(hipMemUnmap,(void*,size_t));HIP_FN(hipMemSetAccess,(void*,size_t,const MemAccessDesc*,size_t));HIP_FN(hipMemGetAllocationGranularity,(size_t*,const MemAllocationProp*,unsigned));
 HIP_FN(hipModuleLoad,(Handle*,const char*));HIP_FN(hipModuleGetFunction,(Handle*,Handle,const char*));HIP_FN(hipModuleLaunchKernel,(Handle,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,Handle,void**,void**));HIP_FN(hipModuleUnload,(Handle));
 #undef HIP_FN
 using ErrorNameFn=const char*(*)(int);ErrorNameFn hipGetErrorName{};
 template<class T>void Load(T&f,const char*n){f=reinterpret_cast<T>(GetProcAddress(dll,n));if(!f)throw std::runtime_error(std::string("missing HIP export ")+n);}
 explicit Api(unsigned version=7){const wchar_t*name=version==6?L"amdhip64_6.dll":L"amdhip64_7.dll";dll=LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!dll)throw std::runtime_error("HIP runtime not found in System32");
 #define LOAD(name) Load(name,#name)
 LOAD(hipGetDevicePropertiesR0600);LOAD(hipModuleLoadData);LOAD(hipEventCreate);LOAD(hipEventRecord);LOAD(hipEventElapsedTime);LOAD(hipEventDestroy);LOAD(hipEventSynchronize);LOAD(hipHostMalloc);LOAD(hipInit);LOAD(hipRuntimeGetVersion);LOAD(hipGetDeviceCount);LOAD(hipDeviceGetName);LOAD(hipSetDevice);LOAD(hipMemGetInfo);LOAD(hipMalloc);LOAD(hipFree);LOAD(hipMemcpy);LOAD(hipMemcpyAsync);LOAD(hipMemsetAsync);LOAD(hipDeviceSynchronize);LOAD(hipStreamCreate);LOAD(hipStreamSynchronize);LOAD(hipStreamDestroy);LOAD(hipImportExternalMemory);LOAD(hipExternalMemoryGetMappedBuffer);LOAD(hipDestroyExternalMemory);LOAD(hipImportExternalSemaphore);LOAD(hipSignalExternalSemaphoresAsync);LOAD(hipWaitExternalSemaphoresAsync);LOAD(hipDestroyExternalSemaphore);LOAD(hipModuleLoad);LOAD(hipModuleGetFunction);LOAD(hipModuleLaunchKernel);LOAD(hipModuleUnload);LOAD(hipGetErrorName);
 #undef LOAD
 }
 DevicePropertiesR0600 Properties(int device){DevicePropertiesR0600 p{};Check(hipGetDevicePropertiesR0600(&p,device),"device properties");return p;}
 /* DLSS5_STYLE (2026-10-01): when >=0, every module that defines the device constant `dlss5_style_feature` (preprocess feature 6,
    Style/128; modules default to 1/128) gets this value right after loading. -1 = leave the module default (no extra HIP calls). */
 float style_feature=-1;
 using ModuleGetGlobalFn=int(*)(void**,size_t*,Handle,const char*);ModuleGetGlobalFn hipModuleGetGlobal{};
 int LoadModule(Handle*module,const char*path){
  std::ifstream f(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
  if(!f)throw std::runtime_error(std::string("module file missing: ")+path);
  auto n=f.tellg();if(n<=0)throw std::runtime_error("empty module");std::vector<char>bytes(static_cast<size_t>(n));f.seekg(0);if(!f.read(bytes.data(),n))throw std::runtime_error("module read failed");
  // Publish ownership only after optional module initialization succeeds.
  *module=nullptr;Handle loaded=nullptr;
  int r=hipModuleLoadData(&loaded,bytes.data());
  if(r)return r;
  try{
   if(style_feature>=0){
    if(!hipModuleGetGlobal)Load(hipModuleGetGlobal,"hipModuleGetGlobal");
    void*p=nullptr;size_t s=0;if(!hipModuleGetGlobal(&p,&s,loaded,"dlss5_style_feature")&&p&&s==sizeof(float))Check(hipMemcpy(p,&style_feature,sizeof(float),1),"DLSS5_STYLE");
   }
  }catch(...){hipModuleUnload(loaded);throw;}
  *module=loaded;
  return 0;
 }
 void EnableVmm(){Load(hipMemAddressReserve,"hipMemAddressReserve");Load(hipMemAddressFree,"hipMemAddressFree");Load(hipMemCreate,"hipMemCreate");Load(hipMemRelease,"hipMemRelease");Load(hipMemMap,"hipMemMap");Load(hipMemUnmap,"hipMemUnmap");Load(hipMemSetAccess,"hipMemSetAccess");Load(hipMemGetAllocationGranularity,"hipMemGetAllocationGranularity");}
 void EnableGraphs(){Load(hipStreamBeginCapture,"hipStreamBeginCapture");Load(hipStreamEndCapture,"hipStreamEndCapture");Load(hipGraphInstantiate,"hipGraphInstantiate");Load(hipGraphLaunch,"hipGraphLaunch");Load(hipGraphDestroy,"hipGraphDestroy");Load(hipGraphExecDestroy,"hipGraphExecDestroy");}
 void Check(int result,const char*what){if(result)throw std::runtime_error(std::string(what)+": "+hipGetErrorName(result)+" ("+std::to_string(result)+")");}
 // Keep runtime loaded until process teardown: driver-owned workers may outlive probe objects.
};
}
