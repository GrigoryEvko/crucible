// The compile-time checks of foundation/core/StorageFloat.h.  Each conversion
// is constexpr, so each boundary case of each format holds at compile time:
// the ties at the overflow and at the subnormals, the infinities, and the
// NaN rule of each direction.

#include <foundation/core/StorageFloat.h>

#include <foundation/core/Scalar.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::storage_float_checks {

[[nodiscard]] consteval std::uint32_t widen_f16(std::uint16_t word) {
    return bit_cast<std::uint32_t>(widen<float>(f16::from_bits(word)));
}

[[nodiscard]] consteval std::uint32_t widen_bf16(std::uint16_t word) {
    return bit_cast<std::uint32_t>(widen<float>(bf16::from_bits(word)));
}

[[nodiscard]] consteval std::uint32_t widen_e4m3(std::uint8_t word) {
    return bit_cast<std::uint32_t>(widen<float>(fp8_e4m3::from_bits(word)));
}

[[nodiscard]] consteval std::uint32_t widen_e5m2(std::uint8_t word) {
    return bit_cast<std::uint32_t>(widen<float>(fp8_e5m2::from_bits(word)));
}

template <class T>
[[nodiscard]] consteval std::uint64_t widen_wide(typename T::Word word) {
    return bit_cast<std::uint64_t>(widen<double>(T::from_bits(word)));
}

template <class To>
[[nodiscard]] consteval typename To::Word nonsaturating(std::uint32_t bits) {
    return round_nonsaturating<To>(bit_cast<float>(bits)).raw();
}

template <class To>
[[nodiscard]] consteval typename To::Word saturating(std::uint32_t bits) {
    return round_saturating<To>(bit_cast<float>(bits)).raw();
}

// The four types hold their bits and nothing else.  Each bit pattern is a
// value, so bit_cast reads one from bytes.
static_assert(sizeof(f16) == 2 && sizeof(bf16) == 2 && sizeof(fp8_e4m3) == 1 && sizeof(fp8_e5m2) == 1);
static_assert(alignof(f16) == 2 && alignof(fp8_e4m3) == 1);
static_assert(std::is_trivially_copyable_v<f16> && std::is_trivially_copyable_v<fp8_e5m2>);
static_assert(BitValid<f16> && BitValid<bf16> && BitValid<fp8_e4m3> && BitValid<fp8_e5m2>);
static_assert(DecodableFrom<f16, std::uint16_t> && DecodableFrom<fp8_e4m3, std::uint8_t>);
static_assert(f16{}.raw() == 0 && fp8_e5m2{}.raw() == 0);
static_assert(StorageFloat<f16> && StorageFloat<fp8_e5m2> && !StorageFloat<float> && !StorageFloat<std::uint16_t>);

// No implicit conversion, no constructor from a word, no compare and no
// arithmetic.
static_assert(!std::is_convertible_v<float, f16> && !std::is_convertible_v<f16, float>);
static_assert(!std::is_constructible_v<f16, std::uint16_t> && !std::is_constructible_v<bf16, float>);
static_assert(!std::equality_comparable<f16> && !std::equality_comparable<fp8_e4m3>);
template <class T>
concept Adds = requires(T value) { value + value; };
static_assert(!Adds<f16> && !Adds<bf16> && !Adds<fp8_e4m3> && !Adds<fp8_e5m2>);

// A round takes a float only, because a double would round two times.  The
// fixture neg_core_storage_float_round_double shows the refusal of a double.

// The widen of f16.  A subnormal becomes a normal binary32, and a signaling
// NaN becomes quiet with its payload.
static_assert(widen_f16(0x0000) == 0x00000000u && widen_f16(0x8000) == 0x80000000u);
static_assert(widen_f16(0x3C00) == 0x3F800000u && widen_f16(0xC000) == 0xC0000000u);
static_assert(widen_f16(0x0001) == 0x33800000u && widen_f16(0x03FF) == 0x387FC000u);
static_assert(widen_f16(0x0400) == 0x38800000u && widen_f16(0x7BFF) == 0x477FE000u);
static_assert(widen_f16(0x7C00) == 0x7F800000u && widen_f16(0xFC00) == 0xFF800000u);
static_assert(widen_f16(0x7C01) == 0x7FC02000u && widen_f16(0xFE00) == 0xFFC00000u && widen_f16(0x7FFF) == 0x7FFFE000u);

// The widen of bf16 is the shift, so a signaling NaN stays signaling.
static_assert(widen_bf16(0x3F80) == 0x3F800000u && widen_bf16(0x0001) == 0x00010000u);
static_assert(widen_bf16(0x7F7F) == 0x7F7F0000u && widen_bf16(0xFF80) == 0xFF800000u);
static_assert(widen_bf16(0x7F81) == 0x7F810000u && widen_bf16(0xFFC1) == 0xFFC10000u);

// The widen of fp8_e4m3: 448 is the highest value, each code of the top
// exponent other than S.1111.111 is a number, and the NaN keeps its sign.
static_assert(widen_e4m3(0x7E) == 0x43E00000u && widen_e4m3(0x78) == 0x43800000u && widen_e4m3(0xFE) == 0xC3E00000u);
static_assert(widen_e4m3(0x01) == 0x3B000000u && widen_e4m3(0x07) == 0x3C600000u && widen_e4m3(0x08) == 0x3C800000u);
static_assert(widen_e4m3(0x7F) == 0x7FF00000u && widen_e4m3(0xFF) == 0xFFF00000u && widen_e4m3(0x80) == 0x80000000u);

// The widen of fp8_e5m2: the top byte of an f16.
static_assert(widen_e5m2(0x7B) == 0x47600000u && widen_e5m2(0x7C) == 0x7F800000u && widen_e5m2(0xFC) == 0xFF800000u);
static_assert(widen_e5m2(0x7D) == 0x7FE00000u && widen_e5m2(0x7E) == 0x7FC00000u && widen_e5m2(0x7F) == 0x7FE00000u);
static_assert(widen_e5m2(0x01) == 0x37800000u && widen_e5m2(0x04) == 0x38800000u && widen_e5m2(0x3C) == 0x3F800000u);

// The widen to double keeps a subnormal of bf16 and a signaling NaN.
static_assert(widen_wide<f16>(0x3C00) == 0x3FF0000000000000u && widen_wide<bf16>(0x0001) == 0x37A0000000000000u);
static_assert(widen_wide<bf16>(0x8001) == 0xB7A0000000000000u && widen_wide<bf16>(0x7F81) == 0x7FF0200000000000u);
static_assert(widen_wide<fp8_e4m3>(0x7E) == 0x407C000000000000u && widen_wide<fp8_e5m2>(0x7B) == 0x40EC000000000000u);

// The round of f16.  65520 is the tie of the highest value and 65536, and the
// highest code is odd, so the tie overflows.  2^-25 is the tie of 0 and the
// lowest subnormal, and it rounds to 0.
static_assert(nonsaturating<f16>(0x477FE000u) == 0x7BFF && nonsaturating<f16>(0x477FEFFFu) == 0x7BFF);
static_assert(nonsaturating<f16>(0x477FF000u) == 0x7C00 && saturating<f16>(0x477FF000u) == 0x7BFF);
static_assert(nonsaturating<f16>(0x7F800000u) == 0x7C00 && saturating<f16>(0xFF800000u) == 0xFBFF);
static_assert(nonsaturating<f16>(0x7F7FFFFFu) == 0x7C00 && saturating<f16>(0x7F7FFFFFu) == 0x7BFF);
static_assert(nonsaturating<f16>(0x33000000u) == 0x0000 && nonsaturating<f16>(0x33000001u) == 0x0001);
static_assert(nonsaturating<f16>(0xB3000000u) == 0x8000 && nonsaturating<f16>(0x00000001u) == 0x0000);
static_assert(nonsaturating<f16>(0x33C00000u) == 0x0002 && nonsaturating<f16>(0x34200000u) == 0x0002);
static_assert(nonsaturating<f16>(0x387FE000u) == 0x0400 && nonsaturating<f16>(0x387FDFFFu) == 0x03FF);
static_assert(nonsaturating<f16>(0x3F801000u) == 0x3C00 && nonsaturating<f16>(0x3F803000u) == 0x3C02);
// A NaN keeps its sign and the top ten bits of its significand, with the
// quiet bit set.
static_assert(nonsaturating<f16>(0x7FC12345u) == 0x7E09 && nonsaturating<f16>(0x7F800001u) == 0x7E00);
static_assert(nonsaturating<f16>(0xFFC00000u) == 0xFE00 && saturating<f16>(0x7FFFFFFFu) == 0x7FFF);

// The round of bf16 adds the bias to the low 16 bits, but a NaN comes
// first: 0x7FFFFFFF would carry into the sign bit.
static_assert(nonsaturating<bf16>(0x3F808000u) == 0x3F80 && nonsaturating<bf16>(0x3F818000u) == 0x3F82);
static_assert(nonsaturating<bf16>(0x3F808001u) == 0x3F81 && nonsaturating<bf16>(0x00018000u) == 0x0002);
static_assert(nonsaturating<bf16>(0x00012345u) == 0x0001 && nonsaturating<bf16>(0x00008000u) == 0x0000);
static_assert(nonsaturating<bf16>(0x7F7F8000u) == 0x7F80 && saturating<bf16>(0x7F7F8000u) == 0x7F7F);
static_assert(nonsaturating<bf16>(0x7F7F7FFFu) == 0x7F7F && saturating<bf16>(0xFF800000u) == 0xFF7F);
static_assert(nonsaturating<bf16>(0x7FFFFFFFu) == 0x7FFF && nonsaturating<bf16>(0xFFFFFFFFu) == 0xFFFF);
static_assert(nonsaturating<bf16>(0x7F800001u) == 0x7FC0 && nonsaturating<bf16>(0xFFA00000u) == 0xFFE0);

// The round of fp8_e4m3.  464 is the tie of 448 and 480, and the highest
// code 0x7E is even, so 464 stays at 448.  Above 464 is an overflow: a NaN
// with no saturation, and 448 with it.  An infinity overflows too.
static_assert(nonsaturating<fp8_e4m3>(0x43E00000u) == 0x7E && nonsaturating<fp8_e4m3>(0x43E80000u) == 0x7E);
static_assert(saturating<fp8_e4m3>(0x43E80000u) == 0x7E && nonsaturating<fp8_e4m3>(0x43E80001u) == 0x7F);
static_assert(saturating<fp8_e4m3>(0x43E80001u) == 0x7E && nonsaturating<fp8_e4m3>(0xC3F00000u) == 0xFF);
static_assert(nonsaturating<fp8_e4m3>(0x7F800000u) == 0x7F && saturating<fp8_e4m3>(0x7F800000u) == 0x7E);
static_assert(nonsaturating<fp8_e4m3>(0xFF800000u) == 0xFF && saturating<fp8_e4m3>(0xFF800000u) == 0xFE);
static_assert(nonsaturating<fp8_e4m3>(0x7FC00000u) == 0x7F && saturating<fp8_e4m3>(0xFF800001u) == 0xFF);
// 2^-10 is the tie of 0 and the lowest subnormal 2^-9, and 1.5 * 2^-9 is the
// tie of the subnormals 1 and 2.
static_assert(nonsaturating<fp8_e4m3>(0x3A800000u) == 0x00 && nonsaturating<fp8_e4m3>(0x3A800001u) == 0x01);
static_assert(nonsaturating<fp8_e4m3>(0x3B400000u) == 0x02 && nonsaturating<fp8_e4m3>(0x3C700000u) == 0x08);
static_assert(nonsaturating<fp8_e4m3>(0x3F880000u) == 0x38 && nonsaturating<fp8_e4m3>(0x3F980000u) == 0x3A);

// The round of fp8_e5m2.  61440 is the tie of 57344 and 65536, and the
// highest code 0x7B is odd, so the tie overflows.
static_assert(nonsaturating<fp8_e5m2>(0x47600000u) == 0x7B && nonsaturating<fp8_e5m2>(0x476FFFFFu) == 0x7B);
static_assert(nonsaturating<fp8_e5m2>(0x47700000u) == 0x7C && saturating<fp8_e5m2>(0x47700000u) == 0x7B);
static_assert(nonsaturating<fp8_e5m2>(0xFF800000u) == 0xFC && saturating<fp8_e5m2>(0xFF800000u) == 0xFB);
static_assert(nonsaturating<fp8_e5m2>(0x37000000u) == 0x00 && nonsaturating<fp8_e5m2>(0x37000001u) == 0x01);
static_assert(nonsaturating<fp8_e5m2>(0x7FC00000u) == 0x7E && nonsaturating<fp8_e5m2>(0x7FE00000u) == 0x7F);
static_assert(nonsaturating<fp8_e5m2>(0xFF800001u) == 0xFE && saturating<fp8_e5m2>(0x7FFFFFFFu) == 0x7F);

}  // namespace foundation::core::detail::storage_float_checks
