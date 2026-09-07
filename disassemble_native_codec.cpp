#include <windows.h>
#include <d3dcompiler.h>
#include <fstream>
#include <vector>
#include <cstdio>
int wmain(int argc,wchar_t**argv){
 if(argc!=3)return 2;
 std::ifstream in(argv[1],std::ios::binary|std::ios::ate);
 if(!in)return 3;auto size=in.tellg();if(size<=0||size>1024*1024)return 4;
 std::vector<char>data(static_cast<size_t>(size));in.seekg(0);
 if(!in.read(data.data(),size))return 5;
 ID3DBlob*result=nullptr;HRESULT hr=D3DDisassemble(data.data(),data.size(),0,nullptr,&result);
 if(FAILED(hr)){std::fprintf(stderr,"D3DDisassemble=%08x\n",unsigned(hr));return 6;}
 std::ofstream out(argv[2],std::ios::binary);
 bool ok=bool(out.write(static_cast<const char*>(result->GetBufferPointer()),result->GetBufferSize()));
 result->Release();return ok?0:7;
}
