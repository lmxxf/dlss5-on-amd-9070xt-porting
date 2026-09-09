#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
int wmain(int argc,wchar_t**argv){if(argc!=5)return 2;unsigned long long off=_wcstoui64(argv[3],nullptr,0),count=_wcstoui64(argv[4],nullptr,0);FILE*in=_wfopen(argv[1],L"rb"),*out=_wfopen(argv[2],L"wb");if(!in||!out||_fseeki64(in,off,SEEK_SET))return 3;std::vector<unsigned char>b(1<<20);while(count){size_t n=(size_t)(count<b.size()?count:b.size());if(fread(b.data(),1,n,in)!=n||fwrite(b.data(),1,n,out)!=n)return 4;count-=n;}return 0;}
