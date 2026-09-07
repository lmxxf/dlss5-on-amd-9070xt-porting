#include "native_shader_cache.h"
#include <stdexcept>
static void write(const std::wstring&p,const char*text){std::ofstream f(p.c_str(),std::ios::binary);if(!f.write(text,std::strlen(text)))throw std::runtime_error("fixture write");}
static std::vector<unsigned char> compile(const std::wstring&p,const D3D_SHADER_MACRO*m=nullptr,bool baseline=false,const char*entry="main"){
 ID3DBlob*b=nullptr,*e=nullptr;auto hr=baseline?D3DCompileFromFile(p.c_str(),m,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,"cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&e):CompileNativeShader(p,m,entry,&b,&e);
 if(e){std::fwrite(e->GetBufferPointer(),1,e->GetBufferSize(),stderr);e->Release();}if(FAILED(hr))throw std::runtime_error("compile failed");
 auto*data=static_cast<unsigned char*>(b->GetBufferPointer());std::vector<unsigned char>v(data,data+b->GetBufferSize());b->Release();return v;
}
int wmain(int argc,wchar_t**argv){try{
 NativeHalfInclude guard;const void*data=nullptr;UINT size=0;
 if(SUCCEEDED(guard.Open(D3D_INCLUDE_SYSTEM,"native_half_square.hlsli",nullptr,&data,&size))||!guard.unknown)throw std::runtime_error("system include must fall back");
 if(argc==3&&!wcscmp(argv[1],L"production")){
  for(const char*channels:{"64","128","256"})for(const char*entry:{"ffn","attention","projection"}){
   D3D_SHADER_MACRO m[]={{"CHANNELS",channels},{"RAW_OUTPUT","0"},{nullptr,nullptr}};
   auto a=compile(argv[2],m,false,entry),b=compile(argv[2],m,true,entry);if(a!=b)throw std::runtime_error("production bytecode mismatch");
   auto hits=NativeShaderCache().hits;if(compile(argv[2],m,false,entry)!=a||NativeShaderCache().hits!=hits+1)throw std::runtime_error("production cache miss");
   printf("channels=%s entry=%s bytecode_exact=1 cache_hit=1\n",channels,entry);fflush(stdout);
  }return 0;
 }
 if(argc!=2)return 2;std::wstring dir=argv[1];if(GetFileAttributesW(dir.c_str())!=INVALID_FILE_ATTRIBUTES||!CreateDirectoryW(dir.c_str(),nullptr))throw std::runtime_error("fresh fixture directory required");
 auto shader=dir+L"\\test.hlsl",header=dir+L"\\native_half_square.hlsli";
 write(shader,"#include \"native_half_square.hlsli\"\n#ifndef FACTOR\n#define FACTOR 1\n#endif\nRWStructuredBuffer<float> o:register(u0);\n[numthreads(1,1,1)]void main(){o[0]=value()*FACTOR;}\n");
 write(header,"float value(){return 1;}\n");auto a=compile(shader);if(a!=compile(shader,nullptr,true))throw std::runtime_error("baseline mismatch");
 auto&cache=NativeShaderCache();auto c=cache.compiles,h=cache.hits;
 if(a!=compile(shader)||cache.compiles!=c||cache.hits!=h+1)throw std::runtime_error("cache miss");
 write(header,"float value(){return 3;}\n");auto b=compile(shader);if(a==b||b!=compile(shader,nullptr,true)||cache.compiles!=c+1)throw std::runtime_error("dependency invalidation failed");
 D3D_SHADER_MACRO macros[]={{"FACTOR","2"},{nullptr,nullptr}};auto m=compile(shader,macros);if(m==b||m!=compile(shader,macros,true))throw std::runtime_error("macro key failed");
 write(shader,"#include \"other.hlsli\"\nRWStructuredBuffer<float> o:register(u0);[numthreads(1,1,1)]void main(){o[0]=value();}\n");write(dir+L"\\other.hlsli","float value(){return 4;}\n");auto x=compile(shader);if(x!=compile(shader,nullptr,true))throw std::runtime_error("fallback mismatch");
 write(dir+L"\\other.hlsli","float value(){return 5;}\n");auto y=compile(shader);if(x==y||y!=compile(shader,nullptr,true))throw std::runtime_error("unknown include was cached");
 puts("shader cache: byte-exact baseline, hit, dependency/macro invalidation, unknown-include fallback pass");return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
