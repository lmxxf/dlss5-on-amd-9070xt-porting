#define INTERNAL_COPY_LOG LR"(D:\DLSSNR-Lab\logs\native-internal-copy-unit.txt)"
#include "native_internal_copy_contract.cpp"
static unsigned forwarded=0;
static int __cdecl fake(void*a,void*b,void*c,void*d){++forwarded;return a==reinterpret_cast<void*>(1)&&b==reinterpret_cast<void*>(2)&&c==reinterpret_cast<void*>(3)&&d==reinterpret_cast<void*>(4)?17:-5;}
int main(){original=&fake;
 if(copy(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4))!=17)return 1;
 if(copy(nullptr,nullptr,nullptr,nullptr)!=-5||forwarded!=2)return 2;
 puts("internal copy wrapper forwards arguments/status; no live hook acceptance");return 0;}
