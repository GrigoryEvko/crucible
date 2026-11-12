// The compile-time checks of foundation/core/Compare.h.

#include <foundation/core/Compare.h>

#include <compare>
#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::compare_checks {

// -Werror=float-equal refuses == of two floats, so the checks compare with
// <= and >=, and a NaN is unordered with itself.
template <class T>
[[nodiscard]] consteval bool same_value(T left, T right) {
    return left <= right && left >= right;
}
template <class T>
[[nodiscard]] consteval bool is_unordered(T value) {
    return !(value <= value) && !(value >= value);
}

// min and max of a NaN give the first operand.  -0 and +0 compare equal, so
// they give the first operand too.
static_assert(min(3, 5) == 3 && min(5, 3) == 3 && max(3, 5) == 5 && max(5, 3) == 5);
static_assert(min(min_of<std::int64_t>, max_of<std::int64_t>) == min_of<std::int64_t>);
static_assert(is_unordered(min(quiet_nan_of<double>, 1.0)) && same_value(min(1.0, quiet_nan_of<double>), 1.0));
static_assert(is_unordered(max(quiet_nan_of<float>, 1.0f)) && same_value(max(1.0f, quiet_nan_of<float>), 1.0f));
static_assert(min(-0.0, 0.0) < 1.0 && max(-0.0, 0.0) > -1.0);

// Bounds proves lo <= hi and no NaN.
static_assert(Bounds<int>::of(1, 2).is_some() && Bounds<int>::of(2, 2).is_some() && Bounds<int>::of(3, 2).is_none());
static_assert(Bounds<double>::of(quiet_nan_of<double>, 1.0).is_none());
static_assert(Bounds<double>::of(0.0, quiet_nan_of<double>).is_none());
static_assert(Bounds<double>::of(-infinity_of<double>, infinity_of<double>).is_some());
static_assert(bounds<0, 10>().lo() == 0 && bounds<0, 10>().hi() == 10);
static_assert(std::is_same_v<decltype(bounds<std::uint8_t{1}, std::uint8_t{9}>()), Bounds<std::uint8_t>>);
static_assert(OrderedBounds<1, 1> && !OrderedBounds<2, 1> && !OrderedBounds<1, 2u> && !OrderedBounds<true, true>);
static_assert(!OrderedBounds<quiet_nan_of<double>, 1.0> && OrderedBounds<-1.5, 1.5>);
static_assert(std::is_trivially_copyable_v<Bounds<int>> && sizeof(Bounds<int>) == 8);
static_assert(sizeof(Option<Bounds<int>>) == 12);
static_assert(!std::is_default_constructible_v<Bounds<int>> && !std::is_constructible_v<Bounds<int>, int, int>);

// clamp, and a NaN gives lo.
static_assert(clamp(5, bounds<0, 10>()) == 5 && clamp(-5, bounds<0, 10>()) == 0 && clamp(50, bounds<0, 10>()) == 10);
static_assert(same_value(clamp(quiet_nan_of<double>, bounds<-1.0, 1.0>()), -1.0));
static_assert(same_value(clamp(2.5, bounds<-1.0, 1.0>()), 1.0));

// compare is exact for each pair of signs.
static_assert(compare(-1, 0u) < 0 && compare(0u, -1) > 0 && compare(std::int8_t{-1}, max_of<std::uint64_t>) < 0);
static_assert(compare(max_of<std::uint64_t>, max_of<std::int64_t>) > 0 && compare(5, 5u) == 0);
static_assert(compare(std::uint8_t{200}, std::uint64_t{200}) == 0 && compare(std::int16_t{-3}, 2L) < 0);
static_assert(compare(std::uint8_t{1}, std::uint16_t{2}) < 0 && compare(std::int8_t{3}, std::uint8_t{2}) > 0);
static_assert(std::is_same_v<decltype(compare(1, 2u)), std::strong_ordering>);

// fits.
static_assert(fits<std::uint8_t>(255) && !fits<std::uint8_t>(256) && !fits<std::uint8_t>(-1));
static_assert(fits<std::int64_t>(max_of<std::uint32_t>) && !fits<std::int64_t>(max_of<std::uint64_t>));
static_assert(fits<std::int8_t>(-128) && !fits<std::int8_t>(-129) && fits<std::uint64_t>(std::uint8_t{3}));
static_assert(!fits<std::uint32_t>(std::int64_t{-1}) && fits<std::uint32_t>(std::int64_t{4294967295}));

// unsigned_abs and abs_diff are unsigned and total.
static_assert(unsigned_abs(-5) == 5u && unsigned_abs(min_of<std::int64_t>) == std::uint64_t{1} << 63);
static_assert(std::is_same_v<decltype(unsigned_abs(std::int8_t{-1})), std::uint8_t>);
static_assert(abs_diff(min_of<std::int8_t>, max_of<std::int8_t>) == 255 && abs_diff(3u, 7u) == 4u);
static_assert(abs_diff(7, 3) == 4u && abs_diff(min_of<std::int64_t>, std::int64_t{0}) == std::uint64_t{1} << 63);

}  // namespace foundation::core::detail::compare_checks
