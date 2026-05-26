// ═══════════════════════════════════════════════════════════════════
// prop_checked_arith.cpp — multi-width fuzzer for the overflow-mode
// arithmetic primitives (safety/Checked.h).
//
// Checked.h is an L0 foundation: every size/offset/capacity computation
// that must not silently wrap routes through checked_* / wrapping_* /
// trapping_* / saturating_*.  The family is `template <std::integral T>`
// — it claims correctness for ALL integral widths and signedness.  But
// test_checked.cpp instantiates the RUNTIME ops at only int32_t and
// uint32_t (the compile-time safe_* helpers are separately self-tested
// via static_assert in the header).  The int8/int16/int64 + unsigned
// widths — where integer promotion, two's-complement truncation, and
// 64-bit overflow boundaries behave differently — are unexercised.
// That is the firing-12 pattern (a generic numeric template tested at
// one width); on a MemSafe/TypeSafe primitive it is worth closing.
//
// The oracle is an INDEPENDENT 128-bit-wide computation: every op is
// recomputed in __int128 / unsigned __int128 (no overflow possible for
// any T ≤ 64 bits — the product of two int64 is ≤ 2^126), then the
// result is range-checked against [T_min, T_max] or truncated to T.
// That is a different computation from the __builtin_*_overflow flag
// path and the native promoted-then-cast path, so a divergence is a
// genuine bug.  Per (a, b, shift) over 8 widths, corner-biased to
// {0, ±1, MIN, MAX}, it asserts:
//
//   checked_add/sub/mul  == wide-result-if-it-fits, else nullopt
//   checked_div/mod      == zero→nullopt, signed MIN/-1 handled, else exact
//   checked_neg/abs      (signed) == MIN→nullopt, else exact
//   checked_shl/shr      == invalid-shift→nullopt, negative-left→nullopt,
//                           else the truncated wide shift
//   wrapping_add/sub/mul == wide result truncated to T (mod 2^bits)
//   trapping_add/sub/mul == checked value (only on non-overflowing inputs;
//                           the abort path can't be fuzzed in-process)
//   saturating_add/sub/mul == wide result clamped to [T_min, T_max]
//
// All verified clean by hand-trace; this is the multi-width regression
// net the int32/uint32-only test lacks.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/safety/Checked.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace {

namespace ck = crucible::safety;
using crucible::fuzz::prop::Rng;

__extension__ using w_s = __int128;
__extension__ using w_u = unsigned __int128;

template <typename T>
using Wide = std::conditional_t<std::signed_integral<T>, w_s, w_u>;

template <typename T>
[[nodiscard]] constexpr bool fits(Wide<T> x) noexcept {
    return x >= static_cast<Wide<T>>(std::numeric_limits<T>::min()) &&
           x <= static_cast<Wide<T>>(std::numeric_limits<T>::max());
}

template <typename T>
struct Spec {
    T a = 0;
    T b = 0;
    int shift = 0;
    std::uint8_t pad[4]{};
};

template <typename T>
[[nodiscard]] T gen_value(Rng& rng) noexcept {
    switch (rng.next_below(8u)) {
        case 0: return T{0};
        case 1: return T{1};
        case 2: return std::numeric_limits<T>::max();
        case 3: return std::numeric_limits<T>::min();
        case 4: return static_cast<T>(~T{0});  // -1 signed / max unsigned
        default: return static_cast<T>(rng.next64());
    }
}

template <typename T>
[[nodiscard]] int run_checked(const char* name, crucible::fuzz::prop::Config cfg) {
    constexpr int bits = static_cast<int>(sizeof(T) * 8);
    return crucible::fuzz::prop::run(name, cfg,
        [](Rng& rng) noexcept -> Spec<T> {
            Spec<T> spec{};
            spec.a = gen_value<T>(rng);
            spec.b = gen_value<T>(rng);
            // shift ∈ [-2, bits+1] — covers negative, valid, and >= bits.
            spec.shift = static_cast<int>(
                rng.next_below(static_cast<std::uint32_t>(bits) + 4u)) - 2;
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
                const std::optional<T> want =
                    fits<T>(s) ? std::optional<T>{static_cast<T>(s)} : std::nullopt;
                if (ck::checked_add<T>(a, b) != want) return false;
                if (want.has_value() && ck::trapping_add<T>(a, b) != *want) return false;
                if (ck::wrapping_add<T>(a, b) != static_cast<T>(s)) return false;
            }
            {
                const Wide<T> d = wa - wb;
                const std::optional<T> want =
                    fits<T>(d) ? std::optional<T>{static_cast<T>(d)} : std::nullopt;
                if (ck::checked_sub<T>(a, b) != want) return false;
                if (want.has_value() && ck::trapping_sub<T>(a, b) != *want) return false;
                if (ck::wrapping_sub<T>(a, b) != static_cast<T>(d)) return false;
            }
            {
                const Wide<T> p = wa * wb;
                const std::optional<T> want =
                    fits<T>(p) ? std::optional<T>{static_cast<T>(p)} : std::nullopt;
                if (ck::checked_mul<T>(a, b) != want) return false;
                if (want.has_value() && ck::trapping_mul<T>(a, b) != *want) return false;
                if (ck::wrapping_mul<T>(a, b) != static_cast<T>(p)) return false;
            }

            // ── checked div / mod ──
            {
                bool is_min_over_neg1 = false;
                if constexpr (std::signed_integral<T>) {
                    is_min_over_neg1 = (a == std::numeric_limits<T>::min() &&
                                        b == static_cast<T>(-1));
                }
                std::optional<T> want_div;
                std::optional<T> want_mod;
                if (b == T{0}) {
                    want_div = std::nullopt;
                    want_mod = std::nullopt;
                } else if (is_min_over_neg1) {
                    want_div = std::nullopt;            // overflow
                    want_mod = std::optional<T>{T{0}};  // defined as 0
                } else {
                    want_div = std::optional<T>{static_cast<T>(wa / wb)};
                    want_mod = std::optional<T>{static_cast<T>(wa % wb)};
                }
                if (ck::checked_div<T>(a, b) != want_div) return false;
                if (ck::checked_mod<T>(a, b) != want_mod) return false;
            }

            // ── checked neg / abs (signed only) ──
            if constexpr (std::signed_integral<T>) {
                const std::optional<T> want_neg =
                    a == std::numeric_limits<T>::min()
                        ? std::nullopt
                        : std::optional<T>{static_cast<T>(-wa)};
                if (ck::checked_neg<T>(a) != want_neg) return false;
                const std::optional<T> want_abs =
                    a == std::numeric_limits<T>::min()
                        ? std::nullopt
                        : std::optional<T>{static_cast<T>(wa < 0 ? -wa : wa)};
                if (ck::checked_abs<T>(a) != want_abs) return false;
            }

            // ── checked shl / shr ──
            {
                const int shift = spec.shift;
                const bool shift_ok = shift >= 0 && shift < bits;
                // shl: also rejects negative-value left shift (signed, UB).
                bool reject_shl = !shift_ok;
                if constexpr (std::signed_integral<T>) {
                    if (a < T{0}) reject_shl = true;
                }
                const std::optional<T> want_shl = reject_shl
                    ? std::nullopt
                    : std::optional<T>{static_cast<T>(static_cast<w_u>(wa) << shift)};
                if (ck::checked_shl<T>(a, shift) != want_shl) return false;

                const std::optional<T> want_shr =
                    shift_ok ? std::optional<T>{static_cast<T>(wa >> shift)}
                             : std::nullopt;
                if (ck::checked_shr<T>(a, shift) != want_shr) return false;
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
                const T tmin = std::numeric_limits<T>::min();
                const T tmax = std::numeric_limits<T>::max();
                if constexpr (std::unsigned_integral<T>) {
                    const w_u sa = static_cast<w_u>(a) + static_cast<w_u>(b);
                    const T want_add = sa > static_cast<w_u>(tmax) ? tmax : static_cast<T>(sa);
                    if (ck::saturating_add<T>(a, b) != want_add) return false;
                    const T want_sub = a >= b ? static_cast<T>(a - b) : tmin;
                    if (ck::saturating_sub<T>(a, b) != want_sub) return false;
                    const w_u pa = static_cast<w_u>(a) * static_cast<w_u>(b);
                    const T want_mul = pa > static_cast<w_u>(tmax) ? tmax : static_cast<T>(pa);
                    if (ck::saturating_mul<T>(a, b) != want_mul) return false;
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
                    if (ck::saturating_add<T>(a, b) != clamp_s(sa + sb)) return false;
                    if (ck::saturating_sub<T>(a, b) != clamp_s(sa - sb)) return false;
                    if (ck::saturating_mul<T>(a, b) != clamp_s(sa * sb)) return false;
                }
            }
            return true;
        });
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

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
