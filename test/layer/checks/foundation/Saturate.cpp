// The compile-time checks of foundation/Saturate.h.

#include <foundation/Saturate.h>

namespace foundation::sat {

namespace detail::saturate_self_test {

static_assert(add_sat<unsigned char>(250, 10) == 255);
static_assert(add_sat<signed char>(120, 10) == 127);
static_assert(add_sat<signed char>(-120, -10) == -128);
static_assert(add_sat<int>(1, 2) == 3);
static_assert(sub_sat<unsigned char>(5, 10) == 0);
static_assert(sub_sat<signed char>(-120, 10) == -128);
static_assert(sub_sat<signed char>(120, -10) == 127);
static_assert(mul_sat<unsigned char>(16, 16) == 255);
static_assert(mul_sat<signed char>(-16, 16) == -128);
static_assert(mul_sat<signed char>(-16, -16) == 127);
static_assert(mul_sat<long long>(3, 4) == 12);

}  // namespace detail::saturate_self_test

}  // namespace foundation::sat
