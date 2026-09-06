#pragma once
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
inline HRESULT CompileNativeShader(const std::wstring&path,const D3D_SHADER_MACRO*macros,const char*entry,ID3DBlob**code,ID3DBlob**errors){
 if(!code||!entry)return E_INVALIDARG;*code=nullptr;if(errors)*errors=nullptr;
 std::ifstream file(path.c_str(),std::ios::binary);if(!file)return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
 std::string source((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
 // Includes require dependency tracking: compile uncached rather than risk
 // treating an unchanged top-level file as an unchanged whole program.
 if(source.find("include")!=std::string::npos)return D3DCompileFromFile(path.c_str(),macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,"cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code,errors);
 std::string key=source;key.push_back('\0');key+=entry;key.push_back('\0');
 if(macros)for(auto*m=macros;m->Name;m++){key+=m->Name;key.push_back('\0');if(m->Definition)key+=m->Definition;key.push_back('\0');}
 auto&state=NativeShaderCache();std::lock_guard<std::mutex>lock(state.mutex);
 auto found=state.entries.find(key);if(found!=state.entries.end()){
  HRESULT hr=D3DCreateBlob(found->second.size(),code);if(FAILED(hr))return hr;
  std::memcpy((*code)->GetBufferPointer(),found->second.data(),found->second.size());state.hits++;return S_OK;
 }
 const bool progress=_wgetenv(L"DLSS5_SHADER_PROGRESS")!=nullptr;
 auto started=std::chrono::steady_clock::now();
 if(progress){std::fprintf(stderr,"shader_compile_begin index=%zu entry=%s path=%ls\n",state.compiles+1,entry,path.c_str());std::fflush(stderr);}
 HRESULT hr=D3DCompile(source.data(),source.size(),"native-standalone",macros,nullptr,entry,"cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code,errors);state.compiles++;
 if(progress){auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();std::fprintf(stderr,"shader_compile_end index=%zu ms=%lld hr=0x%08x\n",state.compiles,(long long)ms,unsigned(hr));std::fflush(stderr);}
 if(SUCCEEDED(hr)){auto*begin=static_cast<const unsigned char*>((*code)->GetBufferPointer());state.entries.emplace(std::move(key),std::vector<unsigned char>(begin,begin+(*code)->GetBufferSize()));}
 return hr;
}
