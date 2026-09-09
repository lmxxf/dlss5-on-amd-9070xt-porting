#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
int wmain(int ac,wchar_t**av){if(ac!=3){std::fwprintf(stderr,L"usage: %ls input-r10.bin output-rgba.f32\n",av[0]);return 2;}constexpr uint64_t P=3840ull*2160;std::ifstream f(av[1],std::ios::binary|std::ios::ate);if(!f||uint64_t(f.tellg())!=P*4)return 2;f.seekg(0);std::vector<uint32_t>in(P);f.read((char*)in.data(),P*4);std::vector<float>out(P*4);for(uint64_t p=0;p<P;p++){uint32_t v=in[p];out[p*4]=(float)(double(v&1023)/1023.0);out[p*4+1]=(float)(double((v>>10)&1023)/1023.0);out[p*4+2]=(float)(double((v>>20)&1023)/1023.0);out[p*4+3]=1.f;}std::ofstream g(av[2],std::ios::binary);g.write((char*)out.data(),out.size()*4);std::printf("shape=2160x3840x4 bytes=%llu\n",(unsigned long long)out.size()*4);return g?0:2;}
