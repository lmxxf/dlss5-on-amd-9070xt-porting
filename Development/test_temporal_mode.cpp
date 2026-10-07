#include "../src/native_temporal_mode.h"
#include <cassert>
#include <iostream>
int main(){assert(NativeParseTemporalMode({},false)==NativeTemporalMode::Off);assert(NativeParseTemporalMode({},true)==NativeTemporalMode::FastHistory);for(bool old:{false,true}){assert(NativeParseTemporalMode(std::string("0"),old)==NativeTemporalMode::Off);assert(NativeParseTemporalMode(std::string("1"),old)==NativeTemporalMode::FastHistory);assert(NativeParseTemporalMode(std::string("2"),old)==NativeTemporalMode::LowFrequency);}for(auto s:{"","3","true"," 1"}){bool fail=false;try{NativeParseTemporalMode(std::string(s),true);}catch(...){fail=true;}assert(fail);}std::cout<<"PASS selector explicit presence/legacy mapping/invalid rejection\n";}
