#include "native_device_identity.h"
#include <cstdio>
#include <initializer_list>
struct Object: IUnknown {
 ULONG refs=1;Object*wrapped=nullptr;bool fail=false,empty=false;
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{
  *out=nullptr;
  if(IsEqualIID(id,NativeDeviceUnwrapId)){
   if(fail)return E_FAIL;if(empty)return S_OK;if(!wrapped)return E_NOINTERFACE;
   wrapped->AddRef();*out=static_cast<IUnknown*>(wrapped);return S_OK;
  }
  if(IsEqualIID(id,IID_IUnknown)){AddRef();*out=static_cast<IUnknown*>(this);return S_OK;}
  return E_NOINTERFACE;
 }
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release()override{return --refs;}
};
int main(){
 Object a,b,proxy,other,broken,empty;proxy.wrapped=&a;other.wrapped=&b;broken.fail=true;empty.empty=true;
 if(!NativeSameDevice(&a,&a)||!NativeSameDevice(&proxy,&a)||!NativeSameDevice(&a,&proxy))return 1;
 if(NativeSameDevice(&a,&b)||NativeSameDevice(&proxy,&other)||NativeSameDevice(nullptr,&a)||NativeSameDevice(&broken,&a)||NativeSameDevice(&empty,&a))return 2;
 for(auto*p:{&a,&b,&proxy,&other,&broken,&empty})if(p->refs!=1)return 3;
 puts("device identity: native/proxy equality, distinct/error rejection, reference balance pass");return 0;
}
