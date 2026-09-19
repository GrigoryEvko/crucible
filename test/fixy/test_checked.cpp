// Sentinel TU for fixy/Checked.h: each family names its overflow
// behaviour, and the three that differ at the same operands are shown
// answering differently.
//
// The header carries no runtime self-test, because every claim in it is
// a static_assert.  What a static_assert inside a header cannot cover is
// the runtime instantiation of the same functions, so this TU repeats
// the corner cases through operands the compiler cannot fold.

#include <fixy/Checked.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace {

using ::fixy::checked_abs;
using ::fixy::checked_add;
using ::fixy::checked_div;
using ::fixy::checked_mod;
using ::fixy::checked_mul;
using ::fixy::checked_neg;
using ::fixy::checked_shl;
using ::fixy::checked_shr;
using ::fixy::checked_sub;
using ::fixy::saturating_add;
using ::fixy::saturating_mul;
using ::fixy::saturating_sub;
using ::fixy::trapping_add;
using ::fixy::wrapping_add;
using ::fixy::wrapping_mul;
using ::fixy::wrapping_sub;

using I8 = std::int8_t;
using U8 = std::uint8_t;

constexpr I8 i8_min = std::numeric_limits<I8>::min();
constexpr I8 i8_max = std::numeric_limits<I8>::max();

// The checked family reports rather than produces.
static_assert(checked_add<U8>(200, 55).value() == 255);
static_assert(!checked_add<U8>(200, 56).has_value());
static_assert(!checked_sub<U8>(0, 1).has_value());
static_assert(checked_mul<U8>(16, 15).value() == 240);
static_assert(!checked_mul<U8>(16, 16).has_value());
static_assert(!checked_add<I8>(i8_max, 1).has_value());
static_assert(!checked_sub<I8>(i8_min, 1).has_value());

// Division has two failures, and the second one is not shared by the
// remainder of the same operands.
static_assert(!checked_div<I8>(7, 0).has_value());
static_assert(!checked_div<I8>(i8_min, -1).has_value());
static_assert(!checked_mod<I8>(7, 0).has_value());
static_assert(checked_mod<I8>(i8_min, -1).value() == 0);
static_assert(checked_div<I8>(-7, 2).value() == -3);
static_assert(checked_mod<I8>(-7, 2).value() == -1);

// Negation and absolute value fail at the one value with no positive
// counterpart.
static_assert(!checked_neg<I8>(i8_min).has_value());
static_assert(checked_neg<I8>(i8_max).value() == -i8_max);
static_assert(!checked_abs<I8>(i8_min).has_value());
static_assert(checked_abs<I8>(-7).value() == 7);
static_assert(checked_abs<I8>(7).value() == 7);

// Only a signed type has them at all, because an unsigned value has no
// sign to move.
template <typename T>
concept HasCheckedNeg = requires(T a) { checked_neg(a); };
static_assert(HasCheckedNeg<I8>);
static_assert(!HasCheckedNeg<U8>);

// A shift is refused when the count leaves the width, and a negative
// left operand is refused rather than shifted.
static_assert(checked_shl<U8>(1, 7).value() == 128);
static_assert(!checked_shl<U8>(1, 8).has_value());
static_assert(!checked_shl<U8>(1, -1).has_value());
static_assert(!checked_shl<I8>(-1, 1).has_value());
static_assert(checked_shr<U8>(128, 7).value() == 1);
static_assert(!checked_shr<U8>(128, 8).has_value());
static_assert(checked_shr<I8>(-8, 1).value() == -4);

// The wrapping family produces the truncated value the hardware would
// have produced, and the saturating family produces the end of the
// range.  At the same operands the three families answer differently.
static_assert(wrapping_add<U8>(200, 56) == 0);
static_assert(saturating_add<U8>(200, 56) == 255);
static_assert(!checked_add<U8>(200, 56).has_value());

static_assert(wrapping_sub<U8>(0, 1) == 255);
static_assert(saturating_sub<U8>(0, 1) == 0);
static_assert(wrapping_mul<U8>(16, 16) == 0);
static_assert(saturating_mul<U8>(16, 16) == 255);
static_assert(saturating_add<I8>(i8_min, -1) == i8_min);
static_assert(saturating_mul<I8>(-16, -16) == i8_max);

// Every non-overflowing case agrees across all three families, at every
// 8-bit pair.  A defect in one family shows as a disagreement here.
[[nodiscard]] consteval bool families_agree_where_the_result_fits_u8() noexcept {
    for (unsigned a = 0; a < 256u; ++a) {
        for (unsigned b = 0; b < 256u; ++b) {
            const auto lhs = static_cast<U8>(a);
            const auto rhs = static_cast<U8>(b);
            const std::optional<U8> got = checked_add(lhs, rhs);
            if (got.has_value() != (a + b <= 255u)) return false;
            if (got.has_value()) {
                if (*got != wrapping_add(lhs, rhs)) return false;
                if (*got != saturating_add(lhs, rhs)) return false;
            } else {
                if (saturating_add(lhs, rhs) != U8{255}) return false;
                if (wrapping_add(lhs, rhs) != static_cast<U8>(a + b)) return false;
            }
        }
    }
    return true;
}
static_assert(families_agree_where_the_result_fits_u8());

// The compile-time budget helpers carry the check through every step of
// a sum of products.
static_assert(::fixy::safe_capacity<64u, 64u> == 4096u);
static_assert(::fixy::safe_array_bytes<std::uint32_t, 16u> == 64u);
static_assert(::fixy::safe_add_all<std::size_t, 8u, 16u, 40u> == 64u);
static_assert(::fixy::safe_struct_bytes<std::uint64_t, std::uint64_t> == 16u);
static_assert(::fixy::bytes_fit_v<64u, ::fixy::safe_struct_bytes<std::uint64_t, std::uint64_t>>);

// The same corners, reached at run time, through operands the optimizer
// cannot see through.
int check_runtime_matches_consteval() {
    volatile U8 big = 200;
    volatile U8 step = 56;
    const auto a = static_cast<U8>(big);
    const auto b = static_cast<U8>(step);

    if (checked_add(a, b).has_value()) return 10;
    if (wrapping_add(a, b) != 0u) return 11;
    if (saturating_add(a, b) != 255u) return 12;
    if (trapping_add<U8>(a, U8{55}) != 255u) return 13;

    volatile I8 low = i8_min;
    volatile I8 minus_one = -1;
    const auto lhs = static_cast<I8>(low);
    const auto rhs = static_cast<I8>(minus_one);

    if (checked_div(lhs, rhs).has_value()) return 20;
    const std::optional<I8> rem = checked_mod(lhs, rhs);
    if (!rem.has_value() || *rem != 0) return 21;
    if (checked_neg(lhs).has_value()) return 22;
    if (checked_abs(lhs).has_value()) return 23;

    volatile int width = 8;
    if (checked_shl<U8>(1, static_cast<int>(width)).has_value()) return 30;
    if (checked_shr<U8>(128, static_cast<int>(width)).has_value()) return 31;
    const std::optional<U8> shifted = checked_shl<U8>(1, static_cast<int>(width) - 1);
    if (!shifted.has_value() || *shifted != 128u) return 32;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_runtime_matches_consteval(); rc != 0) return rc;

    return 0;
}
