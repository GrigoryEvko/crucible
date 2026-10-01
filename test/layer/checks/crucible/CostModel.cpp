// The compile-time checks of crucible/CostModel.h.

#include <crucible/CostModel.h>

namespace crucible {

static_assert(sizeof(ValidRegsPerThread) == sizeof(uint16_t),
              "ValidRegsPerThread must be the same size as the value it wraps");
static_assert(sizeof(ValidWarpSize) == sizeof(uint16_t), "ValidWarpSize must be the same size as the value it wraps");
static_assert(sizeof(ValidUtilization) == sizeof(float),
              "ValidUtilization must be the same size as the value it wraps");

}  // namespace crucible
