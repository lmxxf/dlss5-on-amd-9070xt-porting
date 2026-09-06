#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
// Compile-only check: creates no GPU device and modifies no game state.
int wmain(int argc,wchar_t**argv){
 if(argc!=2)return 2;
 ID3DBlob*code=nullptr,*errors=nullptr;
 HRESULT hr=D3DCompileFromFile(argv[1],nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,
                              "main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
 if(errors){std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}
 if(FAILED(hr)){std::fprintf(stderr,"compile HRESULT=0x%08x\n",unsigned(hr));return 1;}
 std::printf("compile_only=pass bytecode_bytes=%zu; GPU/texture verification pending\n",code->GetBufferSize());
 code->Release();return 0;
}
