// The compile-time checks of fixy/Saturated.h.

#include <fixy/Saturated.h>

namespace fixy {

static_assert(sizeof(Saturated<uint8_t>) == 2);
static_assert(sizeof(Saturated<uint16_t>) == 4);
static_assert(sizeof(Saturated<uint32_t>) == 8);
static_assert(sizeof(Saturated<uint64_t>) == 16);
static_assert(sizeof(Saturated<int8_t>) == 2);
static_assert(sizeof(Saturated<int32_t>) == 8);
static_assert(sizeof(Saturated<int64_t>) == 16);

static_assert(alignof(Saturated<uint64_t>) == alignof(uint64_t));
static_assert(alignof(Saturated<uint32_t>) == alignof(uint32_t));

static_assert(std::is_trivially_copyable_v<Saturated<uint64_t>>);
static_assert(std::is_trivially_copyable_v<Saturated<int64_t>>);
static_assert(std::is_trivially_destructible_v<Saturated<uint64_t>>);
static_assert(std::is_standard_layout_v<Saturated<uint64_t>>);

static_assert(!std::is_same_v<Saturated<uint64_t>, uint64_t>);
static_assert(std::is_convertible_v<uint64_t, Saturated<uint64_t>>);
static_assert(!std::is_convertible_v<Saturated<uint64_t>, uint64_t>);

namespace detail::saturated_self_test {

// No accessor writes the value, so the flag keeps its meaning.
template <typename S>
concept WritesValueInPlace = requires(S& carrier) { carrier.value() = typename S::value_type{}; };
static_assert(!WritesValueInPlace<Saturated<uint64_t>>);
static_assert(!WritesValueInPlace<Saturated<int8_t>>);

using Sat64 = Saturated<uint64_t>;
using SatI32 = Saturated<int32_t>;

[[nodiscard]] consteval bool default_zeroes() noexcept {
    Sat64 s{};
    return s.value() == 0 && !s.was_clamped();
}
static_assert(default_zeroes());

[[nodiscard]] consteval bool implicit_from_value() noexcept {
    Sat64 s = uint64_t{42};
    return s.value() == 42 && !s.was_clamped();
}
static_assert(implicit_from_value());

[[nodiscard]] consteval bool explicit_two_arg() noexcept {
    Sat64 s{uint64_t{99}, true};
    return s.value() == 99 && s.was_clamped();
}
static_assert(explicit_two_arg());

[[nodiscard]] consteval bool explicit_t_conversion() noexcept {
    Sat64 s{uint64_t{123}, true};
    auto v = static_cast<uint64_t>(s);
    return v == 123;
}
static_assert(explicit_t_conversion());

[[nodiscard]] consteval bool equality_pair_compare() noexcept {
    Sat64 a{uint64_t{5}, false};
    Sat64 b{uint64_t{5}, false};
    Sat64 c{uint64_t{5}, true};
    Sat64 d{uint64_t{6}, false};
    return (a == b) && !(a == c) && !(a == d);
}
static_assert(equality_pair_compare());

[[nodiscard]] consteval bool add_sat_no_overflow() noexcept {
    auto s = add_sat_checked<uint64_t>(100, 200);
    return s.value() == 300 && !s.was_clamped();
}
static_assert(add_sat_no_overflow());

[[nodiscard]] consteval bool add_sat_unsigned_overflow() noexcept {
    auto s = add_sat_checked<uint8_t>(200, 100);
    return s.value() == std::numeric_limits<uint8_t>::max() && s.was_clamped();
}
static_assert(add_sat_unsigned_overflow());

[[nodiscard]] consteval bool add_sat_signed_pos_overflow() noexcept {
    auto s = add_sat_checked<int8_t>(int8_t{100}, int8_t{50});
    return s.value() == std::numeric_limits<int8_t>::max() && s.was_clamped();
}
static_assert(add_sat_signed_pos_overflow());

[[nodiscard]] consteval bool add_sat_signed_neg_overflow() noexcept {
    auto s = add_sat_checked<int8_t>(int8_t{-100}, int8_t{-50});
    return s.value() == std::numeric_limits<int8_t>::min() && s.was_clamped();
}
static_assert(add_sat_signed_neg_overflow());

[[nodiscard]] consteval bool sub_sat_unsigned_underflow() noexcept {
    auto s = sub_sat_checked<uint64_t>(5, 10);
    return s.value() == 0 && s.was_clamped();
}
static_assert(sub_sat_unsigned_underflow());

[[nodiscard]] consteval bool sub_sat_signed_overflow() noexcept {
    auto s = sub_sat_checked<int8_t>(int8_t{100}, int8_t{-50});
    return s.value() == std::numeric_limits<int8_t>::max() && s.was_clamped();
}
static_assert(sub_sat_signed_overflow());

[[nodiscard]] consteval bool mul_sat_unsigned_overflow() noexcept {
    auto s = mul_sat_checked<uint8_t>(20, 20);
    return s.value() == std::numeric_limits<uint8_t>::max() && s.was_clamped();
}
static_assert(mul_sat_unsigned_overflow());

[[nodiscard]] consteval bool mul_sat_signed_pos_pos_overflow() noexcept {
    auto s = mul_sat_checked<int8_t>(int8_t{50}, int8_t{50});
    return s.value() == std::numeric_limits<int8_t>::max() && s.was_clamped();
}
static_assert(mul_sat_signed_pos_pos_overflow());

[[nodiscard]] consteval bool mul_sat_signed_neg_pos_overflow() noexcept {
    auto s = mul_sat_checked<int8_t>(int8_t{-50}, int8_t{50});
    return s.value() == std::numeric_limits<int8_t>::min() && s.was_clamped();
}
static_assert(mul_sat_signed_neg_pos_overflow());

[[nodiscard]] consteval bool mul_sat_no_overflow() noexcept {
    auto s = mul_sat_checked<uint64_t>(7, 6);
    return s.value() == 42 && !s.was_clamped();
}
static_assert(mul_sat_no_overflow());

static_assert(Sat64::wrapper_kind() == "structural::Saturated");

// A floating-point element is admitted by the arithmetic constraint, so
// the type-system properties are checked here.  The value cannot be,
// because comparing floating-point values with equality is a build
// error in this project.
static_assert(std::is_arithmetic_v<float>);
static_assert(sizeof(Saturated<float>) == 8);

}  // namespace detail::saturated_self_test

}  // namespace fixy
