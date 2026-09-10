#pragma once
#include <dxgi.h>
/* Typeless game textures (Rise of the Ronin's XeSS output is R16G16B16A16_TYPELESS, its velocity R16G16_TYPELESS): views use the float format. */
inline DXGI_FORMAT NativeViewFormat(DXGI_FORMAT f){switch(f){case DXGI_FORMAT_R16G16B16A16_TYPELESS:return DXGI_FORMAT_R16G16B16A16_UNORM;/* Ronin: LDR output, the game views it as UNORM (verified from a dump: UNORM decodes to the scene, FLOAT to noise) */case DXGI_FORMAT_R16G16_TYPELESS:return DXGI_FORMAT_R16G16_FLOAT;case DXGI_FORMAT_R32G32_TYPELESS:return DXGI_FORMAT_R32G32_FLOAT;case DXGI_FORMAT_R32G32B32A32_TYPELESS:return DXGI_FORMAT_R32G32B32A32_FLOAT;default:return f;}}
inline bool NativeIsRgba16Float(DXGI_FORMAT f){DXGI_FORMAT v=NativeViewFormat(f);return v==DXGI_FORMAT_R16G16B16A16_FLOAT||v==DXGI_FORMAT_R16G16B16A16_UNORM;}
/* Motion-vector sign relative to the FSR contract (+1: FSR/Stellar Blade UV units; -1: XeSS titles whose velocity scale is (-w,-h)). Set by the hook before the frame is created. */
inline float&NativeMotionSign(){static float s=1.f;return s;}
/* Lab root and weight loading for the game addon.
   Root: the folder DLSS5-AMD next to this DLL when it holds native-game-flags.txt (the distributed package),
   otherwise D:\DLSSNR-Lab (the development machine). Every lab path in the addon goes through NativeLabPath().
   Weights: NativeReadF32(path.f32) reads the f32 file, or path.f16 (IEEE half, expanded) when the f32 is absent --
   every network coefficient is an exact half, so the package ships halves at half the size. */
#include <windows.h>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <cstring>
inline const std::wstring&NativeLabRoot(){
 static std::wstring root;if(!root.empty())return root;
 HMODULE h=nullptr;wchar_t path[MAX_PATH]{};
 if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&NativeLabRoot),&h)&&GetModuleFileNameW(h,path,MAX_PATH)){
  std::wstring dir(path);size_t slash=dir.find_last_of(L"\\/");if(slash!=std::wstring::npos){dir.resize(slash);std::wstring local=dir+L"\\DLSS5-AMD";if(GetFileAttributesW((local+L"\\native-game-flags.txt").c_str())!=INVALID_FILE_ATTRIBUTES){root=local;return root;}}
 }
 root=L"D:\\DLSSNR-Lab";return root;
}
inline std::wstring NativeLabPath(const wchar_t*relative){std::wstring p=NativeLabRoot();p+=L"\\";p+=relative;return p;}
inline float NativeHalfToFloat(uint16_t h){uint32_t s=(h&0x8000u)<<16,e=(h>>10)&31u,m=h&1023u;uint32_t b;if(e==0){if(m==0)b=s;else{int sh=0;while(!(m&0x400u)){m<<=1;sh++;}m&=0x3ffu;b=s|((113u-sh)<<23)|(m<<13);}}else if(e==31)b=s|0x7f800000u|(m<<13);else b=s|((e+112u)<<23)|(m<<13);float f;std::memcpy(&f,&b,4);return f;}
inline std::vector<float>NativeReadF32(const std::wstring&path,const char*what){
 std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);
 if(f){auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error(std::string(what)+" size");std::vector<float>v(size_t(n)/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(v.data()),n))throw std::runtime_error(std::string(what)+" truncated");return v;}
 if(path.size()>4&&path.compare(path.size()-4,4,L".f32")==0){
  std::wstring half=path.substr(0,path.size()-4)+L".f16";std::ifstream g(half.c_str(),std::ios::binary|std::ios::ate);
  if(g){auto n=g.tellg();if(n<=0||size_t(n)%2)throw std::runtime_error(std::string(what)+" half size");std::vector<uint16_t>h(size_t(n)/2);g.seekg(0);if(!g.read(reinterpret_cast<char*>(h.data()),n))throw std::runtime_error(std::string(what)+" half truncated");std::vector<float>v(h.size());for(size_t i=0;i<h.size();i++)v[i]=NativeHalfToFloat(h[i]);return v;}
 }
 throw std::runtime_error(std::string(what)+" missing");
}
