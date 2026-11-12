// The compile-time checks of fixy/Core.h.

#include <fixy/Core.h>

#include <compare>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::detail::core_checks {

// Each fixy name is the foundation entity, not a second type.
static_assert(std::is_same_v<::fixy::Option<int>, ::foundation::core::Option<int>>);
static_assert(std::is_same_v<::fixy::NoValue, ::foundation::core::NoValue>);
static_assert(std::is_same_v<::fixy::niche_key<int>, ::foundation::core::niche_key<int>>);
static_assert(std::is_same_v<decltype(::fixy::none), decltype(::foundation::core::none)>);
static_assert(::fixy::Option<int>{::fixy::none}.is_none());
static_assert(std::is_same_v<decltype(::fixy::some(7)), ::foundation::core::Option<int>>);
static_assert(::fixy::some(7).is_some());
static_assert(std::is_same_v<::fixy::Record<int, long>, ::foundation::core::Record<int, long>>);
static_assert(std::is_same_v<::fixy::Pack<int, long>, ::foundation::core::Pack<int, long>>);
static_assert(::fixy::pack_size_v<::fixy::Pack<int, long>> == 2);
static_assert(std::is_same_v<::fixy::pack_at_t<1, ::fixy::Pack<int, long>>, long>);
static_assert(::fixy::pack_contains_v<long, ::fixy::Pack<int, long>>);
static_assert(::fixy::pack_index_of_v<long, ::fixy::Pack<int, long>> == 1);
static_assert(std::is_same_v<::fixy::pack_concat_t<::fixy::Pack<int>, ::fixy::Pack<long>>, ::fixy::Pack<int, long>>);
static_assert(std::is_same_v<::fixy::pack_apply_t<::fixy::Record, ::fixy::Pack<int, long>>, ::fixy::Record<int, long>>);
static_assert(::fixy::pack_unique_v<::fixy::Pack<int, long>>);
static_assert(std::is_same_v<::fixy::Box<int>, ::foundation::core::Box<int>>);
static_assert(std::is_same_v<::fixy::Ref<int>, ::foundation::core::Ref<int>>);
static_assert(std::is_same_v<::fixy::Address, ::foundation::core::Address>);
static_assert(
    std::is_same_v<decltype(::fixy::address_of(::fixy::Ref<int>::to(std::declval<int&>()))), ::fixy::Address>);
static_assert(std::is_same_v<decltype(::fixy::same_object(::fixy::Ref<int>::to(std::declval<int&>()),
                                                          ::fixy::Ref<int>::to(std::declval<int&>()))),
                             bool>);
static_assert(std::is_same_v<::fixy::Atomic<int>, ::foundation::core::Atomic<int>>);
static_assert(std::is_same_v<::fixy::CacheLine<int>, ::foundation::core::CacheLine<int>>);
static_assert(std::is_same_v<::fixy::CasRefusal<int>, ::foundation::core::CasRefusal<int>>);
static_assert(std::is_same_v<::fixy::Result<::fixy::Unit, ::fixy::LengthMismatch>,
                             ::foundation::core::Result<::foundation::core::Unit, ::foundation::core::LengthMismatch>>);
static_assert(std::is_same_v<decltype(::fixy::err(::fixy::LengthMismatch{})), ::fixy::Err<::fixy::LengthMismatch>>);
static_assert(std::is_same_v<::fixy::Tally, ::foundation::core::Tally>);
static_assert(std::is_same_v<::fixy::View<int, 4>, ::foundation::core::View<int, 4>>);
static_assert(::fixy::dynamic_extent == ::foundation::core::dynamic_extent);
static_assert(std::is_same_v<::fixy::Site, ::foundation::core::Site>);
static_assert(std::is_same_v<::fixy::Fmt<>, ::foundation::core::Fmt<>>);
static_assert(noexcept(::fixy::fatal("a text of a report")) && noexcept(::fixy::unreachable()));
static_assert(std::is_same_v<::fixy::ParseError, ::foundation::core::ParseError>);
static_assert(::fixy::parse<int>("-12").is_ok() && ::fixy::parse<unsigned>("-12").is_err());
static_assert(::fixy::ParseError{::fixy::ParseErrorKind::overflow, 3}.position == 3);
// The Scalar family.
static_assert(::fixy::Integer<int> && !::fixy::Integer<bool> && ::fixy::Float<double> && ::fixy::Arithmetic<float>);
static_assert(::fixy::SignedInteger<long> && ::fixy::UnsignedInteger<unsigned char>);
static_assert(::fixy::fits_each_value<std::uint8_t, int> && !::fixy::fits_each_value<int, std::uint8_t>);
static_assert(::fixy::max_of<int> == 2147483647 && ::fixy::epsilon_of<double> > 0.0);
static_assert(::fixy::bits_of<std::uint16_t> == 16 && ::fixy::value_bits_of<std::int16_t> == 15);
static_assert(::fixy::min_of<std::int8_t> == -128 && ::fixy::gcd(12, 18) == 6u);
static_assert(std::is_same_v<::fixy::WideProduct<unsigned>, ::foundation::core::WideProduct<unsigned>>);
static_assert(::fixy::widening_mul(std::uint8_t{16}, std::uint8_t{16}).high == 1);
static_assert(::fixy::min(2, 3) == 2 && ::fixy::max(2, 3) == 3 && ::fixy::clamp(9, ::fixy::bounds<0, 5>()) == 5);
static_assert(std::is_same_v<::fixy::Bounds<int>, ::foundation::core::Bounds<int>>);
static_assert(std::is_lt(::fixy::compare(-1, 1u)) && ::fixy::fits<std::uint8_t>(255)
              && !::fixy::fits<std::uint8_t>(256));
static_assert(::fixy::unsigned_abs(-3) == 3u && ::fixy::abs_diff(2, 7) == 5u);
static_assert(::fixy::narrow<std::uint8_t>(300).is_none() && ::fixy::wrapping_narrow<std::uint8_t>(300) == 44);
static_assert(::fixy::saturating_narrow<std::uint8_t>(300) == 255 && ::fixy::widen<long>(3) == 3);
static_assert(::fixy::popcount(0xF0u) == 4 && ::fixy::countl_zero(std::uint8_t{1}) == 7 && ::fixy::bit_width(5u) == 3);
static_assert(::fixy::countr_zero(8u) == 3 && ::fixy::countl_one(0xF0000000u) == 4 && ::fixy::countr_one(7u) == 3);
static_assert(::fixy::has_single_bit(64u) && ::fixy::bit_floor(100u) == 64u && ::fixy::bit_ceil(100u).is_some());
static_assert(::fixy::rotl(std::uint8_t{0x81}, 1) == 3 && ::fixy::rotr(std::uint8_t{0x81}, 1) == 0xC0);
static_assert(::fixy::low_mask<std::uint16_t>(4) == 15 && ::fixy::byteswap(std::uint16_t{0x1234}) == 0x3412);

}  // namespace fixy::detail::core_checks
