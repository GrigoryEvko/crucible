#pragma once

// The exact oracle of the tests of the products, the sums and the fma: an
// exact sum of floats and of products of two floats, with one round at the
// end.
//
// ExactSum<T> holds a fixed-point integer of two's complement in words of 64
// bits.  Bit 0 has the weight of the last bit of the product of two smallest
// subnormal values, and the top word leaves 64 bits above the largest
// product, so a sum of up to 2^62 terms does not wrap.  So each add is exact,
// and rounded_as<Out>() rounds the exact sum one time, to nearest even, into
// a float or a double.  An exact sum that is not zero and rounds to zero
// keeps its sign.  An exact sum of zero gives +0, and the caller states the
// sign of a sum of zeros.  A NaN term, a product of an infinity and a zero,
// and two infinities of the two signs give a NaN, and each other infinity
// gives itself.
//
// The oracle shares no code with the code under test: it reads the bits of
// each term, and it adds integers.  An add costs O(1) words, and a round
// O(words): 10 words for a float and 68 words for a double.

#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>

namespace crucible::test {

// The layout of the bits of a float or a double.
template <class T>
struct ExactLayout;

template <>
struct ExactLayout<float> {
    using Word = std::uint32_t;
    static constexpr int precision = 24;
    static constexpr int exponent_bits = 8;
    static constexpr int lowest_exponent = -149;
    static constexpr int highest_exponent = 104;
};

template <>
struct ExactLayout<double> {
    using Word = std::uint64_t;
    static constexpr int precision = 53;
    static constexpr int exponent_bits = 11;
    static constexpr int lowest_exponent = -1074;
    static constexpr int highest_exponent = 971;
};

__extension__ using ExactWide = unsigned __int128;

// A finite value as a sign, an integer significand and the exponent of its
// last bit.
struct ExactTerm {
    ExactWide significand = 0;
    int exponent = 0;
    bool is_negative = false;
};

template <class T>
[[nodiscard]] ExactTerm exact_term_of(T value) noexcept {
    using Layout = ExactLayout<T>;
    using Word = typename Layout::Word;
    constexpr int fraction_bits = Layout::precision - 1;
    auto const bits = ::fixy::bit_cast<Word>(value);
    Word const fraction = static_cast<Word>(bits & ((Word{1} << fraction_bits) - 1));
    auto const biased = static_cast<int>((bits >> fraction_bits) & ((Word{1} << Layout::exponent_bits) - 1));
    bool const is_negative = (bits >> (Layout::precision + Layout::exponent_bits - 1)) != 0;
    if (biased == 0) return ExactTerm{fraction, Layout::lowest_exponent, is_negative};
    return ExactTerm{ExactWide{fraction} | (ExactWide{1} << fraction_bits), Layout::lowest_exponent + biased - 1,
                     is_negative};
}

template <class T>
class ExactSum {
    using Layout = ExactLayout<T>;

public:
    // The weight of bit 0, and the count of the words.
    static constexpr int origin = 2 * Layout::lowest_exponent;
    static constexpr int top_bit = 2 * (Layout::highest_exponent + Layout::precision) + 64 - origin;
    static constexpr std::size_t words = static_cast<std::size_t>(top_bit / 64 + 1);

    // Adds value.
    void add(T value) noexcept {
        if (!take_special(value)) return;
        if (is_zero(value)) return;
        add_term(exact_term_of(value));
    }

    // Adds the exact product left * right.
    void add_product(T left, T right) noexcept {
        bool const is_left_finite = ::fixy::fp::is_finite(left);
        bool const is_right_finite = ::fixy::fp::is_finite(right);
        if (::fixy::fp::is_nan(left) || ::fixy::fp::is_nan(right)) {
            has_nan_ = true;
            return;
        }
        if (!is_left_finite || !is_right_finite) {
            if (is_zero(left) || is_zero(right)) {
                has_nan_ = true;
                return;
            }
            bool const is_negative = ::fixy::fp::sign_bit(left) != ::fixy::fp::sign_bit(right);
            (is_negative ? has_negative_infinity_ : has_positive_infinity_) = true;
            return;
        }
        if (is_zero(left) || is_zero(right)) return;
        ExactTerm const first = exact_term_of(left);
        ExactTerm const second = exact_term_of(right);
        add_term(ExactTerm{first.significand * second.significand, first.exponent + second.exponent,
                           first.is_negative != second.is_negative});
    }

    // Adds the exact sum other.
    void add_sum(ExactSum const& other) noexcept {
        has_nan_ = has_nan_ || other.has_nan_;
        has_positive_infinity_ = has_positive_infinity_ || other.has_positive_infinity_;
        has_negative_infinity_ = has_negative_infinity_ || other.has_negative_infinity_;
        std::uint64_t carry = 0;
        for (std::size_t word = 0; word < words; ++word) {
            std::uint64_t const left =
                *words_.get(word).expect("a test reads an element at or past the count of its array");
            std::uint64_t const right =
                *other.words_.get(word).expect("a test reads an element at or past the count of its array");
            std::uint64_t const partial = left + right;
            std::uint64_t const total = partial + carry;
            carry = (partial < left ? 1u : 0u) + (total < partial ? 1u : 0u);
            *words_.get(word).expect("a test reads an element at or past the count of its array") = total;
        }
    }

    // True when the sum holds a NaN or two infinities of the two signs.
    [[nodiscard]] bool is_nan() const noexcept {
        return has_nan_ || (has_positive_infinity_ && has_negative_infinity_);
    }

    // True when the finite part of the sum is exactly zero.
    [[nodiscard]] bool is_exact_zero() const noexcept {
        for (std::uint64_t const word : words_) {
            if (word != 0) return false;
        }
        return true;
    }

    // The exact sum rounded to nearest even into Out, a float or a double.
    template <class Out>
    [[nodiscard]] Out rounded_as() const noexcept {
        using OutLayout = ExactLayout<Out>;
        using OutWord = typename OutLayout::Word;
        constexpr int fraction_bits = OutLayout::precision - 1;
        constexpr OutWord sign_word = OutWord{1} << (OutLayout::precision + OutLayout::exponent_bits - 1);
        constexpr OutWord infinity =
            static_cast<OutWord>(((OutWord{1} << OutLayout::exponent_bits) - 1) << fraction_bits);
        if (is_nan()) return ::fixy::quiet_nan_of<Out>;
        if (has_positive_infinity_) return ::fixy::bit_cast<Out>(infinity);
        if (has_negative_infinity_) return ::fixy::bit_cast<Out>(static_cast<OutWord>(sign_word | infinity));
        ::fixy::FixedArray<std::uint64_t, words> magnitude = words_;
        bool const is_negative =
            (*words_.get(words - 1).expect("a test reads an element at or past the count of its array") >> 63) != 0;
        if (is_negative) negate(magnitude);
        int highest = -1;
        for (std::size_t word = words; word-- > 0;) {
            std::uint64_t const value =
                *magnitude.get(word).expect("a test reads an element at or past the count of its array");
            if (value != 0) {
                highest = static_cast<int>(word) * 64 + 63 - ::fixy::countl_zero(value);
                break;
            }
        }
        OutWord const sign = is_negative ? sign_word : OutWord{0};
        if (highest < 0) return ::fixy::bit_cast<Out>(OutWord{0});
        // The bit of the last kept bit: the precision below the highest bit,
        // or the last bit of a subnormal value of Out.
        int last = highest - (OutLayout::precision - 1);
        if (last + origin < OutLayout::lowest_exponent) last = OutLayout::lowest_exponent - origin;
        std::uint64_t kept = 0;
        if (last <= 0) {
            kept = bits_at(magnitude, 0, highest + 1) << -last;
        } else {
            kept = bits_at(magnitude, last, highest - last + 1);
            bool const round_bit = bit_at(magnitude, last - 1);
            bool const has_sticky = any_below(magnitude, last - 1);
            if (round_bit && (has_sticky || (kept & 1u) != 0)) ++kept;
        }
        int exponent = last + origin;
        if ((kept >> OutLayout::precision) != 0) {
            kept >>= 1;
            ++exponent;
        }
        if (kept < (std::uint64_t{1} << fraction_bits)) {
            return ::fixy::bit_cast<Out>(static_cast<OutWord>(sign | static_cast<OutWord>(kept)));
        }
        int const biased = exponent - OutLayout::lowest_exponent + 1;
        if (biased >= (1 << OutLayout::exponent_bits) - 1) {
            return ::fixy::bit_cast<Out>(static_cast<OutWord>(sign | infinity));
        }
        OutWord const fraction = static_cast<OutWord>(kept - (std::uint64_t{1} << fraction_bits));
        return ::fixy::bit_cast<Out>(
            static_cast<OutWord>(sign | (static_cast<OutWord>(biased) << fraction_bits) | fraction));
    }

private:
    using Words = ::fixy::FixedArray<std::uint64_t, words>;

    [[nodiscard]] static bool is_zero(T value) noexcept {
        using Word = typename Layout::Word;
        return static_cast<Word>(::fixy::bit_cast<Word>(value) << 1) == Word{0};
    }

    // Records a NaN or an infinity, and gives true for a finite value.
    [[nodiscard]] bool take_special(T value) noexcept {
        if (::fixy::fp::is_nan(value)) {
            has_nan_ = true;
            return false;
        }
        if (!::fixy::fp::is_finite(value)) {
            (::fixy::fp::sign_bit(value) ? has_negative_infinity_ : has_positive_infinity_) = true;
            return false;
        }
        return true;
    }

    // Adds or subtracts the term at the bit of its exponent.
    void add_term(ExactTerm const& term) noexcept {
        auto const position = static_cast<std::size_t>(term.exponent - origin);
        std::size_t const first = position / 64;
        std::size_t const shift = position % 64;
        // The shifted significand spans at most three words.
        ::fixy::FixedArray<std::uint64_t, 3> part{};
        auto const low = static_cast<std::uint64_t>(term.significand);
        auto const high = static_cast<std::uint64_t>(term.significand >> 64);
        *part.get(0).expect("a test reads an element at or past the count of its array") = low << shift;
        *part.get(1).expect("a test reads an element at or past the count of its array") =
            shift == 0 ? high : (high << shift) | (low >> (64 - shift));
        *part.get(2).expect("a test reads an element at or past the count of its array") =
            shift == 0 ? 0 : high >> (64 - shift);
        std::uint64_t carry = 0;
        for (std::size_t word = first; word < words; ++word) {
            std::size_t const offset = word - first;
            std::uint64_t const value =
                offset < 3 ? *part.get(offset).expect("a test reads an element at or past the count of its array") : 0;
            if (offset >= 3 && carry == 0) break;
            std::uint64_t const current =
                *words_.get(word).expect("a test reads an element at or past the count of its array");
            if (term.is_negative) {
                std::uint64_t const partial = current - value;
                std::uint64_t const total = partial - carry;
                carry = (current < value ? 1u : 0u) + (partial < carry ? 1u : 0u);
                *words_.get(word).expect("a test reads an element at or past the count of its array") = total;
            } else {
                std::uint64_t const partial = current + value;
                std::uint64_t const total = partial + carry;
                carry = (partial < current ? 1u : 0u) + (total < partial ? 1u : 0u);
                *words_.get(word).expect("a test reads an element at or past the count of its array") = total;
            }
        }
    }

    static void negate(Words& value) noexcept {
        std::uint64_t carry = 1;
        for (std::uint64_t& word : value) {
            std::uint64_t const flipped = ~word;
            word = flipped + carry;
            carry = (carry != 0 && word == 0) ? 1u : 0u;
        }
    }

    [[nodiscard]] static bool bit_at(Words const& value, int bit) noexcept {
        auto const position = static_cast<std::size_t>(bit);
        return ((*value.get(position / 64).expect("a test reads an element at or past the count of its array")
                 >> (position % 64))
                & 1u)
            != 0;
    }

    // The count bits from the bit first, for a count of at most 64.
    [[nodiscard]] static std::uint64_t bits_at(Words const& value, int first, int count) noexcept {
        std::uint64_t result = 0;
        for (int bit = count; bit-- > 0;) {
            result = (result << 1) | (bit_at(value, first + bit) ? 1u : 0u);
        }
        return result;
    }

    // True when a bit below the bit limit is set.
    [[nodiscard]] static bool any_below(Words const& value, int limit) noexcept {
        auto const position = static_cast<std::size_t>(limit);
        std::size_t const word = position / 64;
        std::size_t const shift = position % 64;
        for (std::size_t lower = 0; lower < word; ++lower) {
            if (*value.get(lower).expect("a test reads an element at or past the count of its array") != 0) return true;
        }
        if (shift == 0) return false;
        return (*value.get(word).expect("a test reads an element at or past the count of its array")
                & ((std::uint64_t{1} << shift) - 1))
            != 0;
    }

    Words words_{};
    bool has_nan_ = false;
    bool has_positive_infinity_ = false;
    bool has_negative_infinity_ = false;
    ::fixy::FixedArray<std::uint8_t, 5> pad_{};
};

// The fma of the oracle: the exact a * b + c rounded one time, with the sign
// of a zero of IEEE 754 under the round to nearest.  A product of a zero and
// an addend of a zero give -0 when the two are -0, and an exact sum of zero
// from other terms gives +0.
template <class T>
[[nodiscard]] T exact_fma(T left, T right, T addend) noexcept {
    ExactSum<T> sum{};
    sum.add_product(left, right);
    sum.add(addend);
    T const rounded = sum.template rounded_as<T>();
    if (sum.is_nan() || !::fixy::fp::is_finite(rounded) || !sum.is_exact_zero()) return rounded;
    bool const is_product_zero = !::fixy::fp::is_nan(left) && !::fixy::fp::is_nan(right)
                              && (::fixy::fp::abs(left) < ::fixy::min_subnormal_of<T>
                                  || ::fixy::fp::abs(right) < ::fixy::min_subnormal_of<T>);
    bool const is_addend_zero = ::fixy::fp::abs(addend) < ::fixy::min_subnormal_of<T>;
    bool const is_negative_zero = is_product_zero && is_addend_zero
                               && ::fixy::fp::sign_bit(left) != ::fixy::fp::sign_bit(right)
                               && ::fixy::fp::sign_bit(addend);
    return is_negative_zero ? -T{0} : T{0};
}

}  // namespace crucible::test
