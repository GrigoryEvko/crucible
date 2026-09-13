#pragma once

#include <crucible/safety/Checked.h>

#include <cstddef>
#include <cstdint>

namespace crucible::fixy::wrap {

using ::crucible::safety::checked_add;
using ::crucible::safety::checked_sub;
using ::crucible::safety::checked_mul;
using ::crucible::safety::checked_div;
using ::crucible::safety::checked_mod;
using ::crucible::safety::checked_neg;
using ::crucible::safety::checked_abs;
using ::crucible::safety::checked_shl;
using ::crucible::safety::checked_shr;

using ::crucible::safety::wrapping_add;
using ::crucible::safety::wrapping_sub;
using ::crucible::safety::wrapping_mul;

using ::crucible::safety::trapping_add;
using ::crucible::safety::trapping_sub;
using ::crucible::safety::trapping_mul;
using ::crucible::safety::trapping_div;

using ::crucible::safety::saturating_add;
using ::crucible::safety::saturating_sub;
using ::crucible::safety::saturating_mul;

using ::crucible::safety::safe_add;
using ::crucible::safety::safe_sub;
using ::crucible::safety::safe_mul;
using ::crucible::safety::safe_capacity;
using ::crucible::safety::safe_byte_budget;
using ::crucible::safety::safe_add_all;
using ::crucible::safety::safe_array_bytes;
using ::crucible::safety::safe_struct_bytes;
using ::crucible::safety::safe_size_sum;
using ::crucible::safety::safe_size_diff;

using ::crucible::safety::bytes_fit_v;
using ::crucible::safety::ensure_bytes_fit;

namespace self_test_checked {

static_assert(::crucible::fixy::wrap::safe_add<std::uint32_t, 10u, 20u>
              == ::crucible::safety::safe_add<std::uint32_t, 10u, 20u>);
static_assert(::crucible::fixy::wrap::safe_sub<std::uint32_t, 30u, 20u>
              == ::crucible::safety::safe_sub<std::uint32_t, 30u, 20u>);
static_assert(::crucible::fixy::wrap::safe_mul<std::uint32_t, 6u, 7u>
              == ::crucible::safety::safe_mul<std::uint32_t, 6u, 7u>);

static_assert(::crucible::fixy::wrap::safe_capacity<8u, 16u> == ::crucible::safety::safe_capacity<8u, 16u>);
static_assert(::crucible::fixy::wrap::safe_capacity<8u, 16u> == 128u);

static_assert(::crucible::fixy::wrap::safe_byte_budget<256u, 64u> == ::crucible::safety::safe_byte_budget<256u, 64u>);

static_assert(::crucible::fixy::wrap::safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u>
              == ::crucible::safety::safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u>);
static_assert(::crucible::fixy::wrap::safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u> == 15u);

static_assert(::crucible::fixy::wrap::safe_array_bytes<std::uint64_t, 8u>
              == ::crucible::safety::safe_array_bytes<std::uint64_t, 8u>);
static_assert(::crucible::fixy::wrap::safe_array_bytes<std::uint64_t, 8u> == 64u);

static_assert(::crucible::fixy::wrap::safe_struct_bytes<std::uint64_t, std::uint32_t>
              == ::crucible::safety::safe_struct_bytes<std::uint64_t, std::uint32_t>);
static_assert(::crucible::fixy::wrap::safe_struct_bytes<std::uint64_t, std::uint32_t> == 12u);

static_assert(::crucible::fixy::wrap::safe_size_sum<10u, 20u> == ::crucible::safety::safe_size_sum<10u, 20u>);
static_assert(::crucible::fixy::wrap::safe_size_diff<30u, 10u> == ::crucible::safety::safe_size_diff<30u, 10u>);

static_assert(::crucible::fixy::wrap::bytes_fit_v<64u, 20u> == ::crucible::safety::bytes_fit_v<64u, 20u>);
static_assert(::crucible::fixy::wrap::bytes_fit_v<64u, 20u> == true);
static_assert(::crucible::fixy::wrap::bytes_fit_v<64u, 64u> == true);
static_assert(::crucible::fixy::wrap::bytes_fit_v<64u, 65u> == false);

// The initializer forces the consteval call, so a budget overflow fires the
// substrate's static assertion at this include site.
[[maybe_unused]] constexpr auto _ensure_bytes_fit_alias_witness = []() {
    ::crucible::fixy::wrap::ensure_bytes_fit<64u,
                                             ::crucible::fixy::wrap::safe_struct_bytes<std::uint64_t, std::uint64_t>>();
    return 0;
}();

static_assert(::crucible::fixy::wrap::checked_add<std::uint32_t>(10u, 20u)
              == ::crucible::safety::checked_add<std::uint32_t>(10u, 20u));
static_assert(::crucible::fixy::wrap::checked_add<std::uint32_t>(10u, 20u) == std::optional<std::uint32_t>{30u});

static_assert(!::crucible::fixy::wrap::checked_add<std::uint8_t>(200u, 100u).has_value());
static_assert(!::crucible::fixy::wrap::checked_mul<std::uint16_t>(300u, 300u).has_value());

static_assert(!::crucible::fixy::wrap::checked_div<int>(10, 0).has_value());
static_assert(::crucible::fixy::wrap::checked_div<int>(10, 2) == std::optional<int>{5});

static_assert(!::crucible::fixy::wrap::checked_neg<std::int32_t>(INT32_MIN).has_value());
static_assert(!::crucible::fixy::wrap::checked_abs<std::int32_t>(INT32_MIN).has_value());

static_assert(!::crucible::fixy::wrap::checked_shl<std::uint32_t>(1u, 32).has_value());
static_assert(::crucible::fixy::wrap::checked_shl<std::uint32_t>(1u, 4) == std::optional<std::uint32_t>{16u});
static_assert(!::crucible::fixy::wrap::checked_shr<std::uint32_t>(1u, -1).has_value());

static_assert(::crucible::fixy::wrap::checked_mod<int>(7, 3) == std::optional<int>{1});
static_assert(!::crucible::fixy::wrap::checked_mod<int>(7, 0).has_value());
static_assert(::crucible::fixy::wrap::checked_mod<std::int32_t>(INT32_MIN, -1) == std::optional<std::int32_t>{0});

static_assert(::crucible::fixy::wrap::checked_sub<std::uint32_t>(30u, 10u) == std::optional<std::uint32_t>{20u});
static_assert(!::crucible::fixy::wrap::checked_sub<std::uint32_t>(10u, 30u).has_value());

static_assert(::crucible::fixy::wrap::wrapping_add<std::uint8_t>(200u, 100u)
              == ::crucible::safety::wrapping_add<std::uint8_t>(200u, 100u));
static_assert(::crucible::fixy::wrap::wrapping_add<std::uint8_t>(200u, 100u) == static_cast<std::uint8_t>(44u));

static_assert(::crucible::fixy::wrap::wrapping_sub<std::uint8_t>(10u, 20u) == static_cast<std::uint8_t>(246u));
static_assert(::crucible::fixy::wrap::wrapping_mul<std::uint8_t>(20u, 20u) == static_cast<std::uint8_t>(144u));

// The abort path cannot run at constexpr, so only the no-overflow path is
// witnessed here.
static_assert(::crucible::fixy::wrap::trapping_add<std::uint32_t>(10u, 20u)
              == ::crucible::safety::trapping_add<std::uint32_t>(10u, 20u));
static_assert(::crucible::fixy::wrap::trapping_add<std::uint32_t>(10u, 20u) == 30u);
static_assert(::crucible::fixy::wrap::trapping_sub<std::uint32_t>(30u, 10u) == 20u);
static_assert(::crucible::fixy::wrap::trapping_mul<std::uint32_t>(6u, 7u) == 42u);
static_assert(::crucible::fixy::wrap::trapping_div<int>(20, 4) == 5);

static_assert(::crucible::fixy::wrap::saturating_add<std::uint8_t>(200u, 100u)
              == ::crucible::safety::saturating_add<std::uint8_t>(200u, 100u));
static_assert(::crucible::fixy::wrap::saturating_add<std::uint8_t>(200u, 100u) == static_cast<std::uint8_t>(255u));
static_assert(::crucible::fixy::wrap::saturating_sub<std::uint8_t>(10u, 20u) == static_cast<std::uint8_t>(0u));
static_assert(::crucible::fixy::wrap::saturating_mul<std::uint16_t>(300u, 300u) == static_cast<std::uint16_t>(65535u));

constexpr int checked_alias_cardinality = 31;
static_assert(checked_alias_cardinality == 31, "The re-exported overflow-arithmetic surface cardinality changed. "
                                               "Update the using-decls and this sentinel together.");

}  // namespace self_test_checked

}  // namespace crucible::fixy::wrap
