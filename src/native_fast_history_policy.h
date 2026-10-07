#pragma once
#include <stdexcept>
// The reusable auxiliary interface supports MP1/2/3; this addon consumer does not.
namespace NativeFastHistoryPolicy {
inline void RequireSinglePass(bool enabled, unsigned passes) {
 if(enabled && passes != 1)
  throw std::runtime_error("Fast History addon supports MP1 only; disable DLSS5_FAST_HISTORY and restart before selecting multiple passes");
}
}
