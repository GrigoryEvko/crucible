// The compile-time checks of crucible/CostModel.h.

#include <crucible/CostModel.h>

#include <concepts>
#include <cstdint>

namespace crucible {

static_assert(sizeof(ValidRegsPerThread) == sizeof(uint16_t),
              "ValidRegsPerThread must be the same size as the value it wraps");
static_assert(sizeof(ValidWarpSize) == sizeof(uint16_t), "ValidWarpSize must be the same size as the value it wraps");
static_assert(sizeof(ValidUtilization) == sizeof(float),
              "ValidUtilization must be the same size as the value it wraps");

// The ridge point of the B200 preset is 1.125 * 10^15 flops per second over
// 8 * 10^12 bytes per second: 140.625 flops per byte, bit for bit.
static_assert(::fixy::bit_cast<std::uint64_t>(blackwell_b200().compute_ridge_point(ScalarType::Half).raw())
              == ::fixy::bit_cast<std::uint64_t>(140.625));

// A bandwidth and a peak do not compare, and a count of flops over a byte
// count is an intensity.
static_assert(!std::equality_comparable_with<ByteRate, FlopRate>);
static_assert(std::same_as<decltype(divide(FlopRate{1.0}, ByteRate{1.0})), FlopsPerByte>);
static_assert(std::same_as<decltype(blackwell_b200().compute_ridge_point(ScalarType::Half)), FlopsPerByte>);

}  // namespace crucible
