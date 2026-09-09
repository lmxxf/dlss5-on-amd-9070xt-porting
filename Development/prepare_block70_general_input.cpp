#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
static std::vector<float>rd(const wchar_t*p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)ExitProcess(2);size_t n=(size_t)f.tellg();if(n%4)ExitProcess(2);f.seekg(0);std::vector<float>b(n/4);f.read((char*)b.data(),n);return b;}
int wmain(int ac,wchar_t**av){if(ac!=4){std::fwprintf(stderr,L"usage: %ls main.f32 skip.f32 output.f32\n",av[0]);return 2;}constexpr uint64_t H=1088,W=1920,C=32,ACTIVE=1080,TILES=270*480;auto main=rd(av[1]),skip=rd(av[2]);if(main.size()!=H*W*C||skip.size()!=H*W*C)return 2;std::vector<float>out(TILES*1024);for(uint64_t y=0;y<ACTIVE;y++)for(uint64_t x=0;x<W;x++){uint64_t tile=(y/4)*480+x/4,local=((y&3)*4+(x&3))*C,src=(y*W+x)*C,dst=tile*1024+local;for(uint64_t c=0;c<C;c++){out[dst+c]=main[src+c];out[dst+512+c]=skip[src+c];}}std::ofstream f(av[3],std::ios::binary);f.write((char*)out.data(),out.size()*4);std::printf("tiles=%llu floats=%llu bytes=%llu\n",(unsigned long long)TILES,(unsigned long long)out.size(),(unsigned long long)out.size()*4);return f?0:2;}
