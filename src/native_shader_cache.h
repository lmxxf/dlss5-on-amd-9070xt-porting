#pragma once
#include <windows.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
// Process-local cache for standalone shaders. The key contains the exact
// source, entry and macros; resources, weights and root constants are never cached.
struct NativeShaderCacheState {
 std::mutex mutex;
 std::map<std::string,std::vector<unsigned char>> entries;
 size_t hits{},compiles{};
};
inline NativeShaderCacheState& NativeShaderCache(){static NativeShaderCacheState state;return state;}
// Snapshot the one flat dependency used by production neural shaders. Unknown
// includes are compiled through the old uncached path, never cached speculatively.
struct NativeHalfInclude final:ID3DInclude {
 std::string bytes;bool unknown{};
 HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE type,const char*name,const void*,const void**data,UINT*size)override{
  if(type!=D3D_INCLUDE_LOCAL||!name||std::strcmp(name,"native_half_square.hlsli")){unknown=true;return E_FAIL;}
  *data=bytes.data();*size=UINT(bytes.size());return S_OK;
 }
 HRESULT STDMETHODCALLTYPE Close(const void*)override{return S_OK;}
};
inline HRESULT CompileNativeShader(const std::wstring&path,const D3D_SHADER_MACRO*macros,const char*entry,ID3DBlob**code,ID3DBlob**errors){
 if(!code||!entry)return E_INVALIDARG;*code=nullptr;if(errors)*errors=nullptr;
 std::ifstream file(path.c_str(),std::ios::binary);if(!file)return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
 std::string source((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
 const bool has_include=source.find("include")!=std::string::npos;
 NativeHalfInclude dependency;bool snapshot=false;std::string source_name;
 if(has_include){
  auto slash=path.find_last_of(L"/\\");auto header=(slash==std::wstring::npos?L"":path.substr(0,slash+1))+L"native_half_square.hlsli";
  std::ifstream include_file(header.c_str(),std::ios::binary);
  if(include_file){dependency.bytes.assign(std::istreambuf_iterator<char>(include_file),{});snapshot=!dependency.bytes.empty()&&dependency.bytes.size()<1024*1024&&dependency.bytes.find("include")==std::string::npos;}
  int n=WideCharToMultiByte(CP_UTF8,0,path.data(),int(path.size()),nullptr,0,nullptr,nullptr);
  if(n>0){source_name.resize(n);WideCharToMultiByte(CP_UTF8,0,path.data(),int(path.size()),source_name.data(),n,nullptr,nullptr);}else snapshot=false;
 }
 if(has_include&&!snapshot){
  const bool progress=_wgetenv(L"DLSS5_SHADER_PROGRESS")!=nullptr;auto started=std::chrono::steady_clock::now();
  if(progress){std::fprintf(stderr,"shader_compile_begin uncached_include=1 entry=%s path=%ls\n",entry,path.c_str());std::fflush(stderr);}
  HRESULT hr=D3DCompileFromFile(path.c_str(),macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,"cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code,errors);
  if(progress){auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();std::fprintf(stderr,"shader_compile_end uncached_include=1 ms=%lld hr=0x%08x\n",(long long)ms,unsigned(hr));std::fflush(stderr);}
  return hr;
 }
 std::string key=source;key.push_back('\0');key+=entry;key.push_back('\0');
 if(snapshot){key+=source_name;key.push_back('\0');key+=dependency.bytes;key.push_back('\0');}
 if(macros)for(auto*m=macros;m->Name;m++){key+=m->Name;key.push_back('\0');if(m->Definition)key+=m->Definition;key.push_back('\0');}
 auto&state=NativeShaderCache();std::lock_guard<std::mutex>lock(state.mutex);
 auto found=state.entries.find(key);if(found!=state.entries.end()){
  HRESULT hr=D3DCreateBlob(found->second.size(),code);if(FAILED(hr))return hr;
  std::memcpy((*code)->GetBufferPointer(),found->second.data(),found->second.size());state.hits++;return S_OK;
 }
 /* On-disk copy of the process cache (DLSS5_SHADER_DISK_CACHE=0 disables): <shader dir>\shader-cache\<fnv1a-64 of the key>.dxbc.
    The key is the full source + entry + macros (+ the snapshotted include), so an edited source never hits a stale blob. The ViT
    QKV "normalize" entry alone costs fxc 2.5 s per process; with the cache the six runtime compiles are file reads (DevHistory 09-11). */
 const wchar_t*disk_flag=_wgetenv(L"DLSS5_SHADER_DISK_CACHE");const bool disk=!(disk_flag&&!wcscmp(disk_flag,L"0"));
 std::wstring disk_path;
 if(disk){
  unsigned long long h=1469598103934665603ull;for(unsigned char c:key){h^=c;h*=1099511628211ull;}
  auto slash=path.find_last_of(L"/\\");std::wstring cache_dir=(slash==std::wstring::npos?L"":path.substr(0,slash+1))+L"shader-cache";
  CreateDirectoryW(cache_dir.c_str(),nullptr);wchar_t name[32];swprintf(name,32,L"\\%016llx.dxbc",h);disk_path=cache_dir+name;
  std::ifstream cached(disk_path.c_str(),std::ios::binary|std::ios::ate);
  if(cached){auto n=cached.tellg();if(n>0){std::vector<unsigned char>bytes;bytes.resize((size_t)n);cached.seekg(0);if(cached.read(reinterpret_cast<char*>(bytes.data()),n)){
   HRESULT hr=D3DCreateBlob(bytes.size(),code);if(FAILED(hr))return hr;std::memcpy((*code)->GetBufferPointer(),bytes.data(),bytes.size());
   state.entries.emplace(key,std::move(bytes));state.hits++;return S_OK;}}}
 }
 const bool progress=_wgetenv(L"DLSS5_SHADER_PROGRESS")!=nullptr;
 auto started=std::chrono::steady_clock::now();
 if(progress){std::fprintf(stderr,"shader_compile_begin index=%zu entry=%s path=%ls\n",state.compiles+1,entry,path.c_str());std::fflush(stderr);}
 HRESULT hr=D3DCompile(source.data(),source.size(),snapshot?source_name.c_str():"native-standalone",macros,snapshot?&dependency:nullptr,entry,"cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code,errors);state.compiles++;
 if(dependency.unknown){
  if(*code){(*code)->Release();*code=nullptr;}if(errors&&*errors){(*errors)->Release();*errors=nullptr;}
  return D3DCompileFromFile(path.c_str(),macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,"cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code,errors);
 }
 if(progress){auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();std::fprintf(stderr,"shader_compile_end index=%zu ms=%lld hr=0x%08x\n",state.compiles,(long long)ms,unsigned(hr));std::fflush(stderr);}
 if(SUCCEEDED(hr)){auto*begin=static_cast<const unsigned char*>((*code)->GetBufferPointer());std::vector<unsigned char>bytes(begin,begin+(*code)->GetBufferSize());
  if(disk&&!dependency.unknown){std::ofstream out(disk_path.c_str(),std::ios::binary);if(out)out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
  state.entries.emplace(std::move(key),std::move(bytes));}
 return hr;
}
