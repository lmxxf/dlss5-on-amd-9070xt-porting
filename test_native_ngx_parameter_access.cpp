#include <cstdio>
#include <cstring>
#include "native_ngx_parameter_access.h"
static const NVSDK_NGX_Parameter*expected;
static NVSDK_NGX_Result NVSDK_CONV resource(const NVSDK_NGX_Parameter*p,const char*k,ID3D12Resource**v){if(p!=expected||std::strcmp(k,"Color"))return NVSDK_NGX_Result_Fail;*v=reinterpret_cast<ID3D12Resource*>(0x12340);return NVSDK_NGX_Result_Success;}
static NVSDK_NGX_Result NVSDK_CONV number(const NVSDK_NGX_Parameter*p,const char*k,unsigned*v){if(p!=expected||std::strcmp(k,"Width"))return NVSDK_NGX_Result_Fail;*v=1920;return NVSDK_NGX_Result_Success;}
int main(){void*slots[17]{};slots[9]=reinterpret_cast<void*>(&resource);slots[12]=reinterpret_cast<void*>(&number);void**object=slots;expected=reinterpret_cast<const NVSDK_NGX_Parameter*>(&object);ID3D12Resource*r=nullptr;unsigned w=0;
 if(NativeNgxGetResource(expected,"Color",&r)!=NVSDK_NGX_Result_Success||r!=reinterpret_cast<ID3D12Resource*>(0x12340)||NativeNgxGetUInt(expected,"Width",&w)!=NVSDK_NGX_Result_Success||w!=1920)return 1;
 std::puts("MS-layout mock: resource slot9 and uint slot12 pass; live NGX not yet verified");return 0;}
