#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
int wmain(int ac,wchar_t**av){if(ac!=7){std::fwprintf(stderr,L"usage: %ls input.f32 output.f32 in_rows out_rows width channels\n",av[0]);return 2;}uint64_t ih=_wtoi64(av[3]),oh=_wtoi64(av[4]),w=_wtoi64(av[5]),c=_wtoi64(av[6]);if(!ih||oh<ih||!w||!c)return 2;std::ifstream f(av[1],std::ios::binary|std::ios::ate);if(!f||uint64_t(f.tellg())!=ih*w*c*4)return 2;f.seekg(0);std::vector<float>out(oh*w*c,0);f.read((char*)out.data(),ih*w*c*4);std::ofstream g(av[2],std::ios::binary);g.write((char*)out.data(),out.size()*4);std::printf("shape=%llux%llux%llu bytes=%llu\n",(unsigned long long)oh,(unsigned long long)w,(unsigned long long)c,(unsigned long long)out.size()*4);return g?0:2;}
