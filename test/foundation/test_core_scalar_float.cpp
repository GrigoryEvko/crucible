// Tests of foundation/core/Float.h at run time.  Each class, sign_bit, abs,
// copy_sign and total_key is compared with a model that reads the bits of
// the value, over Philox bit patterns of float and double and over each
// edge pattern: each sign, 0, the subnormal values, the normal limits, the
// infinities and the NaNs of each payload.  total_key is one to one, so a
// compare of two keys is a compare of two bit patterns.  sqrt gives the
// canonical NaN for each negative value and each NaN, and fma rounds one
// time.  The differential test against the standard library is
// test_core_scalar_oracle.cpp.

#include <foundation/core/Array.h>
#include <foundation/core/Float.h>
#include <foundation/core/Scalar.h>

#include <foundation/Platform.h>

#include "philox_stream.h"

#include <cstdint>

namespace {

namespace core = ::foundation::core;
namespace fp = ::foundation::core::fp;

// The value, through a call that the optimizer cannot read, so each
// operation runs at run time.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

// The layout of the bits of a Float.
template <class T>
struct Layout;

template <>
struct Layout<float> {
    using Word = std::uint32_t;
    static constexpr int significand = 23;
    static constexpr Word exponent_mask = 0xFFu;
    static constexpr Word quiet_nan = 0x7FC00000u;
};

template <>
struct Layout<double> {
    using Word = std::uint64_t;
    static constexpr int significand = 52;
    static constexpr Word exponent_mask = 0x7FFu;
    static constexpr Word quiet_nan = 0x7FF8000000000000u;
};

template <class T>
using WordOf = typename Layout<T>::Word;

template <class T>
inline constexpr WordOf<T> sign_mask = static_cast<WordOf<T>>(WordOf<T>{1} << (core::bits_of<T> - 1));

// The value of a bit pattern.  Each bit pattern of a Float is a value.
template <class T>
[[nodiscard]] T value_of(WordOf<T> bits) {
    return core::decode<T>(bits).expect("each bit pattern of a Float is a value");
}

// The key of the total order of a bit pattern: each bit flipped for a
// negative pattern, and the sign bit set for each other pattern.
template <class T>
[[nodiscard]] WordOf<T> model_key(WordOf<T> bits) noexcept {
    if ((bits & sign_mask<T>) != 0) return static_cast<WordOf<T>>(~bits);
    return static_cast<WordOf<T>>(bits | sign_mask<T>);
}

template <class T>
[[nodiscard]] WordOf<T> exponent_of(WordOf<T> bits) noexcept {
    return static_cast<WordOf<T>>((bits >> Layout<T>::significand) & Layout<T>::exponent_mask);
}

template <class T>
[[nodiscard]] WordOf<T> fraction_of(WordOf<T> bits) noexcept {
    return static_cast<WordOf<T>>(bits & static_cast<WordOf<T>>((WordOf<T>{1} << Layout<T>::significand) - 1));
}

template <class T>
void check_bits(WordOf<T> bits, WordOf<T> other) {
    T const value = value_of<T>(bits);
    WordOf<T> const exponent = exponent_of<T>(bits);
    WordOf<T> const fraction = fraction_of<T>(bits);
    bool const is_top = exponent == Layout<T>::exponent_mask;
    bool const is_nan = is_top && fraction != 0;
    CRUCIBLE_FATAL_INVARIANT(fp::total_key(opaque(value)) == model_key<T>(bits));
    CRUCIBLE_FATAL_INVARIANT(fp::is_nan(opaque(value)) == is_nan);
    CRUCIBLE_FATAL_INVARIANT(fp::is_inf(opaque(value)) == (is_top && fraction == 0));
    CRUCIBLE_FATAL_INVARIANT(fp::is_finite(opaque(value)) == !is_top);
    CRUCIBLE_FATAL_INVARIANT(fp::is_normal(opaque(value)) == (exponent != 0 && !is_top));
    CRUCIBLE_FATAL_INVARIANT(fp::sign_bit(opaque(value)) == ((bits & sign_mask<T>) != 0));

    // abs and copy_sign change only the sign bit, also of a NaN.
    WordOf<T> const magnitude = static_cast<WordOf<T>>(bits & static_cast<WordOf<T>>(~sign_mask<T>));
    WordOf<T> const signed_copy = static_cast<WordOf<T>>(magnitude | (other & sign_mask<T>));
    CRUCIBLE_FATAL_INVARIANT(fp::total_key(fp::abs(opaque(value))) == model_key<T>(magnitude));
    CRUCIBLE_FATAL_INVARIANT(fp::total_key(fp::copy_sign(opaque(value), opaque(value_of<T>(other))))
                             == model_key<T>(signed_copy));

    // sqrt: the canonical NaN for each NaN and each value below -0.
    bool const is_negative = (bits & sign_mask<T>) != 0 && magnitude != 0;
    if (is_nan || is_negative) {
        CRUCIBLE_FATAL_INVARIANT(fp::total_key(fp::sqrt(opaque(value))) == model_key<T>(Layout<T>::quiet_nan));
    } else {
        CRUCIBLE_FATAL_INVARIANT(!fp::is_nan(fp::sqrt(opaque(value))));
    }
}

// Each edge pattern of the type, with each sign.
template <class T>
void for_each_edge(auto&& body) {
    WordOf<T> const top = static_cast<WordOf<T>>(Layout<T>::exponent_mask << Layout<T>::significand);
    WordOf<T> const fraction = static_cast<WordOf<T>>((WordOf<T>{1} << Layout<T>::significand) - 1);
    WordOf<T> const quiet_bit = static_cast<WordOf<T>>(WordOf<T>{1} << (Layout<T>::significand - 1));
    WordOf<T> const one = static_cast<WordOf<T>>(WordOf<T>{1} << Layout<T>::significand);
    core::FixedArray<WordOf<T>, 10> const patterns{WordOf<T>{0},
                                                   WordOf<T>{1},
                                                   fraction,
                                                   one,
                                                   static_cast<WordOf<T>>(top - 1),
                                                   top,
                                                   static_cast<WordOf<T>>(top | 1u),
                                                   static_cast<WordOf<T>>(top | quiet_bit),
                                                   static_cast<WordOf<T>>(top | fraction),
                                                   static_cast<WordOf<T>>(top | quiet_bit | 1u)};
    for (WordOf<T> const pattern : patterns) {
        body(pattern);
        body(static_cast<WordOf<T>>(pattern | sign_mask<T>));
    }
}

template <class T>
void test_each_operation_of_the_bits() {
    ::crucible::test::PhiloxStream stream{sizeof(T) == 4 ? 60u : 61u};
    auto const check_against_each_edge = [](WordOf<T> edge) {
        for_each_edge<T>([edge](WordOf<T> other) { check_bits<T>(edge, other); });
    };
    for_each_edge<T>(check_against_each_edge);
    for (int index = 0; index < 200000; ++index) {
        auto const bits = static_cast<WordOf<T>>(stream.next_wide());
        auto const other = static_cast<WordOf<T>>(stream.next_wide());
        check_bits<T>(bits, other);
        // A pattern with the top exponent reaches the infinities and the
        // NaNs, which a uniform draw almost never gives.
        WordOf<T> const top = static_cast<WordOf<T>>(Layout<T>::exponent_mask << Layout<T>::significand);
        check_bits<T>(static_cast<WordOf<T>>(bits | top), other);
        check_bits<T>(static_cast<WordOf<T>>(bits & static_cast<WordOf<T>>(~top)), other);
    }
}

// The key keeps the order of two numbers.  -0 and +0 compare equal, and
// the key puts -0 first.
template <class T>
void test_total_key_keeps_the_order() {
    ::crucible::test::PhiloxStream stream{sizeof(T) == 4 ? 62u : 63u};
    for (int index = 0; index < 200000; ++index) {
        T const left = value_of<T>(static_cast<WordOf<T>>(stream.next_wide()));
        T const right = value_of<T>(static_cast<WordOf<T>>(stream.next_wide()));
        if (fp::is_nan(left) || fp::is_nan(right)) continue;
        if (left < right) CRUCIBLE_FATAL_INVARIANT(fp::total_key(opaque(left)) < fp::total_key(opaque(right)));
        if (right < left) CRUCIBLE_FATAL_INVARIANT(fp::total_key(opaque(right)) < fp::total_key(opaque(left)));
    }
    CRUCIBLE_FATAL_INVARIANT(fp::total_key(opaque(-T{0})) < fp::total_key(opaque(T{0})));
}

// sqrt of a square is exact, sqrt of +inf is +inf, and sqrt keeps -0.
template <class T>
void test_sqrt_of_squares_and_limits() {
    ::crucible::test::PhiloxStream stream{sizeof(T) == 4 ? 64u : 65u};
    std::uint64_t const bound = sizeof(T) == 4 ? (std::uint64_t{1} << 12) : (std::uint64_t{1} << 26);
    for (int index = 0; index < 100000; ++index) {
        auto const root = static_cast<T>(stream.below(bound));
        CRUCIBLE_FATAL_INVARIANT(fp::total_key(fp::sqrt(opaque(root * root))) == fp::total_key(root));
    }
    CRUCIBLE_FATAL_INVARIANT(fp::total_key(fp::sqrt(opaque(core::infinity_of<T>)))
                             == fp::total_key(core::infinity_of<T>));
    CRUCIBLE_FATAL_INVARIANT(fp::total_key(fp::sqrt(opaque(-T{0}))) == fp::total_key(-T{0}));
    CRUCIBLE_FATAL_INVARIANT(fp::total_key(fp::sqrt(opaque(-core::infinity_of<T>)))
                             == fp::total_key(core::quiet_nan_of<T>));
}

// fma of small integers is exact, an fma that overflows gives an infinity,
// and the one rounding keeps the low bits of a product.
void test_fma_rounds_one_time() {
    ::crucible::test::PhiloxStream stream{66};
    for (int index = 0; index < 100000; ++index) {
        auto const left = static_cast<std::int64_t>(stream.below(1u << 21)) - (1 << 20);
        auto const right = static_cast<std::int64_t>(stream.below(1u << 21)) - (1 << 20);
        auto const addend = static_cast<std::int64_t>(stream.below(1u << 21)) - (1 << 20);
        double const exact = static_cast<double>(left * right + addend);
        double const fused =
            fp::fma(opaque(static_cast<double>(left)), opaque(static_cast<double>(right)), static_cast<double>(addend));
        CRUCIBLE_FATAL_INVARIANT(fp::total_key(fused) == fp::total_key(exact));
    }
    CRUCIBLE_FATAL_INVARIANT(fp::total_key(fp::fma(opaque(0.1), opaque(10.0), -1.0)) == fp::total_key(0x1p-54));
    CRUCIBLE_FATAL_INVARIANT(fp::total_key(fp::fma(opaque(1e300), opaque(1e300), 0.0))
                             == fp::total_key(core::infinity_of<double>));
    CRUCIBLE_FATAL_INVARIANT(fp::total_key(fp::fma(opaque(3.0f), opaque(4.0f), 5.0f)) == fp::total_key(17.0f));
}

}  // namespace

int main() {
    test_each_operation_of_the_bits<float>();
    test_each_operation_of_the_bits<double>();
    test_total_key_keeps_the_order<float>();
    test_total_key_keeps_the_order<double>();
    test_sqrt_of_squares_and_limits<float>();
    test_sqrt_of_squares_and_limits<double>();
    test_fma_rounds_one_time();
    return 0;
}
