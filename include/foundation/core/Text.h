#pragma once

// The text types of the Region family: a borrowed run of characters, a
// borrowed run with a terminating zero, and a buffer of characters with a
// fixed capacity.
//
//   TextView        a borrowed run of characters: a pointer and a count
//   CText           a borrowed run of characters with a zero after the last
//                   one, for a call of the kernel that reads up to the zero
//   FixedText<N>    an owned buffer of N characters and the count of the
//                   characters that it holds
//
// A CText comes from a string literal.  The consteval constructor proves the
// zero at the end and refuses a zero before it, so a reader that stops at the
// first zero reads each character of the text and no other byte.  A CText of
// a path that a program builds at run time comes from
// foundation::os::PathText, which keeps the same two facts.
//
// A TextView comes from a string literal or from an owner of the base, such
// as FixedText::view().  The constructor from a literal is consteval, so
// the literal has static storage and the view never dangles.  The
// constructor from raw parts is private, and the seal refuses std::bit_cast,
// so no pointer that a caller holds and no bytes become a TextView.
//
// A FixedText starts empty.  format() of foundation/core/Format.h writes a
// text into it.  A FixedText never holds more than N characters, and no
// operation reads past its count.
//
// This header uses no other family, so the Report family can take a
// TextView as an argument of a report.

#include <foundation/ByteSeal.h>

#include <cstddef>

namespace foundation::core {

class TextView;
class CText;

template <std::size_t Capacity>
    requires(Capacity > 0 && Capacity <= static_cast<std::size_t>(__PTRDIFF_MAX__))
class FixedText;

namespace detail {

// The consteval constructor of a TextView and of a CText calls one of these
// functions when a literal breaks a rule.  No function has a definition, so
// the call is no constant expression, and the error of the compiler names the
// rule.
void text_literal_has_no_terminating_zero() noexcept;
void text_literal_holds_a_zero_byte_before_its_end() noexcept;

// The doors of the base to the parts of a text.  The detail-namespace
// guard keeps each caller outside the base out.
[[nodiscard]] constexpr char const* text_first_(TextView text) noexcept;
[[nodiscard]] constexpr char const* c_text_first_(CText text) noexcept;
[[nodiscard]] constexpr CText c_text_over_(char const* first, std::size_t size) noexcept;

template <std::size_t Capacity>
[[nodiscard]] constexpr char* text_bytes_(FixedText<Capacity>& text) noexcept;

template <std::size_t Capacity>
constexpr void text_resize_(FixedText<Capacity>& text, std::size_t count) noexcept;

}  // namespace detail

// A borrowed run of characters.  The characters need no terminating zero,
// and a zero byte inside the run is a character as each other byte is.
class TextView {
    char const* first_ = nullptr;
    std::size_t size_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <std::size_t Capacity>
        requires(Capacity > 0 && Capacity <= static_cast<std::size_t>(__PTRDIFF_MAX__))
    friend class FixedText;
    friend class CText;
    friend constexpr char const* detail::text_first_(TextView text) noexcept;

    constexpr TextView(char const* first, std::size_t size) noexcept : first_{first}, size_{size} {}

public:
    // The empty text.
    constexpr TextView() noexcept = default;

    // The characters of a string literal, without its terminating zero.  A
    // zero byte before the end would cut the text for a reader that stops
    // at a zero, so it is no constant expression, and the build stops at
    // the call.
    template <std::size_t Length>
    consteval TextView(char const (&literal)[Length]) noexcept : first_{literal}, size_{Length - 1} {
        if (literal[Length - 1] != '\0') detail::text_literal_has_no_terminating_zero();
        for (std::size_t index = 0; index + 1 < Length; ++index) {
            if (literal[index] == '\0') detail::text_literal_holds_a_zero_byte_before_its_end();
        }
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

    // Two texts are equal when they hold the same characters in the same
    // order.  The cost is one compare for each character.
    [[nodiscard]] friend constexpr bool operator==(TextView left, TextView right) noexcept {
        if (left.size_ != right.size_) return false;
        for (std::size_t index = 0; index < left.size_; ++index) {
            if (left.first_[index] != right.first_[index]) return false;
        }
        return true;
    }
};

// A borrowed run of characters with a zero after its last character.  A
// call of the kernel that takes a path reads it up to the zero, so a door
// takes a CText where it gives a text to the kernel.
class CText {
    char const* first_ = "";
    std::size_t size_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend constexpr char const* detail::c_text_first_(CText text) noexcept;
    friend constexpr CText detail::c_text_over_(char const* first, std::size_t size) noexcept;

    constexpr CText(char const* first, std::size_t size) noexcept : first_{first}, size_{size} {}

public:
    // The empty text.
    constexpr CText() noexcept = default;

    // The characters of a string literal.  The literal must end with its
    // zero and hold no zero before it, or the call is no constant expression
    // and the build stops at it.
    template <std::size_t Length>
    consteval CText(char const (&literal)[Length]) noexcept : first_{literal}, size_{Length - 1} {
        if (literal[Length - 1] != '\0') detail::text_literal_has_no_terminating_zero();
        for (std::size_t index = 0; index + 1 < Length; ++index) {
            if (literal[index] == '\0') detail::text_literal_holds_a_zero_byte_before_its_end();
        }
    }

    // The count of the characters, without the zero.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

    // The characters, without the zero.
    [[nodiscard]] constexpr TextView view() const noexcept { return TextView{first_, size_}; }
};

// An owned buffer of Capacity characters.  The count of the characters that
// it holds is at most Capacity in each state, so view() never reads past
// the buffer.
template <std::size_t Capacity>
    requires(Capacity > 0 && Capacity <= static_cast<std::size_t>(__PTRDIFF_MAX__))
class FixedText {
    char bytes_[Capacity]{};
    std::size_t size_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <std::size_t OtherCapacity>
    friend constexpr char* detail::text_bytes_(FixedText<OtherCapacity>& text) noexcept;
    template <std::size_t OtherCapacity>
    friend constexpr void detail::text_resize_(FixedText<OtherCapacity>& text, std::size_t count) noexcept;

public:
    static constexpr std::size_t capacity = Capacity;

    // The empty text.
    constexpr FixedText() noexcept = default;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t const count = size_;
        [[assume(count <= Capacity)]];
        return count;
    }

    // The characters that the buffer holds.  The view borrows this buffer,
    // so a view of a temporary buffer is refused: it would dangle at the end
    // of the full expression.
    [[nodiscard]] constexpr TextView view() const& noexcept { return TextView{bytes_, size()}; }
    TextView
    view() const&& = delete("a view of a temporary FixedText points into an object that the full expression ends");
};

namespace detail {

constexpr char const* text_first_(TextView text) noexcept { return text.first_; }

// The first character of a CText.  A zero follows its last character.
constexpr char const* c_text_first_(CText text) noexcept { return text.first_; }

// A CText over the size characters at first.  The caller is an owner of the
// base that keeps a zero at first[size] and no zero before it, such as
// foundation::os::PathText or the name of an entry that the kernel writes
// with its zero.
constexpr CText c_text_over_(char const* first, std::size_t size) noexcept { return CText{first, size}; }

template <std::size_t Capacity>
constexpr char* text_bytes_(FixedText<Capacity>& text) noexcept {
    return text.bytes_;
}

// Sets the count of the characters that text holds.  A count past the
// capacity becomes the capacity, so the count of a FixedText never leaves
// its buffer.
template <std::size_t Capacity>
constexpr void text_resize_(FixedText<Capacity>& text, std::size_t count) noexcept {
    text.size_ = count <= Capacity ? count : Capacity;
}

}  // namespace detail

}  // namespace foundation::core
