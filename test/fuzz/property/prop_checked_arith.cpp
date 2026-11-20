// ═══════════════════════════════════════════════════════════════════
// prop_checked_arith.cpp — multi-width fuzzer for the overflow-mode
// arithmetic of the Scalar family (foundation/core/Arithmetic.h).
//
// Each size, offset and capacity computation that must not silently wrap
// goes through checked_* / wrapping_* / saturating_*.  Each set takes an
// Integer, and it claims a correct result for each width and each sign.
// This fuzzer runs the operations at eight widths, signed and unsigned,
// where integer promotion, two's-complement truncation and 64-bit
// overflow boundaries behave differently.
//
// The oracle is an INDEPENDENT 128-bit computation: each operation is
// computed again in __int128 / unsigned __int128 (no overflow is possible
// for a T of 64 bits or less, because the product of two int64 is at most
// 2^126).  Then the result is compared with [T_min, T_max] or truncated
// to T.  That computation is different from the __builtin_*_overflow flag
// path of the family, so a divergence is a real defect.  For each
// (a, b, shift) over 8 widths, with a bias to {0, ±1, MIN, MAX}, it
// asserts:
//
//   checked_add/sub/mul  == wide result if it fits, else no value
//   checked_div/rem      == zero → no value, signed MIN/-1 handled, else exact
//   checked_neg/abs      (signed) == MIN → no value, else exact
//   checked_shl          == invalid shift → no value, else the wide product
//                           a * 2^shift if it fits, else no value
//   checked_shr          == invalid shift → no value, else the wide shift
//   wrapping_add/sub/mul == wide result truncated to T (mod 2^bits)
//   checked_*.expect     == the checked value (only on inputs that do not
//                           overflow: the end of the process cannot be
//                           fuzzed in one process)
//   saturating_add/sub/mul == wide result clamped to [T_min, T_max]
//   saturating_div       == wide quotient clamped to [T_min, T_max], for a
//                           divisor other than 0
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <foundation/core/Arithmetic.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace core = ::foundation::core;
using core::Option;
using crucible::fuzz::prop::Rng;

__extension__ using w_s = __int128;
__extension__ using w_u = unsigned __int128;

template <typename T>
using Wide = std::conditional_t<std::signed_integral<T>, w_s, w_u>;

template <typename T>
[[nodiscard]] constexpr bool fits(Wide<T> x) noexcept {
    return x >= static_cast<Wide<T>>(core::min_of<T>) && x <= static_cast<Wide<T>>(core::max_of<T>);
}

// The wide value as an Option of T: the value when T holds it, else none.
template <typename T>
[[nodiscard]] constexpr Option<T> option_of(bool is_held, Wide<T> x) noexcept {
    if (!is_held) return core::none;
    return Option<T>::some(static_cast<T>(x));
}

// True when the two results hold the same value, or are both empty.
template <typename T>
[[nodiscard]] constexpr bool agree(Option<T> got, Option<T> want) noexcept {
    if (got.is_some() != want.is_some()) return false;
    return static_cast<Option<T>&&>(got).value_or(T{0}) == static_cast<Option<T>&&>(want).value_or(T{0});
}

template <typename T>
struct Spec {
    T a = 0;
    T b = 0;
    int shift = 0;
    ::fixy::FixedArray<std::uint8_t, 4> pad{};
};

template <typename T>
[[nodiscard]] T gen_value(Rng& rng) noexcept {
    switch (rng.next_below(8u)) {
        case 0:
            return T{0};
        case 1:
            return T{1};
        case 2:
            return core::max_of<T>;
        case 3:
            return core::min_of<T>;
        case 4:
            return static_cast<T>(~T{0});  // -1 signed / max unsigned
        default:
            return static_cast<T>(rng.next64());
    }
}

template <typename T>
[[nodiscard]] int run_checked(::fixy::TextView const name, crucible::fuzz::prop::Config cfg) {
    constexpr int bits = core::bits_of<T>;
    return crucible::fuzz::prop::run(
        name, cfg,
        [](Rng& rng) noexcept -> Spec<T> {
            Spec<T> spec{};
            spec.a = gen_value<T>(rng);
            spec.b = gen_value<T>(rng);
            // shift ∈ [-2, bits+1] — covers negative, valid, and >= bits.
            spec.shift = static_cast<int>(rng.next_below(static_cast<std::uint32_t>(bits) + 4u)) - 2;
            return spec;
        },
        [](const Spec<T>& spec) noexcept -> bool {
            const T a = spec.a;
            const T b = spec.b;
            const Wide<T> wa = static_cast<Wide<T>>(a);
            const Wide<T> wb = static_cast<Wide<T>>(b);

            // ── checked add / sub / mul vs wide-fits oracle ──
            {
                const Wide<T> s = wa + wb;
                if (!agree(core::checked_add<T>(a, b), option_of<T>(fits<T>(s), s))) return false;
                if (fits<T>(s) && core::checked_add<T>(a, b).expect("the sum fits") != static_cast<T>(s)) return false;
                if (core::wrapping_add<T>(a, b) != static_cast<T>(s)) return false;
            }
            {
                const Wide<T> d = wa - wb;
                if (!agree(core::checked_sub<T>(a, b), option_of<T>(fits<T>(d), d))) return false;
                if (fits<T>(d) && core::checked_sub<T>(a, b).expect("the difference fits") != static_cast<T>(d)) {
                    return false;
                }
                if (core::wrapping_sub<T>(a, b) != static_cast<T>(d)) return false;
            }
            {
                const Wide<T> p = wa * wb;
                if (!agree(core::checked_mul<T>(a, b), option_of<T>(fits<T>(p), p))) return false;
                if (fits<T>(p) && core::checked_mul<T>(a, b).expect("the product fits") != static_cast<T>(p)) {
                    return false;
                }
                if (core::wrapping_mul<T>(a, b) != static_cast<T>(p)) return false;
            }

            // ── checked div / rem, saturating div ──
            {
                bool is_min_over_neg1 = false;
                if constexpr (std::signed_integral<T>) {
                    is_min_over_neg1 = (a == core::min_of<T> && b == static_cast<T>(-1));
                }
                if (b == T{0}) {
                    if (core::checked_div<T>(a, b).is_some() || core::checked_rem<T>(a, b).is_some()) return false;
                } else if (is_min_over_neg1) {
                    if (core::checked_div<T>(a, b).is_some()) return false;
                    if (!agree(core::checked_rem<T>(a, b), Option<T>::some(T{0}))) return false;
                    if (core::saturating_div<T>(a, b) != core::max_of<T>) return false;
                } else {
                    const Wide<T> quotient = wa / wb;
                    if (!agree(core::checked_div<T>(a, b), Option<T>::some(static_cast<T>(quotient)))) return false;
                    if (!agree(core::checked_rem<T>(a, b), Option<T>::some(static_cast<T>(wa % wb)))) return false;
                    if (core::saturating_div<T>(a, b) != static_cast<T>(quotient)) return false;
                }
            }

            // ── checked neg / abs (signed only) ──
            if constexpr (std::signed_integral<T>) {
                const bool is_min = a == core::min_of<T>;
                if (!agree(core::checked_neg<T>(a), option_of<T>(!is_min, -wa))) return false;
                if (!agree(core::checked_abs<T>(a), option_of<T>(!is_min, wa < 0 ? -wa : wa))) return false;
            }

            // ── checked shl / shr ──
            {
                const int shift = spec.shift;
                const bool shift_ok = shift >= 0 && shift < bits;
                // shl: the exact product a * 2^shift if it fits, else no
                // value.  The wide type holds that product for each T of
                // 64 bits or less.
                const Wide<T> product = shift_ok ? wa * (Wide<T>{1} << shift) : Wide<T>{0};
                if (!agree(core::checked_shl<T>(a, shift), option_of<T>(shift_ok && fits<T>(product), product))) {
                    return false;
                }
                const Wide<T> shifted = shift_ok ? (wa >> shift) : Wide<T>{0};
                if (!agree(core::checked_shr<T>(a, shift), option_of<T>(shift_ok, shifted))) return false;
            }

            // ── saturating add / sub / mul vs wide-clamp oracle ──
            //
            // The wide type must NOT wrap: for unsigned T, an underflowing
            // a-b must saturate to MIN (0), so the oracle computes in a
            // representation where the true (possibly negative) value is
            // visible.  signed __int128 holds every add/sub/mul of two
            // int64; for the one case it cannot (uint64 product up to
            // ~2^128) the unsigned path computes in unsigned __int128 and
            // only ever clamps high.
            {
                const T tmin = core::min_of<T>;
                const T tmax = core::max_of<T>;
                if constexpr (std::unsigned_integral<T>) {
                    const w_u sa = static_cast<w_u>(a) + static_cast<w_u>(b);
                    const T want_add = sa > static_cast<w_u>(tmax) ? tmax : static_cast<T>(sa);
                    if (core::saturating_add<T>(a, b) != want_add) return false;
                    const T want_sub = a >= b ? static_cast<T>(a - b) : tmin;
                    if (core::saturating_sub<T>(a, b) != want_sub) return false;
                    const w_u pa = static_cast<w_u>(a) * static_cast<w_u>(b);
                    const T want_mul = pa > static_cast<w_u>(tmax) ? tmax : static_cast<T>(pa);
                    if (core::saturating_mul<T>(a, b) != want_mul) return false;
                } else {
                    const w_s lo = static_cast<w_s>(tmin);
                    const w_s hi = static_cast<w_s>(tmax);
                    const auto clamp_s = [&](w_s x) noexcept -> T {
                        if (x < lo) return tmin;
                        if (x > hi) return tmax;
                        return static_cast<T>(x);
                    };
                    const w_s sa = static_cast<w_s>(a);
                    const w_s sb = static_cast<w_s>(b);
                    if (core::saturating_add<T>(a, b) != clamp_s(sa + sb)) return false;
                    if (core::saturating_sub<T>(a, b) != clamp_s(sa - sb)) return false;
                    if (core::saturating_mul<T>(a, b) != clamp_s(sa * sb)) return false;
                }
            }
            return true;
        });
}

}  // namespace

int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
    using namespace crucible::fuzz::prop;

    const Config cfg = parse_args(arguments, 2000000);

    int rc = 0;
    rc |= run_checked<std::int8_t>("checked_i8", cfg);
    rc |= run_checked<std::int16_t>("checked_i16", cfg);
    rc |= run_checked<std::int32_t>("checked_i32", cfg);
    rc |= run_checked<std::int64_t>("checked_i64", cfg);
    rc |= run_checked<std::uint8_t>("checked_u8", cfg);
    rc |= run_checked<std::uint16_t>("checked_u16", cfg);
    rc |= run_checked<std::uint32_t>("checked_u32", cfg);
    rc |= run_checked<std::uint64_t>("checked_u64", cfg);
    return rc;
}
