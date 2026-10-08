#pragma once
#include <stdexcept>
// The reusable auxiliary interface supports MP1/2/3; this addon consumer does not.
namespace NativeFastHistoryPolicy {
inline void RequireSinglePass(bool enabled, unsigned passes) {
 if(enabled && passes != 1)
  throw std::runtime_error("Temporal modes 1/2/3 support MP1 only; set DLSS5_TEMPORAL_MODE=0 and restart before selecting multiple passes");
}
}
