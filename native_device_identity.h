#pragma once
#include <unknwn.h>
// Verified ReShade unwrap interface. Never equate devices by adapter LUID.
inline constexpr GUID NativeDeviceUnwrapId={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
inline IUnknown*NativeDeviceIdentity(IUnknown*object){
 if(!object)return nullptr;
 IUnknown*raw=nullptr;HRESULT hr=object->QueryInterface(NativeDeviceUnwrapId,reinterpret_cast<void**>(&raw));
 if(hr!=S_OK&&hr!=E_NOINTERFACE){if(raw)raw->Release();return nullptr;}
 if(hr==S_OK&&!raw)return nullptr;
 if(hr==E_NOINTERFACE&&raw){raw->Release();return nullptr;}
 IUnknown*identity=nullptr;
 HRESULT identity_hr=(raw?raw:object)->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity));
 if(raw)raw->Release();
 if(FAILED(identity_hr)){if(identity)identity->Release();return nullptr;}
 return identity; // Caller owns one reference, including native-only case.
}
inline bool NativeSameDevice(IUnknown*a,IUnknown*b){
 IUnknown*x=NativeDeviceIdentity(a),*y=NativeDeviceIdentity(b);
 bool same=x&&y&&x==y;if(x)x->Release();if(y)y->Release();return same;
}
