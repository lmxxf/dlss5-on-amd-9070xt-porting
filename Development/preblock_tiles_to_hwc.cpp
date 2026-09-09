#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <tuple>
#include <vector>
template<class T>static std::vector<T>rd(const wchar_t*p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)ExitProcess(2);size_t n=(size_t)f.tellg();if(n%sizeof(T))ExitProcess(2);f.seekg(0);std::vector<T>b(n/sizeof(T));f.read((char*)b.data(),n);return b;}
static uint8_t enc(float x){if(x==0)return 0;uint8_t sign=x<0?0x80:0;float a=std::fabs(x);int e=(int)std::floor(std::log2(std::max(a,std::ldexp(1.f,-9))));if(e<-6){int m=(int)std::nearbyint(a*512);return sign|(uint8_t)std::clamp(m,0,7);}int ee=std::clamp(e+7,1,15),m=(int)std::nearbyint((a/std::ldexp(1.f,e)-1)*8);if(m>=8){m=0;ee=std::min(ee+1,15);}m=std::clamp(m,0,7);if(ee==15&&m>6)m=6;return sign|(uint8_t)(ee<<3)|m;}
static float dec(uint8_t v){float s=v&128?-1.f:1.f;int e=(v>>3)&15,m=v&7;if(!e)return s*(m/8.f)*std::ldexp(1.f,-6);if(e==15&&m==7)return s*448.f;return s*(1.f+m/8.f)*std::ldexp(1.f,e-7);}
int wmain(int ac,wchar_t**av){if(ac!=4){std::fwprintf(stderr,L"usage: %ls input.f32 permutation.i32 output.f32\n",av[0]);return 2;}constexpr uint32_t H=1088,W=1920,C=32,TR=136,TC=240,TILES=TR*TC;auto in=rd<float>(av[1]);auto perm=rd<int32_t>(av[2]);if(in.size()!=uint64_t(TILES)*2048||perm.size()!=4096)return 2;uint16_t maps[4][512]{};const int src[6]={4,0,1,3,2,5};for(int qy=0;qy<2;qy++)for(int qx=0;qx<2;qx++){std::vector<std::tuple<int,int>>e;for(int ly=0;ly<4;ly++)for(int lx=0;lx<4;lx++){int x=qx*4+lx,y=qy*4+ly,bits[6]={x&1,(x>>1)&1,(x>>2)&1,y&1,(y>>1)&1,(y>>2)&1},token=0;for(int b=0;b<6;b++)token|=bits[src[b]]<<b;for(int c=0;c<C;c++)e.emplace_back(perm[token*64+c],(ly*4+lx)*C+c);}std::sort(e.begin(),e.end());for(int rank=0;rank<512;rank++)maps[qy*2+qx][std::get<1>(e[rank])]=(uint16_t)rank;}std::vector<float>out(uint64_t(H)*W*C);uint8_t tile[2048];for(uint32_t ty=0;ty<TR;ty++)for(uint32_t tx=0;tx<TC;tx++){const float*p=&in[(uint64_t(ty)*TC+tx)*2048];for(int i=0;i<2048;i++)tile[i]=enc(p[i]);for(int qy=0;qy<2;qy++)for(int qx=0;qx<2;qx++){int q=qy*2+qx;for(int ly=0;ly<4;ly++)for(int lx=0;lx<4;lx++)for(int c=0;c<C;c++){uint64_t y=ty*8+qy*4+ly,x=tx*8+qx*4+lx;out[(y*W+x)*C+c]=dec(tile[q*512+maps[q][(ly*4+lx)*C+c]]);}}}std::ofstream f(av[3],std::ios::binary);f.write((char*)out.data(),out.size()*4);std::printf("shape=%ux%ux%u bytes=%llu\n",H,W,C,(unsigned long long)out.size()*4);return f?0:2;}
