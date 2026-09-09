#pragma once
#include <cstdint>
#include <stdexcept>
// Directly decoded original launch blobs; see native-runtime-parameters.json.
inline std::uint32_t NativeDecoderShift(std::uint32_t block){
 static constexpr std::uint32_t masks[]={
  0,3,1,2,0,3,1,2, //40..47
  0,3,1,2,0,3,1,2, //48..55
  1,2,0,3,1,2,     //56..61
  0,3,1,2,         //62..65
  0,3,1,2          //66..69
 };
 if(block<40||block>69)throw std::runtime_error("decoder shift block range");
 return masks[block-40];
}
