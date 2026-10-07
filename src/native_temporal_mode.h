#pragma once
#include <optional>
#include <string>
#include <stdexcept>
#include <cstdlib>
#include <cmath>
enum class NativeTemporalMode : unsigned { Off=0, FastHistory=1, LowFrequency=2, Enhance=3 };
inline NativeTemporalMode NativeParseTemporalMode(const std::optional<std::string>&mode,bool legacyFast){
 if(!mode)return legacyFast?NativeTemporalMode::FastHistory:NativeTemporalMode::Off;
 if(*mode=="0")return NativeTemporalMode::Off;if(*mode=="1")return NativeTemporalMode::FastHistory;if(*mode=="2")return NativeTemporalMode::LowFrequency;if(*mode=="3")return NativeTemporalMode::Enhance;
 throw std::runtime_error("DLSS5_TEMPORAL_MODE must be 0, 1, 2 or 3; restart required");
}

struct NativeTemporalAdmission {bool motion_read,metadata,reset;};
inline NativeTemporalAdmission NativeAdmitTemporalSource(NativeTemporalMode mode,bool reference,bool unjittered,bool valid,bool rgFloat,bool sourceReset){bool read=(reference||mode!=NativeTemporalMode::Off)&&unjittered&&valid&&rgFloat;bool meta=read||(mode!=NativeTemporalMode::Off&&valid);return {read,meta,meta?sourceReset:true};}

inline bool NativeOutputTemporalMode(NativeTemporalMode mode){return mode==NativeTemporalMode::LowFrequency||mode==NativeTemporalMode::Enhance;}
inline float NativeParseTemporalEnhancementStrength(const std::optional<std::string>&value){
 if(!value)return 1.f;char*end{};float result=std::strtof(value->c_str(),&end);
 if(value->empty()||end!=value->c_str()+value->size()||!std::isfinite(result)||result<0||result>2)throw std::runtime_error("DLSS5_TEMPORAL_ENHANCE_STRENGTH must be finite 0..2; restart required");
 return result;
}
