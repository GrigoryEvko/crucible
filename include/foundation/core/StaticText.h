#pragma once

// A static text in one word.
//
// A StaticText names a run of characters with static storage and a zero
// after its last character.  Only a string literal and the site door of the
// base layer make one.  So the text is never null and lives until the
// program ends, and an atomic cell holds it in one pointer.  view() gives
// the TextView of the characters before the zero.
//
// The type stands in a header of its own, because its atomic word stands
// beside it, and foundation/core/Atomic.h includes foundation/core/Text.h
// through foundation/core/Report.h.  The literal form asks the static text
// door of foundation/core/Text.h.

#include <foundation/ByteSeal.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Text.h>

#include <cstddef>
#include <type_traits>

namespace foundation::core {

class StaticText;

namespace detail {

// Has no constant definition.  A call to it stops the constant evaluation,
// and the diagnostic names it.
void static_text_holds_a_zero_before_its_end() noexcept;

struct StaticTextDoor;

}  // namespace detail

// The word of a StaticText in an atomic cell (THE WORD OF A VALUE of
// foundation/core/Atomic.h): the pointer of its first character.  No
// StaticText holds the null pointer, so the null pointer is the empty word,
// and the cell of an Option of a StaticText holds one pointer.  Only a cell
// makes the key of from_word, and a cell holds only words that to_word gave.
template <>
struct atomic_word<StaticText> {
    using word_type = char const*;
    static constexpr word_type empty_word = nullptr;

    [[nodiscard]] static constexpr word_type to_word(StaticText const& text) noexcept;

    [[nodiscard]] static constexpr StaticText from_word(atomic_word_key<StaticText> const& key,
                                                        word_type word) noexcept;
};

// The text has static storage, and no thread writes it, so a StaticText
// moves to another thread with no borrow (foundation/Transfer.h).
class[[= ::foundation::transfer::sendable{}]] StaticText {
public:
    // The text of a string literal.  The constructor is consteval, so a
    // pointer of the run time cannot reach it, a literal with a zero before
    // its end stops the build, and an array of a frame is no constant result.
    template <std::size_t Count>
    consteval StaticText(char const (&literal)[Count]) noexcept : first_{literal} {
        if (mint_static_text(literal).size() + 1 != Count) detail::static_text_holds_a_zero_before_its_end();
    }

    // A literal binds to the array form above, and a pointer gets here.
    template <class Pointer>
        requires std::is_pointer_v<std::remove_cvref_t<Pointer>>
    StaticText(Pointer&& text) = delete("a pointer of the run time can name a text that ends before the program");

    // The characters before the zero.  No object is larger than
    // __PTRDIFF_MAX__ bytes, and that bound keeps GCC from changing the loop
    // into a call of strlen of the C library, so a signal handler can read the
    // view.  Complexity: linear in the length.
    [[nodiscard]] constexpr TextView view() const noexcept {
        std::size_t size = 0;
        while (size < static_cast<std::size_t>(__PTRDIFF_MAX__) && first_[size] != '\0')
            ++size;
        return detail::text_over_(first_, size);
    }

private:
    friend struct atomic_word<StaticText>;
    friend struct detail::StaticTextDoor;

    // The tag of the site door, which only the door names.
    struct SiteFile {};

    // The text of a word that the atomic word of this class gave.
    constexpr StaticText(char const* first, atomic_word_key<StaticText> const& /*key*/) noexcept : first_{first} {}

    // The file of a site, through the door below.
    constexpr StaticText(char const* first, SiteFile /*tag*/) noexcept : first_{first} {}

    char const* first_ = "";
    // The seal makes the assignment user-provided, so std::bit_cast cannot
    // build a StaticText from the bytes of an address.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

namespace detail {

// The door of the base to the text of the file of a site.  The file name of
// a std::source_location is never null: the standard gives it as a string of
// static storage with a zero at its end.  The watch of fixy/session/Watch.h
// gives it here.  A null pointer is the empty word of the atomic cell, so a
// store of it in the cell of the watch ends the process.  The door is in
// namespace detail, so code outside the base cannot reach it, and the base
// gives it only the file name of a site.
struct StaticTextDoor {
    [[nodiscard]] static constexpr StaticText of_site_file(char const* file) noexcept {
        return StaticText{file, StaticText::SiteFile{}};
    }
};

}  // namespace detail

constexpr atomic_word<StaticText>::word_type atomic_word<StaticText>::to_word(StaticText const& text) noexcept {
    return text.first_;
}

constexpr StaticText atomic_word<StaticText>::from_word(atomic_word_key<StaticText> const& key,
                                                        word_type word) noexcept {
    return StaticText{word, key};
}

}  // namespace foundation::core
