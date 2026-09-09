#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <algorithm>

inline HMODULE runtime_bundle_module=nullptr;
inline bool runtime_bundle_present(){return runtime_bundle_module&&FindResourceW(runtime_bundle_module,MAKEINTRESOURCEW(297),MAKEINTRESOURCEW(10));}
inline std::wstring runtime_bundle_key(const wchar_t *path){
    std::wstring key=path;
    const wchar_t prefix[]=L"D:\\DLSSNR-Lab\\";
    if(key.size()>=wcslen(prefix)&&_wcsnicmp(key.c_str(),prefix,wcslen(prefix))==0)key.erase(0,wcslen(prefix));
    std::replace(key.begin(),key.end(),L'\\',L'/');return key;
}
struct RuntimeBundle {
    std::map<std::wstring,std::pair<const unsigned char*,size_t>> files;
    bool valid=false;
    RuntimeBundle(){
        HRSRC resource=FindResourceW(runtime_bundle_module,MAKEINTRESOURCEW(297),MAKEINTRESOURCEW(10));
        if(!resource)return;
        auto *p=static_cast<const unsigned char*>(LockResource(LoadResource(runtime_bundle_module,resource)));
        size_t remaining=SizeofResource(runtime_bundle_module,resource);
        if(!p||remaining<12||std::memcmp(p,"DLSS5PK1",8))return;
        uint32_t count;std::memcpy(&count,p+8,4);p+=12;remaining-=12;if(count>10000)return;
        for(uint32_t i=0;i<count;i++){
            if(remaining<10)return;
            uint16_t length;uint64_t bytes;std::memcpy(&length,p,2);std::memcpy(&bytes,p+2,8);p+=10;remaining-=10;
            if(!length||length>1024||length>remaining)return;
            std::wstring name;for(UINT j=0;j<length;j++){if(!p[j]||p[j]>127)return;name.push_back(wchar_t(p[j]));}
            p+=length;remaining-=length;if(bytes>remaining)return;
            if(!files.emplace(name,std::make_pair(p,size_t(bytes))).second)return;
            p+=size_t(bytes);remaining-=size_t(bytes);
        }
        valid=remaining==0;
    }
};
inline const unsigned char *runtime_bundle_get(const wchar_t *path,size_t &bytes){
    static const RuntimeBundle bundle;
    if(!bundle.valid)return nullptr;
    auto it=bundle.files.find(runtime_bundle_key(path));if(it==bundle.files.end())return nullptr;
    bytes=it->second.second;return it->second.first;
}
