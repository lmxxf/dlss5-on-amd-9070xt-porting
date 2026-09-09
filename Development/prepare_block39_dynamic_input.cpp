#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
static std::vector<float>rd(const wchar_t*p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)ExitProcess(2);size_t n=(size_t)f.tellg();if(n%4)ExitProcess(2);f.seekg(0);std::vector<float>b(n/4);f.read((char*)b.data(),n);return b;}
int wmain(int ac,wchar_t**av){if(ac!=4){std::fwprintf(stderr,L"usage: %ls block38.f32 block30-body.f32 output.f32\n",av[0]);return 2;}constexpr uint64_t MH=36,MW=60,MC=1024,H=68,W=120,SC=512,OC=1536;auto main=rd(av[1]),skip=rd(av[2]);if(main.size()!=MH*MW*MC||skip.size()!=H*W*SC)return 2;std::vector<float>out(H*W*OC);for(uint64_t y=0;y<H;y++)for(uint64_t x=0;x<W;x++){const float*m=&main[((y/2)*MW+x/2)*MC],*s=&skip[(y*W+x)*SC];float*o=&out[(y*W+x)*OC];for(uint64_t c=0;c<MC;c++)o[c]=m[c];for(uint64_t c=0;c<SC;c++)o[MC+c]=s[c];}std::ofstream f(av[3],std::ios::binary);f.write((char*)out.data(),out.size()*4);std::printf("shape=%llux%llux%llu bytes=%llu\n",(unsigned long long)H,(unsigned long long)W,(unsigned long long)OC,(unsigned long long)out.size()*4);return f?0:2;}
