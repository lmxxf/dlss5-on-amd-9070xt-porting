#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
enum class NativeFrameInputCheck { valid, wrong_size, nonfinite, black };
inline bool NativeFrameRequestValid(uint32_t current_pid,uint32_t request_pid,uint32_t request,uint32_t last){
 return current_pid&&current_pid==request_pid&&request>last&&request<=1000000;
}
inline NativeFrameInputCheck CheckNativeFrameInput(const std::vector<unsigned char>&bytes){
 if(bytes.size()!=1920ull*1080*8)return NativeFrameInputCheck::wrong_size;
 bool rgb=false;
 for(size_t i=0;i<bytes.size();i+=2){uint16_t h;std::memcpy(&h,bytes.data()+i,2);
  if((h&0x7c00)==0x7c00)return NativeFrameInputCheck::nonfinite;
  if((i/2)%4!=3&&(h&0x7fff))rgb=true;
 }
 return rgb?NativeFrameInputCheck::valid:NativeFrameInputCheck::black;
}
