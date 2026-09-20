// Sentinel TU for fixy/Saturated.h: the clamped value comes from the
// library and the flag records that clamping happened.
//
// The port made the three checked operations delegate their clamped
// value to std::saturating_*, keeping only the flag.  This TU pins the
// property that delegation has to preserve: the flag is set exactly when
// the exact result does not fit, and the value is then the end of the
// range the exact result ran past.

#include <fixy/Saturated.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

using ::fixy::add_sat_checked;
using ::fixy::mul_sat_checked;
using ::fixy::Saturated;
using ::fixy::sub_sat_checked;

static_assert(sizeof(Saturated<std::uint8_t>) == 2);
static_assert(std::is_trivially_copyable_v<Saturated<std::uint64_t>>);

// A plain value converts in, because that can only mean no clamping was
// observed.  Unwrapping drops the observation and stays explicit.
static_assert(std::is_convertible_v<std::uint64_t, Saturated<std::uint64_t>>);
static_assert(!std::is_convertible_v<Saturated<std::uint64_t>, std::uint64_t>);

// The flag takes part in equality: the same number reached two ways is
// two different values.
static_assert(Saturated<int>{5} == Saturated<int>{5});
static_assert(!(Saturated<int>{5} == Saturated<int>{5, true}));

// The carrier admits any arithmetic type, but only an integral one has
// checked operations, because the clamp is defined by the integer range.
static_assert(std::is_arithmetic_v<float>);
template <typename T>
concept HasCheckedAdd = requires(T a, T b) { add_sat_checked(a, b); };
static_assert(HasCheckedAdd<std::int32_t>);
static_assert(!HasCheckedAdd<float>, "the clamp is an integer-range notion");
static_assert(!HasCheckedAdd<double>);

// The flag fires exactly when the exact result does not fit, at every
// 8-bit pair.  This is the check the delegation has to survive.
[[nodiscard]] consteval bool add_flag_and_value_agree_exhaustively_u8() noexcept {
    for (unsigned a = 0; a < 256u; ++a) {
        for (unsigned b = 0; b < 256u; ++b) {
            const auto lhs = static_cast<std::uint8_t>(a);
            const auto rhs = static_cast<std::uint8_t>(b);
            const unsigned exact = a + b;
            const bool fits = exact <= 255u;
            const Saturated<std::uint8_t> got = add_sat_checked(lhs, rhs);
            if (got.was_clamped() == fits) return false;
            const auto want = fits ? static_cast<std::uint8_t>(exact) : std::uint8_t{255};
            if (got.value() != want) return false;
        }
    }
    return true;
}
static_assert(add_flag_and_value_agree_exhaustively_u8());

[[nodiscard]] consteval bool sub_flag_and_value_agree_exhaustively_u8() noexcept {
    for (unsigned a = 0; a < 256u; ++a) {
        for (unsigned b = 0; b < 256u; ++b) {
            const auto lhs = static_cast<std::uint8_t>(a);
            const auto rhs = static_cast<std::uint8_t>(b);
            const bool fits = a >= b;
            const Saturated<std::uint8_t> got = sub_sat_checked(lhs, rhs);
            if (got.was_clamped() == fits) return false;
            const auto want = fits ? static_cast<std::uint8_t>(a - b) : std::uint8_t{0};
            if (got.value() != want) return false;
        }
    }
    return true;
}
static_assert(sub_flag_and_value_agree_exhaustively_u8());

// The signed direction is where a hand-written clamp rule would have
// picked the wrong end.  Both ends are exercised.
static_assert(add_sat_checked<std::int8_t>(120, 10).value() == std::numeric_limits<std::int8_t>::max());
static_assert(add_sat_checked<std::int8_t>(120, 10).was_clamped());
static_assert(add_sat_checked<std::int8_t>(-120, -10).value() == std::numeric_limits<std::int8_t>::min());
static_assert(sub_sat_checked<std::int8_t>(-120, 10).value() == std::numeric_limits<std::int8_t>::min());
static_assert(sub_sat_checked<std::int8_t>(120, -10).value() == std::numeric_limits<std::int8_t>::max());
static_assert(mul_sat_checked<std::int8_t>(-16, 16).value() == std::numeric_limits<std::int8_t>::min());
static_assert(mul_sat_checked<std::int8_t>(-16, -16).value() == std::numeric_limits<std::int8_t>::max());
static_assert(mul_sat_checked<std::int8_t>(2, 3).value() == 6);
static_assert(!mul_sat_checked<std::int8_t>(2, 3).was_clamped());

int check_runtime_matches_consteval() {
    volatile std::uint8_t hi = 250;
    volatile std::uint8_t step = 10;
    const auto a = static_cast<std::uint8_t>(hi);
    const auto b = static_cast<std::uint8_t>(step);

    const Saturated<std::uint8_t> sum = add_sat_checked(a, b);
    if (!sum.was_clamped() || sum.value() != 255u) return 10;

    const Saturated<std::uint8_t> diff = sub_sat_checked(b, a);
    if (!diff.was_clamped() || diff.value() != 0u) return 11;

    const Saturated<std::uint8_t> prod = mul_sat_checked(a, b);
    if (!prod.was_clamped() || prod.value() != 255u) return 12;

    const Saturated<std::uint8_t> clean = add_sat_checked(b, b);
    if (clean.was_clamped() || clean.value() != 20u) return 13;

    return 0;
}

// The carrier holds two fields, and every door that fills them has to
// fill both.  The equality above is a constant expression; these are the
// same claims against values the compiler cannot fold.
int check_carrier_shape() {
    using Sat64 = Saturated<std::uint64_t>;

    Sat64 empty{};
    if (empty.value() != 0u || empty.was_clamped()) return 20;

    Sat64 converted = std::uint64_t{777};
    if (converted.value() != 777u || converted.was_clamped()) return 21;

    Sat64 flagged{std::uint64_t{888}, true};
    if (flagged.value() != 888u || !flagged.was_clamped()) return 22;

    const auto add_ok = add_sat_checked<std::uint64_t>(10, 20);
    if (add_ok.value() != 30u || add_ok.was_clamped()) return 23;

    const auto sub_clamped = sub_sat_checked<std::uint64_t>(5, 10);
    if (sub_clamped.value() != 0u || !sub_clamped.was_clamped()) return 24;

    const auto mul_ok = mul_sat_checked<std::uint64_t>(7, 6);
    if (mul_ok.value() != 42u || mul_ok.was_clamped()) return 25;

    // The flag is part of the identity, so two carriers holding the same
    // value but a different flag are not equal.
    if (!(Sat64{std::uint64_t{5}, false} == Sat64{std::uint64_t{5}, false})) return 26;
    if (Sat64{std::uint64_t{5}, false} == Sat64{std::uint64_t{5}, true}) return 27;

    // The explicit conversion releases the value and drops the flag.
    if (static_cast<std::uint64_t>(Sat64{std::uint64_t{5}, true}) != 5u) return 28;

    const Sat64 src{std::uint64_t{0xDEADBEEFCAFEBABEull}, true};
    Sat64 dst;
    dst = src;
    if (dst.value() != src.value() || dst.was_clamped() != src.was_clamped()) return 29;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_carrier_shape(); rc != 0) return rc;
    if (int rc = check_runtime_matches_consteval(); rc != 0) return rc;

    return 0;
}
