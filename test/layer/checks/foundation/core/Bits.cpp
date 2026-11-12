// The compile-time checks of foundation/core/Bits.h.

#include <foundation/core/Bits.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::bits_checks {

template <class T>
[[nodiscard]] consteval bool holds(Option<T> result, T expected) {
    return result.is_some() && static_cast<Option<T>&&>(result).value_or(T{0}) == expected;
}

template <class T>
[[nodiscard]] consteval bool empty(Option<T> result) {
    return result.is_none();
}

// Each count of 0 is the width, and each count is an int.
static_assert(std::is_same_v<decltype(popcount(std::uint8_t{0})), int>);
static_assert(popcount(std::uint8_t{0}) == 0 && popcount(std::uint8_t{0xFF}) == 8);
static_assert(popcount(max_of<std::uint64_t>) == 64 && popcount(std::uint32_t{0x80000001u}) == 2);
static_assert(countl_zero(std::uint8_t{0}) == 8 && countl_zero(std::uint16_t{0}) == 16);
static_assert(countl_zero(std::uint32_t{0}) == 32 && countl_zero(std::uint64_t{0}) == 64);
static_assert(countr_zero(std::uint8_t{0}) == 8 && countr_zero(std::uint64_t{0}) == 64);
static_assert(countl_zero(std::uint8_t{1}) == 7 && countr_zero(std::uint8_t{0x80}) == 7);
static_assert(countl_zero(std::uint64_t{1} << 63) == 0 && countr_zero(std::uint64_t{1} << 63) == 63);
static_assert(countl_one(std::uint8_t{0xF0}) == 4 && countr_one(std::uint8_t{0x0F}) == 4);
static_assert(countl_one(max_of<std::uint16_t>) == 16 && countr_one(max_of<std::uint32_t>) == 32);
static_assert(countl_one(std::uint32_t{0}) == 0 && countr_one(std::uint64_t{0}) == 0);

static_assert(bit_width(std::uint8_t{0}) == 0 && bit_width(std::uint8_t{1}) == 1 && bit_width(std::uint8_t{255}) == 8);
static_assert(bit_width(std::uint64_t{1} << 40) == 41 && bit_width(max_of<std::uint64_t>) == 64);

static_assert(!has_single_bit(std::uint8_t{0}) && has_single_bit(std::uint8_t{1}) && has_single_bit(std::uint8_t{128}));
static_assert(!has_single_bit(std::uint8_t{3}) && !has_single_bit(max_of<std::uint64_t>));
static_assert(has_single_bit(std::uint64_t{1} << 63));

static_assert(bit_floor(std::uint8_t{0}) == 0 && bit_floor(std::uint8_t{1}) == 1
              && bit_floor(std::uint8_t{255}) == 128);
static_assert(bit_floor(max_of<std::uint64_t>) == std::uint64_t{1} << 63 && bit_floor(std::uint32_t{6}) == 4u);

// bit_ceil gives no value when the power is more than the maximum.
static_assert(holds(bit_ceil(std::uint8_t{0}), std::uint8_t{1}) && holds(bit_ceil(std::uint8_t{1}), std::uint8_t{1}));
static_assert(holds(bit_ceil(std::uint8_t{5}), std::uint8_t{8})
              && holds(bit_ceil(std::uint8_t{128}), std::uint8_t{128}));
static_assert(empty(bit_ceil(std::uint8_t{129})) && empty(bit_ceil(max_of<std::uint8_t>)));
static_assert(holds(bit_ceil(std::uint64_t{1} << 63), std::uint64_t{1} << 63));
static_assert(empty(bit_ceil((std::uint64_t{1} << 63) + 1)));

// A rotation takes each count modulo the width, and a negative count turns
// the other way.
static_assert(rotl(std::uint8_t{0x81}, 1) == 0x03 && rotr(std::uint8_t{0x81}, 1) == 0xC0);
static_assert(rotl(std::uint8_t{0x81}, 9) == 0x03 && rotl(std::uint8_t{0x81}, -1) == 0xC0);
static_assert(rotl(std::uint8_t{0x81}, 0) == 0x81 && rotl(std::uint8_t{0x81}, 8) == 0x81);
static_assert(rotl(std::uint64_t{1}, 63) == std::uint64_t{1} << 63
              && rotr(std::uint64_t{1}, 1) == std::uint64_t{1} << 63);
static_assert(rotl(std::uint32_t{0x12345678u}, min_of<int>) == 0x12345678u);
static_assert(rotr(std::uint32_t{0x12345678u}, min_of<int>) == 0x12345678u);
static_assert(rotl(std::uint16_t{0x8001}, max_of<int>) == rotr(std::uint16_t{0x8001}, 1));

static_assert(low_mask<std::uint8_t>(0) == 0 && low_mask<std::uint8_t>(-5) == 0 && low_mask<std::uint8_t>(3) == 7);
static_assert(low_mask<std::uint8_t>(8) == 255 && low_mask<std::uint8_t>(max_of<int>) == 255);
static_assert(low_mask<std::uint64_t>(63) == max_of<std::uint64_t> >> 1);
static_assert(low_mask<std::uint64_t>(64) == max_of<std::uint64_t> && low_mask<std::uint64_t>(min_of<int>) == 0);

// byteswap takes a signed value too, and one byte does not change.
static_assert(byteswap(std::uint8_t{0xAB}) == 0xAB && byteswap(std::int8_t{-2}) == -2);
static_assert(byteswap(std::uint16_t{0x1234}) == 0x3412 && byteswap(std::uint32_t{0x12345678u}) == 0x78563412u);
static_assert(byteswap(std::uint64_t{0x0102030405060708u}) == std::uint64_t{0x0807060504030201u});
static_assert(byteswap(std::int16_t{0x0080}) == min_of<std::int16_t> && byteswap(-1) == -1);

}  // namespace foundation::core::detail::bits_checks
