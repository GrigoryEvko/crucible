#pragma once

// The reads of the text that a host tool reports: lines, fields of a line,
// and decimal numbers.  Each function borrows the characters of a TextView of
// the base, so a result lives as long as the text that it reads.
//
// A field is a key and a value at the two sides of the first separator of a
// line.  The white space at the two ends of the key and of the value is no
// part of them.  White space is a space, a tab, a carriage return and a line
// feed.

#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>

namespace crucible::topology::text {

struct Field {
    ::fixy::TextView key{};
    ::fixy::TextView value{};
};

[[nodiscard]] constexpr bool is_space(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

[[nodiscard]] constexpr bool is_digit(char character) noexcept { return character >= '0' && character <= '9'; }

[[nodiscard]] constexpr char to_lower(char character) noexcept {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) : character;
}

// The first count characters of text, or all of text when it is shorter.
[[nodiscard]] constexpr ::fixy::TextView head(::fixy::TextView text, std::size_t count) noexcept {
    return text.window(0, ::fixy::min(count, text.size())).value_or(::fixy::TextView{});
}

// The characters of text after its first count characters, or no character
// when the text is not longer than count.
[[nodiscard]] constexpr ::fixy::TextView tail(::fixy::TextView text, std::size_t count) noexcept {
    if (count >= text.size()) {
        return ::fixy::TextView{};
    }
    return text.window(count, text.size() - count).value_or(::fixy::TextView{});
}

// The text without the white space at its two ends.  Complexity: linear in
// the characters.
[[nodiscard]] constexpr ::fixy::TextView trim(::fixy::TextView text) noexcept {
    std::size_t first = 0;
    std::size_t end = 0;
    std::size_t position = 0;
    bool has_word = false;
    for (char const character : text) {
        ++position;
        if (is_space(character)) {
            continue;
        }
        if (!has_word) {
            first = position - 1;
            has_word = true;
        }
        end = position;
    }
    return has_word ? text.window(first, end - first).value_or(::fixy::TextView{}) : ::fixy::TextView{};
}

// The first line of rest, without its line feed.  rest then holds the text
// after that line feed, or no character when the line was the last one.
[[nodiscard]] constexpr ::fixy::TextView take_line(::fixy::TextView& rest) noexcept {
    std::size_t const end = ::fixy::find(rest, '\n').value_or(rest.size());
    ::fixy::TextView const line = head(rest, end);
    rest = tail(rest, end + 1);
    return line;
}

// The line split at its first separator of the first kind in separators that
// the line holds.  A line with no such separator is a key with no value.
// Complexity: linear in the characters for each kind of separator.
[[nodiscard]] constexpr Field split_field(::fixy::TextView line, ::fixy::TextView separators) noexcept {
    for (char const separator : separators) {
        ::fixy::Option<std::size_t> const found = ::fixy::find(line, separator);
        for (std::size_t const position : found) {
            return Field{.key = trim(head(line, position)), .value = trim(tail(line, position + 1))};
        }
    }
    return Field{.key = trim(line), .value = ::fixy::TextView{}};
}

// True when the two texts hold the same characters, with no difference
// between an upper case and a lower case ASCII letter.
[[nodiscard]] constexpr bool equal_ignoring_case(::fixy::TextView left, ::fixy::TextView right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    auto other = right.begin();
    for (char const character : left) {
        if (to_lower(character) != to_lower(*other)) {
            return false;
        }
        ++other;
    }
    return true;
}

// True when haystack holds needle, with no difference between an upper case
// and a lower case ASCII letter.  An empty needle is in no text.
// Complexity: O(n * m) for a haystack of n and a needle of m characters.
[[nodiscard]] constexpr bool contains_ignoring_case(::fixy::TextView haystack, ::fixy::TextView needle) noexcept {
    if (needle.size() == 0 || needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        if (equal_ignoring_case(haystack.window(start, needle.size()).value_or(::fixy::TextView{}), needle)) {
            return true;
        }
    }
    return false;
}

// The count of the decimal digits at the start of text.
[[nodiscard]] constexpr std::size_t leading_digits(::fixy::TextView text) noexcept {
    std::size_t digits = 0;
    for (char const character : text) {
        if (!is_digit(character)) {
            break;
        }
        ++digits;
    }
    return digits;
}

// The value of the decimal digits at the start of text, or no value when
// the text starts with no digit or the value does not fit 64 bits.
[[nodiscard]] constexpr ::fixy::Option<std::uint64_t> leading_u64(::fixy::TextView text) noexcept {
    std::size_t const digits = leading_digits(text);
    if (digits == 0) {
        return ::fixy::none;
    }
    return ::fixy::parse<std::uint64_t>(head(text, digits)).ok();
}

// The first word of text, after its leading white space.
[[nodiscard]] constexpr ::fixy::TextView first_word(::fixy::TextView text) noexcept {
    ::fixy::TextView const trimmed = trim(text);
    std::size_t length = 0;
    for (char const character : trimmed) {
        if (is_space(character)) {
            break;
        }
        ++length;
    }
    return head(trimmed, length);
}

}  // namespace crucible::topology::text
