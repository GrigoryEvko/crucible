#pragma once

// The reads of the numbers of /proc and /sys text: a decimal or a hexadecimal
// count at the start of a text, and the hit ratio of the NUMA counters.  The
// kernel writes the text, and a count can be any value of 64 bits.  So each
// read and each sum or product of counts is checked, and a value that does
// not fit 64 bits gives no value.
//
// Each function borrows the characters of a TextView of the base, and reads
// no file.  crucible/perf/ProcGauges.h reads the files.

#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::perf::proc_text {

[[nodiscard]] constexpr bool is_blank(char character) noexcept { return character == ' ' || character == '\t'; }

[[nodiscard]] constexpr bool is_decimal_digit(char character) noexcept { return character >= '0' && character <= '9'; }

[[nodiscard]] constexpr bool is_hex_digit(char character) noexcept {
    return is_decimal_digit(character) || (character >= 'a' && character <= 'f')
        || (character >= 'A' && character <= 'F');
}

// The count of the leading characters of text that satisfy predicate.
// Complexity: linear in that count.
template <class Predicate>
[[nodiscard]] constexpr std::size_t leading_count(::fixy::TextView text, Predicate predicate) noexcept {
    std::size_t count = 0;
    for (char const character : text) {
        if (!predicate(character)) break;
        ++count;
    }
    return count;
}

// The first count characters of text.  The caller keeps count at most the
// size of the text.
[[nodiscard]] constexpr ::fixy::TextView prefix(::fixy::TextView text, std::size_t count) noexcept {
    return text.window(0, count).value_or(::fixy::TextView{});
}

// The characters of text after its first count characters, or no character
// when the text holds fewer.
[[nodiscard]] constexpr ::fixy::TextView suffix(::fixy::TextView text, std::size_t count) noexcept {
    std::size_t const start = ::fixy::min(count, text.size());
    return text.window(start, text.size() - start).value_or(::fixy::TextView{});
}

constexpr void skip_blanks(::fixy::TextView& text) noexcept { text = suffix(text, leading_count(text, is_blank)); }

// The decimal number after the blanks at the start of text: zero when no digit
// follows them, or no value when the digits do not fit 64 bits.  The text then
// starts after the last digit.  Complexity: linear in the count of the digits.
[[nodiscard]] constexpr ::fixy::Option<std::uint64_t> take_decimal(::fixy::TextView& text) noexcept {
    skip_blanks(text);
    std::size_t const count = leading_count(text, is_decimal_digit);
    ::fixy::TextView const digits = prefix(text, count);
    text = suffix(text, count);
    if (count == 0) return ::fixy::some(std::uint64_t{0});
    return ::fixy::parse<std::uint64_t>(digits).ok();
}

// The hexadecimal digits at the start of text, the form in which
// /proc/net/softnet_stat and /proc/net/tcp write their counters: zero when no
// digit is there, or no value when the digits do not fit 64 bits.  The text
// then starts after the last digit.  Complexity: linear in the count of the
// digits.
[[nodiscard]] constexpr ::fixy::Option<std::uint64_t> take_hex(::fixy::TextView& text) noexcept {
    std::size_t const count = leading_count(text, is_hex_digit);
    ::fixy::TextView const digits = prefix(text, count);
    text = suffix(text, count);
    if (count == 0) return ::fixy::some(std::uint64_t{0});
    return ::foundation::core::parse_hex<std::uint64_t>(digits).ok();
}

// The sum of a count and the value of a read, or no value when the read has
// no value or the sum does not fit 64 bits.  A gauge that sums its counts
// folds each read with it, so one count past 64 bits makes the gauge give no
// reading.  The sum is commutative, so the two arguments can swap with no
// change of meaning.
[[nodiscard]] constexpr ::fixy::Option<std::uint64_t> add_read(::fixy::Option<std::uint64_t> total,
                                                               ::fixy::Option<std::uint64_t> read) noexcept {
    return std::move(total).and_then([&read](std::uint64_t sum) noexcept {
        return std::move(read).and_then(
            [sum](std::uint64_t value) noexcept { return ::fixy::checked_add(sum, value); });
    });
}

// The two NUMA counters of /proc/vmstat: the allocations that got memory of
// their own node, and the allocations that did not.
struct NumaCounts {
    std::uint64_t hit = 0;
    std::uint64_t miss = 0;
};

// The share of the hits in the allocations, in hundredths of a percent, and
// zero when no allocation is counted.  The function halves the two counts
// until the hits times 10000 and the sum of the two counts fit 64 bits.  A
// halving occurs only for a count of at least 2^50, so it changes the ratio
// by far less than one hundredth of a percent.  Complexity: at most 64
// halvings.
[[nodiscard]] constexpr std::uint64_t numa_hit_ratio_x100(NumaCounts counts) noexcept {
    constexpr std::uint64_t kHundredthsOfAPercent = 10000;
    std::uint64_t hit = counts.hit;
    std::uint64_t miss = counts.miss;
    while (::fixy::checked_mul(hit, kHundredthsOfAPercent).is_none() || ::fixy::checked_add(hit, miss).is_none()) {
        hit >>= 1U;
        miss >>= 1U;
    }
    std::uint64_t const denominator = hit + miss;
    return denominator == 0 ? 0 : (hit * kHundredthsOfAPercent) / denominator;
}

}  // namespace crucible::perf::proc_text
