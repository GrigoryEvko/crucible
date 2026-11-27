// The compile-time checks of fixy/fp/Log.h.
//
// Each pin here is also a case of test/fixy/test_fp_elementary.cpp, which runs
// the code of each level, so the constant evaluation and the code give the
// same bits.

#include <fixy/fp/Log.h>

#include <foundation/core/Scalar.h>

#include <cstdint>

namespace fixy::fp::detail::log_self_test {

[[nodiscard]] consteval std::uint64_t bits_of(double value) noexcept {
    return ::foundation::core::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] consteval std::uint32_t bits_of(float value) noexcept {
    return ::foundation::core::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] consteval double double_of(std::uint64_t bits) noexcept {
    return ::foundation::core::bit_cast<double>(bits);
}

[[nodiscard]] consteval float float_of(std::uint32_t bits) noexcept {
    return ::foundation::core::bit_cast<float>(bits);
}

// The identity, the neighbors of 1 on the path near 1, 2 on the table path,
// the smallest subnormal through the normalization by integers, and the
// largest finite value.
static_assert(bits_of(::fixy::fp::log(1.0)) == 0x0000000000000000ULL, "log(1) must be exactly +0.");
static_assert(bits_of(::fixy::fp::log(double_of(0x3FF0000000000001ULL))) == 0x3CAFFFFFFFFFFFFFULL,
              "log of the next double above 1 must be the pinned value.");
static_assert(bits_of(::fixy::fp::log(2.0)) == 0x3FE62E42FEFA39EFULL, "log(2) must be the pinned ln2.");
static_assert(bits_of(::fixy::fp::log(double_of(0x0000000000000001ULL))) == 0xC0874385446D71C3ULL,
              "log of the smallest subnormal must be the pinned value.");
static_assert(bits_of(::fixy::fp::log(double_of(0x7FEFFFFFFFFFFFFFULL))) == 0x40862E42FEFA39EFULL,
              "log of the largest finite value must be the pinned value.");

// The slow path on its own: the hardest input of the list log.wc, an input
// near 1 with k2 = 1, and the smallest subnormal input.
static_assert(bits_of(log_accurate_(double_of(0x6DBFD15DAA6CE332ULL))) == 0x407FC12387D0632AULL,
              "the slow path of the hardest input of log.wc must be the correctly rounded value.");
static_assert(bits_of(log_accurate_(double_of(0x3FF0000800000001ULL))) == 0x3EDFFFF80006AAA8ULL,
              "the slow path above 1 + 2^-17 must be the correctly rounded value.");
static_assert(bits_of(log_accurate_(double_of(0x0000000000000001ULL))) == 0xC0874385446D71C3ULL,
              "the slow path of the smallest subnormal must be the correctly rounded value.");

// The tables of log(double): the two intervals next to 1 have r = 1 and
// -log(r) = 0, as does the entry k2 = 0.  So the path near 1 and the slow path
// near 1 add no table value.  The row of the interval 511 holds r/2 = 1/2, as
// each row below 512 does.
[[nodiscard]] consteval bool is_identity(LogEntry const& entry, double stored_inverse) noexcept {
    return bits_of(entry.inverse) == bits_of(stored_inverse) && bits_of(entry.high) == 0 && bits_of(entry.middle) == 0
        && bits_of(entry.low) == 0;
}
static_assert(is_identity(kLogFirst[kLogFirstOne], 0.5) && is_identity(kLogFirst[kLogFirstOne + 1], 1.0),
              "the intervals 511 and 512 must have r = 1 and -log(r) = 0.");
static_assert(is_identity(kLogSecond[kLogSecondCenter], 1.0), "the entry k2 = 0 must have r2 = 1 and -log(r2) = 0.");

// The rows below 512 hold r_i/2, in [1/2, 1), and the other rows hold r_i, in
// [1/2, 1] too, as r_i is near 1/y with y in [1, sqrt2).  So each stored inverse
// is in [1/2, 1].  O(363).
[[nodiscard]] consteval bool has_halved_low_rows() noexcept {
    for (LogEntry const& entry : kLogFirst) {
        if (entry.inverse < 0.5 || entry.inverse > 1.0) return false;
    }
    return true;
}
static_assert(has_halved_low_rows(), "each stored inverse must be in [1/2, 1].");
static_assert(sizeof(LogEntry) == 32 && alignof(LogEntry) == 32, "a row must fill half a line of the cache.");

// Each inverse r_i has 10 or fewer significant bits, and each l1 is a
// multiple of 2^-42.  So z = fma(r_i, y, -1) is exact, and e ln2_high + l1 is
// exact.  O(363).
[[nodiscard]] consteval bool has_short_inverses_and_l1() noexcept {
    for (LogEntry const& entry : kLogFirst) {
        std::uint64_t const inverse_fraction = bits_of(entry.inverse) & kLogFractionMask;
        if ((inverse_fraction & ((std::uint64_t{1} << 43) - 1)) != 0) return false;
        double const scaled = entry.high * 0x1p42;
        if (bits_of(scaled) != bits_of(static_cast<double>(static_cast<std::int64_t>(scaled)))) return false;
    }
    return true;
}
static_assert(has_short_inverses_and_l1(), "each r_i must have 10 or fewer bits and each l1 a multiple of 2^-42.");

// The float form: the identity, the smallest subnormal, the largest finite
// value, and one input of the list of midpoint inputs.
static_assert(bits_of(::fixy::fp::log(1.0f)) == 0x00000000u, "logf(1) must be exactly +0.");
static_assert(bits_of(::fixy::fp::log(float_of(0x00000001u))) == 0xC2CE8ED0u,
              "logf of the smallest subnormal must be the correctly rounded value.");
static_assert(bits_of(::fixy::fp::log(float_of(0x7F7FFFFFu))) == 0x42B17218u,
              "logf of the largest finite value must be the correctly rounded value.");
static_assert(bits_of(::fixy::fp::log(float_of(0x41178FEBu))) == 0x400FE5E7u,
              "logf of a midpoint input must take the result of the list.");

// The interval of the float tables that holds 1 has invc = 1 and logc = 0,
// and in the bits 1 is at its middle.  So the fast path adds no logc to r on
// each side of 1, and log(x) near 1 has no cancellation.
inline constexpr std::uint32_t kOneOffset = bits_of(1.0f) - kLogFloatOffset;
inline constexpr std::uint32_t kOneIndex = kOneOffset >> (23 - kLogFloatTableBits);
static_assert(bits_of(kLogFloatInvc[kOneIndex]) == bits_of(1.0), "the interval of 1 must have invc = 1.");
static_assert(bits_of(kLogFloatLogc[kOneIndex]) == bits_of(0.0), "the interval of 1 must have logc = +0.");
static_assert((kOneOffset & ((1u << (23 - kLogFloatTableBits)) - 1u)) == 1u << (22 - kLogFloatTableBits),
              "in the bits, 1 must be at the middle of its interval.");

}  // namespace fixy::fp::detail::log_self_test
