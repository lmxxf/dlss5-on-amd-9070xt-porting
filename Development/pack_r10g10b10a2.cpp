#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
int wmain(int ac,wchar_t**av){if(ac!=3){std::fwprintf(stderr,L"usage: %ls input-rgba.f32 output-r10.bin\n",av[0]);return 2;}constexpr uint64_t P=3840ull*2160;std::ifstream f(av[1],std::ios::binary|std::ios::ate);if(!f)return 2;uint64_t bytes=(uint64_t)f.tellg();if(bytes!=P*3*4&&bytes!=P*4*4)return 2;uint64_t channels=bytes/(P*4);f.seekg(0);std::vector<float>in(bytes/4);f.read((char*)in.data(),bytes);std::vector<uint32_t>out(P);for(uint64_t p=0;p<P;p++){uint32_t q[3];for(int c=0;c<3;c++){float x=std::clamp(in[p*channels+c],0.f,1.f);q[c]=(uint32_t)std::nearbyint(x*1023.f);}out[p]=q[0]|(q[1]<<10)|(q[2]<<20)|(3u<<30);}std::ofstream g(av[2],std::ios::binary);g.write((char*)out.data(),out.size()*4);std::printf("bytes=%llu channels=%llu\n",(unsigned long long)out.size()*4,(unsigned long long)channels);return g?0:2;}
