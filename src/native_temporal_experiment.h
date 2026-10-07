#pragma once
#include "native_lab_paths.h"
#include <cmath>
// Opt-in temporary experiment. These keys require restart; no default history change.
inline bool NativeTemporalExperimentFlag(const wchar_t*name,const char*key){
 if(const wchar_t*v=_wgetenv(name))return !wcscmp(v,L"1");
 const std::string wanted=std::string(key)+"=1";for(const auto&line:NativeConfigFileLines())if(line==wanted)return true;return false;
}
inline bool NativeTemporalExperimentRequested(){return NativeTemporalExperimentFlag(L"DLSS5_TEMPORAL_HISTORY_EXPERIMENT","DLSS5_TEMPORAL_HISTORY_EXPERIMENT");}
inline bool NativeTemporalExperimentUnjittered(){return NativeTemporalExperimentFlag(L"DLSS5_TEMPORAL_MV_UNJITTERED","DLSS5_TEMPORAL_MV_UNJITTERED");}
inline bool NativeFastHistoryRequested(){return NativeTemporalExperimentFlag(L"DLSS5_FAST_HISTORY","DLSS5_FAST_HISTORY");}
struct NativeTemporalFrameMetadata {
 bool ffx_pre{};unsigned frame_id{},dispatch_flags{};
 float motion_scale[2]{},jitter[2]{},pre_exposure{1.f};
 bool Valid()const{return ffx_pre&&std::isfinite(motion_scale[0])&&std::isfinite(motion_scale[1])&&std::isfinite(jitter[0])&&std::isfinite(jitter[1])&&std::isfinite(pre_exposure)&&pre_exposure>0.f;}
};
