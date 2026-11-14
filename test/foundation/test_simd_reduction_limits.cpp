// Each reduction of foundation/Simd.h, at lanes that hold the limits of
// each lane type.  A fold of signed lanes in their own type overflows at a
// limit, and a signed overflow is undefined behavior.  The test builds with
// UBSan and -fno-sanitize-recover=all.  Most presets also build with
// -fno-strict-overflow, which defines a signed overflow as a wrap, so UBSan
// finds no overflow there.  The preset ubsan-strict removes that flag, and
// there an overflow ends the test.  Each result must also equal a scalar
// fold: the sum wraps modulo two to the width of the lane, and the other
// reductions cannot overflow.

#include <foundation/Platform.h>
#include <foundation/Simd.h>
#include <foundation/core/Number.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace simd = ::foundation::simd;
namespace fc = ::foundation::core;

// The value comes back through a call that no optimization looks into, so
// each reduction below runs on a run-time vector.
template <typename T>
[[nodiscard, gnu::noipa]] T opaque(T value) noexcept {
    return value;
}

// The pattern of the lanes of one vector: each lane at the greatest value,
// each lane at the least value, or the two in turn.
enum class LanePattern : std::uint8_t {
    greatest,
    least,
    alternating,
};

template <typename T>
[[nodiscard]] constexpr T lane_value(LanePattern pattern, int lane) noexcept {
    if (pattern == LanePattern::greatest) return fc::max_of<T>;
    if (pattern == LanePattern::least) return fc::min_of<T>;
    return lane % 2 == 0 ? fc::max_of<T> : fc::min_of<T>;
}

// The scalar folds of the first count lanes.  The sum adds in the unsigned
// type of the lane, so it wraps and has no overflow.
template <typename T>
struct Folds {
    T sum{};
    T bit_xor{};
    T bit_and{};
    T bit_or{};
    T least{};
    T greatest{};
};

template <typename T>
[[nodiscard]] Folds<T> fold(LanePattern pattern, int count) noexcept {
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned sum = 0;
    T bit_xor = 0;
    T bit_and = static_cast<T>(~T{0});
    T bit_or = 0;
    T least = lane_value<T>(pattern, 0);
    T greatest = lane_value<T>(pattern, 0);
    for (int lane = 0; lane < count; ++lane) {
        const T value = lane_value<T>(pattern, lane);
        sum = static_cast<Unsigned>(sum + static_cast<Unsigned>(value));
        bit_xor = static_cast<T>(bit_xor ^ value);
        bit_and = static_cast<T>(bit_and & value);
        bit_or = static_cast<T>(bit_or | value);
        least = value < least ? value : least;
        greatest = value > greatest ? value : greatest;
    }
    return {static_cast<T>(sum), bit_xor, bit_and, bit_or, least, greatest};
}

// Each reduction of a vector of the pattern, and the masked reductions of
// its first half, against the scalar folds.
template <typename T, int Lanes>
void check_reductions(LanePattern pattern) {
    using V = simd::vec<T, Lanes>;
    const LanePattern lanes = opaque(pattern);
    const V vector = opaque(V([lanes](auto lane) noexcept { return lane_value<T>(lanes, decltype(lane)::value); }));

    const Folds<T> all = fold<T>(pattern, Lanes);
    CRUCIBLE_FATAL_INVARIANT(simd::reduce_add(vector) == all.sum);
    CRUCIBLE_FATAL_INVARIANT(simd::reduce_xor(vector) == all.bit_xor);
    CRUCIBLE_FATAL_INVARIANT(simd::reduce_and(vector) == all.bit_and);
    CRUCIBLE_FATAL_INVARIANT(simd::reduce_or(vector) == all.bit_or);
    CRUCIBLE_FATAL_INVARIANT(simd::reduce_min(vector) == all.least);
    CRUCIBLE_FATAL_INVARIANT(simd::reduce_max(vector) == all.greatest);

    const Folds<T> half = fold<T>(pattern, Lanes / 2);
    const typename V::mask_type first_half = simd::prefix_mask<V>(opaque(Lanes / 2));
    CRUCIBLE_FATAL_INVARIANT(simd::reduce_add(vector, first_half) == half.sum);
    CRUCIBLE_FATAL_INVARIANT(simd::reduce_xor(vector, first_half) == half.bit_xor);
}

// Each pattern, for a vector of 64 bytes and for a vector of two lanes.
template <typename T>
void check_lane_type() {
    constexpr int wide = static_cast<int>(64 / sizeof(T));
    check_reductions<T, wide>(LanePattern::greatest);
    check_reductions<T, wide>(LanePattern::least);
    check_reductions<T, wide>(LanePattern::alternating);
    check_reductions<T, 2>(LanePattern::greatest);
    check_reductions<T, 2>(LanePattern::least);
    check_reductions<T, 2>(LanePattern::alternating);
}

}  // namespace

int main() {
    check_lane_type<std::int8_t>();
    check_lane_type<std::uint8_t>();
    check_lane_type<std::int16_t>();
    check_lane_type<std::uint16_t>();
    check_lane_type<std::int32_t>();
    check_lane_type<std::uint32_t>();
    check_lane_type<std::int64_t>();
    check_lane_type<std::uint64_t>();
    return 0;
}
