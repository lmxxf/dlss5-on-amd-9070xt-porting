#include <windows.h>
#include <d3dcompiler.h>
#include <fstream>
#include <cstdio>
int wmain(int argc,wchar_t**argv){
 if(argc!=3&&argc!=4)return 2;ID3DBlob*code=nullptr,*error=nullptr,*assembly=nullptr;
 const bool temporal=argc==4&&!wcscmp(argv[3],L"temporal");if(argc==4&&!temporal)return 2;
 D3D_SHADER_MACRO macros[]={{"POST_BASE_ONLY","0"},{"NORMALIZED_COORDINATES",temporal?"1":"0"},{nullptr,nullptr}};
 HRESULT hr=D3DCompileFromFile(argv[1],macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,temporal?"main":"finish","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
 if(FAILED(hr)){if(error)std::fwrite(error->GetBufferPointer(),1,error->GetBufferSize(),stderr);return 1;}
 hr=D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&assembly);if(FAILED(hr))return 1;
 std::ofstream f(argv[2],std::ios::binary);f.write((char*)assembly->GetBufferPointer(),assembly->GetBufferSize());
 assembly->Release();code->Release();if(error)error->Release();return f?0:1;
}
