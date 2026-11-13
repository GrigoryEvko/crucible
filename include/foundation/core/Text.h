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
// foundation::os::PathText, which keeps the same two facts.  mint_c_text
// gives the CText of a view whose last character is its only zero, such as
// the view of an owner that keeps a zero after its characters.
//
// A TextView comes from a string literal or from an owner of the base, such
// as FixedText::view().  The constructor from a literal is consteval, so
// the literal has static storage and the view never dangles.  The
// constructor from raw parts is private, and the seal refuses std::bit_cast,
// so no pointer that a caller holds and no bytes become a TextView.
//
// A FixedText starts empty, or holds the characters of a literal that fits
// it.  push(), append() and the free append_hex<Digits>() put characters
// after the last one, and format()
// of foundation/core/Format.h writes a text into it from its start.  A write
// that does not fit gives Truncated.  A FixedText never holds more than N
// characters, and no operation reads past its count.
//
// THE READS OF THE CHARACTERS
//   A TextView gives the reads of a View: size(), the loop
//   `for (char c : text)` and window(offset, count), which gives the count
//   characters from offset, or no value when they do not fit.  The free
//   operations of foundation/core/Algorithms.h search and compare texts.
//
//   The Report family takes a TextView as an argument of a report, so
//   foundation/core/Report.h includes this header.  The loop gives the
//   cursor of foundation/core/Cursor.h, and the window gives an Option of
//   the Choice family.  These two headers end the process through
//   foundation/core/Fatal.h, which names no text type, so this header
//   includes them and defines begin(), end() and window() itself.  Each
//   member and each friend of a class of this header has its definition in
//   this header, so no other file can define one with the access of the
//   class.
//
// THE NAME OF AN ENUMERATOR
//   enum_name_text(value) gives the identifier of the enumerator that holds
//   value, and enum_words_text<E, Separator>(value) gives it in lower-case
//   words.  They give the texts of enum_name and enum_words of
//   foundation/reflect/EnumName.h as a TextView.  That header is on a lower
//   layer than this one, so it cannot name a TextView itself.  Each text has
//   static storage and a zero after its last character, so the view never
//   dangles.

#include <foundation/ByteSeal.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Cursor.h>
#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <cstdint>

namespace foundation::core {

class TextView;
class CText;

template <std::size_t Capacity>
    requires(Capacity > 0 && Capacity <= static_cast<std::size_t>(__PTRDIFF_MAX__))
class FixedText;

// The error of a write into a FixedText whose characters need more than its
// capacity: a push(), an append() or a format() of foundation/core/Format.h.
struct Truncated final {};

namespace detail {

// The consteval constructor of a TextView, of a CText and of a FixedText
// calls one of these functions when a literal breaks a rule.  No function has
// a definition, so the call is no constant expression, and the error of the
// compiler names the rule.
void text_literal_has_no_terminating_zero() noexcept;
void text_literal_holds_a_zero_byte_before_its_end() noexcept;

// The doors of the base to the parts of a text.  The rule owners keeps
// each caller outside the base out.
[[nodiscard]] constexpr char const* text_first_(TextView text) noexcept;
[[nodiscard]] constexpr TextView text_over_(char const* first, std::size_t size) noexcept;
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
    friend constexpr TextView detail::text_over_(char const* first, std::size_t size) noexcept;
    friend struct niche<TextView>;

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

    // Each door gives the characters of one object, and GCC bounds the size
    // of an object at __PTRDIFF_MAX__.  The optimizer reads that bound, so
    // a window of the text is never the empty value of an Option, with no
    // check.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t const count = size_;
        [[assume(count <= static_cast<std::size_t>(__PTRDIFF_MAX__))]];
        return count;
    }

    // The loop `for (char c : text)` reads each character one time, in
    // order.  The cursor holds the place and the end of the text, as the
    // cursor of a View does, so it never leaves the text.
    [[nodiscard]] constexpr ViewCursor<char const> begin() const noexcept {
        return ViewCursor<char const>{detail::ViewCursorDoor::key(), first_, first_ + size()};
    }
    [[nodiscard]] constexpr ViewCursor<char const> end() const noexcept {
        char const* const last = first_ + size();
        return ViewCursor<char const>{detail::ViewCursorDoor::key(), last, last};
    }

    // The count characters from offset, as an Option<TextView>, or no value
    // when they do not fit.  One branch.  The return type is deduced, so the
    // Option is named only where this class is complete and its niche below
    // it is visible.
    [[nodiscard]] constexpr auto window(std::size_t offset, std::size_t count) const noexcept;

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

// The empty value of an Option of a TextView: the largest count, which no
// TextView holds, because each door gives the characters of one object.
// The specialization stands beside the class, so each Option and each Result
// of a TextView sees it.
template <>
struct niche<TextView> {
    [[nodiscard]] static constexpr TextView empty(niche_key<TextView> const&) noexcept {
        return TextView{nullptr, ~std::size_t{0}};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<TextView> const&, TextView const& text) noexcept {
        return text.size_ == ~std::size_t{0};
    }
};

// The window fits, so its count is at most the count of this text, which
// size() bounds.  The optimizer then knows that the count is not the empty
// value of the niche, and the Option needs no test.
constexpr auto TextView::window(std::size_t offset, std::size_t count) const noexcept {
    using Result = Option<TextView>;
    if (offset > size() || count > size() - offset) [[unlikely]] {
        return Result{none};
    }
    [[assume(count <= static_cast<std::size_t>(__PTRDIFF_MAX__))]];
    return Result::some(TextView{first_ + offset, count});
}

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

    // The characters of a string literal, without its terminating zero.  A
    // literal of more than Capacity characters builds no FixedText.  A
    // literal with no terminating zero, or with a zero before its end, is no
    // constant expression, and the build stops at the call.
    template <std::size_t Length>
        requires(Length - 1 <= Capacity)
    consteval explicit FixedText(char const (&literal)[Length]) noexcept : size_{Length - 1} {
        if (literal[Length - 1] != '\0') detail::text_literal_has_no_terminating_zero();
        for (std::size_t index = 0; index + 1 < Length; ++index) {
            if (literal[index] == '\0') detail::text_literal_holds_a_zero_byte_before_its_end();
            bytes_[index] = literal[index];
        }
    }

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

    // The loop `for (char c : text)` reads each character that the buffer
    // holds.  A loop over a temporary binds it to a reference first, and a
    // cursor of an rvalue is refused.
    [[nodiscard]] constexpr ViewCursor<char const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<char const> end() const& noexcept { return view().end(); }
    void
    begin() const&& = delete("a cursor of a temporary FixedText points into an object that the full expression ends");
    void
    end() const&& = delete("a cursor of a temporary FixedText points into an object that the full expression ends");

    // The buffer then holds no character.
    constexpr void clear() noexcept { size_ = 0; }

    // Puts character after the last character, or gives Truncated when the
    // buffer holds Capacity characters.  One compare.
    [[nodiscard]] constexpr Result<Unit, Truncated> push(char character) & noexcept {
        if (size_ == Capacity) [[unlikely]] {
            return err(Truncated{});
        }
        bytes_[size_] = character;
        ++size_;
        return Unit{};
    }

    // Puts each character of text after the last character.  When they do
    // not fit, it gives Truncated, and the buffer keeps the characters that it
    // held.  One compare, and one copy for each character.
    [[nodiscard]] constexpr Result<Unit, Truncated> append(TextView text) & noexcept {
        std::size_t const count = text.size();
        if (count > Capacity - size()) [[unlikely]] {
            return err(Truncated{});
        }
        for (std::size_t index = 0; index < count; ++index) {
            bytes_[size_ + index] = text.first_[index];
        }
        size_ += count;
        return Unit{};
    }
};

namespace detail {

constexpr char const* text_first_(TextView text) noexcept { return text.first_; }

// A TextView over the size characters at first.  The caller is an owner of
// the base that holds the characters, such as a GrowText, or a door that
// checked the raw parts of a foreign run.  The size is at most
// __PTRDIFF_MAX__, and first is not null when the size is above zero.
constexpr TextView text_over_(char const* first, std::size_t size) noexcept { return TextView{first, size}; }

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

// Writes the low count hexadecimal digits of value at out, the most
// significant first, in lower case.  count is 1 to 16.
constexpr void put_hex_digits_(std::uint64_t value, std::size_t count, char* out) noexcept {
    for (std::size_t index = count; index != 0; --index) {
        out[index - 1] = "0123456789abcdef"[value & 0xFU];
        value >>= 4U;
    }
}

}  // namespace detail

// The CText of the characters of text before its last one, when the last
// character is a zero and no character before it is a zero.  The view holds
// the zero itself, and the mint reads only the characters of the view: a
// TextView does not own the byte after its last character.  An empty view, a
// view whose last character is not a zero, and a view with a zero before its
// last character give no value.  The CText borrows the owner of the view.
// Complexity: O(n).
[[nodiscard]] constexpr Option<CText> mint_c_text(TextView text) noexcept {
    std::size_t const size = text.size();
    if (size == 0) return none;
    char const* const first = detail::text_first_(text);
    if (first[size - 1] != '\0') return none;
    for (std::size_t index = 0; index + 1 < size; ++index) {
        if (first[index] == '\0') return none;
    }
    return Option<CText>::some(detail::c_text_over_(first, size - 1));
}

// Puts the low Digits hexadecimal digits of value after the last character
// of text, the most significant first, in lower case and with the zeros
// that lead.  When they do not fit, it gives Truncated, and text keeps the
// characters that it held.  One compare and Digits stores.
template <std::size_t Digits, std::size_t Capacity>
    requires(Digits >= 1 && Digits <= 16)
[[nodiscard]] constexpr Result<Unit, Truncated> append_hex(FixedText<Capacity>& text, std::uint64_t value) noexcept {
    std::size_t const size = text.size();
    if (Digits > Capacity - size) [[unlikely]] {
        return err(Truncated{});
    }
    detail::put_hex_digits_(value, Digits, detail::text_bytes_(text) + size);
    detail::text_resize_(text, size + Digits);
    return Unit{};
}

// The identifier of the enumerator that holds value, or "<unknown E>" when
// no enumerator holds it.  When two enumerators share a value, the first in
// declaration order wins.  Complexity: one compare for each enumerator.
template <::foundation::reflect::ScopedEnum E>
[[nodiscard]] constexpr TextView enum_name_text(E value) noexcept {
    ::foundation::reflect::detail::NameChars const name = ::foundation::reflect::detail::enum_name_chars(value);
    return detail::c_text_over_(name.first, name.size).view();
}

// The identifier of the enumerator that holds value in lower-case words
// with Separator between two words, or "<unknown E>" when no enumerator
// holds it.  Complexity: one compare for each enumerator.
template <::foundation::reflect::ScopedEnum E, char Separator>
[[nodiscard]] constexpr TextView enum_words_text(E value) noexcept {
    ::foundation::reflect::detail::NameChars const words =
        ::foundation::reflect::detail::enum_words_chars<E, Separator>(value);
    return detail::c_text_over_(words.first, words.size).view();
}

}  // namespace foundation::core
