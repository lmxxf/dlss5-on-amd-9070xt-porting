#pragma once
#include <optional>
#include <string>
#include <stdexcept>
enum class NativeTemporalMode : unsigned { Off=0, FastHistory=1, LowFrequency=2 };
inline NativeTemporalMode NativeParseTemporalMode(const std::optional<std::string>&mode,bool legacyFast){
 if(!mode)return legacyFast?NativeTemporalMode::FastHistory:NativeTemporalMode::Off;
 if(*mode=="0")return NativeTemporalMode::Off;if(*mode=="1")return NativeTemporalMode::FastHistory;if(*mode=="2")return NativeTemporalMode::LowFrequency;
 throw std::runtime_error("DLSS5_TEMPORAL_MODE must be 0, 1 or 2; restart required");
}

struct NativeTemporalAdmission {bool motion_read,metadata,reset;};
inline NativeTemporalAdmission NativeAdmitTemporalSource(NativeTemporalMode mode,bool reference,bool unjittered,bool valid,bool rgFloat,bool sourceReset){bool read=(reference||mode!=NativeTemporalMode::Off)&&unjittered&&valid&&rgFloat;bool meta=read||(mode!=NativeTemporalMode::Off&&valid);return {read,meta,meta?sourceReset:true};}
