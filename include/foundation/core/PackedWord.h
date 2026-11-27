#pragma once

// The packed words of the Scalar family: one unsigned word of named fields.
//
//   BitField<Tag, Width>          a field of Width bits, named by Tag, an
//                                 empty final class, as a tag of Side names
//                                 a role.  It holds no value: it names one
//                                 part of the layout of a PackedWord
//   PackedWord<Word, Fields...>   an unsigned Word that holds each field of
//                                 Fields: the first field in the low bits,
//                                 and each later field above the one before
//
// Layout
//   The fields start at bit 0, in the order of Fields, with no gap.  A field
//   has 1 to 64 bits, and the widths sum to at most the bits of Word.  Each
//   tag names one field.  A layout that breaks a rule does not compile, and
//   get and with do not compile for a tag that names no field.  Each bit above
//   the last field is zero: from_bits refuses a word with such a bit, and with
//   refuses a value with a bit past the width of its field.
//
// Operations
//   PackedWord{}            each field zero
//   from_bits(word)         the packed word of a word, such as the word of a
//                           record from a file, or no value when the word
//                           holds a bit above the last field.  One compare
//   bits()                  the word
//   get<Tag>()              the field of Tag, moved to bit 0, in the narrowest
//                           unsigned word that holds its width.  A shift and
//                           a mask
//   with<Tag>(value)        the same word with the field of Tag set to value,
//                           or no value when value holds a bit past the width
//                           of the field.  One compare, a mask and an or
//   with_fitting<Tag>(value)  the same word with the field of Tag set to
//                           value, for an unsigned value whose type has no
//                           more bits than the field.  The type proves the
//                           fit, so the write has no test and gives the word
//                           itself.  A mask and an or.  A signed value or a
//                           wider type does not compile: a signed source goes
//                           through one explicit conversion at the call
//   ==                      true when the two words are equal
//
// A PackedWord is trivially copyable, with the size, the alignment and the
// layout of its word.  So a record that the code copies by its bytes, such as
// an op of a region, holds it where it held the word, and no layout moves.
// When the fields leave a bit of the word free, the word of each bit set is
// the niche of the type (the niche protocol of foundation/core/Choice.h).  No
// door makes that word, so an Option of such a PackedWord is one word with no
// flag.
// The type refuses no byte image, so a word that crosses a boundary, such as
// a field of a file, comes in through from_bits.  The shape check of
// test/shape/core_shapes.cpp compares get and with with the shift and the
// mask of a hand-written word.

#include <foundation/core/Choice.h>
#include <foundation/core/Number.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// A tag of a field: an empty final class.
template <class Tag>
concept FieldTag = std::is_class_v<Tag> && std::is_empty_v<Tag> && std::is_final_v<Tag>;

template <class Tag, std::size_t Width>
    requires FieldTag<Tag> && (Width >= 1) && (Width <= 64)
struct BitField final {
    using tag_type = Tag;
    static constexpr std::size_t width = Width;
};

namespace detail {

// True for a BitField.  A consteval test of the type, so no unit can
// specialize it.
template <class Field>
[[nodiscard]] consteval bool is_bit_field_() {
    return requires {
        typename Field::tag_type;
        requires std::is_same_v<Field, BitField<typename Field::tag_type, Field::width>>;
    };
}

// The narrowest unsigned word that holds a field of Width bits.
template <std::size_t Width>
using FieldWordOf = std::conditional_t<
    (Width <= 8), std::uint8_t,
    std::conditional_t<(Width <= 16), std::uint16_t, std::conditional_t<(Width <= 32), std::uint32_t, std::uint64_t>>>;

// The count of the fields of Fields that Tag names.  O(fields) at compile time.
template <class Tag, class... Fields>
inline constexpr std::size_t tag_count_ =
    (std::size_t{std::is_same_v<typename Fields::tag_type, Tag>} + ... + std::size_t{0});

// The width of the field of Tag.  O(fields) at compile time.
template <class Tag, class... Fields>
inline constexpr std::size_t field_width_ =
    ((std::is_same_v<typename Fields::tag_type, Tag> ? Fields::width : std::size_t{0}) + ... + std::size_t{0});

// The low bit of the field of Tag: the sum of the widths of the fields before
// it.  O(fields) at compile time.
template <class Tag, class... Fields>
[[nodiscard]] consteval std::size_t field_shift_() {
    std::size_t shift = 0;
    bool is_past = false;
    ((is_past = is_past || std::is_same_v<typename Fields::tag_type, Tag>,
      shift += is_past ? std::size_t{0} : Fields::width),
     ...);
    return shift;
}

// The count low bits of a word of 64 bits, for a count from 1 to 64.
[[nodiscard]] consteval std::uint64_t low_bits_(std::size_t count) {
    return count >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << count) - 1;
}

// A value of 64 bits as a word that holds each bit of the value that the
// caller keeps.  The word of 64 bits takes the value with no cast.
template <class Word>
[[nodiscard]] constexpr Word packed_word_from_(std::uint64_t value) noexcept {
    if constexpr (std::is_same_v<Word, std::uint64_t>) {
        return value;
    } else {
        return static_cast<Word>(value);
    }
}

}  // namespace detail

// A layout of a PackedWord: an unsigned Word, at least one BitField, each tag
// one time, and the widths at most the bits of Word.
template <class Word, class... Fields>
concept PackedLayout = UnsignedInteger<Word> && (sizeof...(Fields) >= 1) && (detail::is_bit_field_<Fields>() && ...)
                    && ((detail::tag_count_<typename Fields::tag_type, Fields...> == 1) && ...)
                    && ((Fields::width + ... + 0) <= sizeof(Word) * 8);

// A tag that names one field of the layout.
template <class Tag, class... Fields>
concept NamesOneField = (detail::tag_count_<Tag, Fields...> == 1);

template <class Word, class... Fields>
    requires PackedLayout<Word, Fields...>
class PackedWord final {
    // The bits of the fields, from bit 0 to the last field.
    static constexpr std::uint64_t used_bits_ = detail::low_bits_((Fields::width + ... + 0));

    // True when the fields leave at least one bit of the word free.
    static constexpr bool has_free_bit_ = (Fields::width + ... + 0) < sizeof(Word) * 8;

    template <class Tag>
    static constexpr std::size_t width_of_ = detail::field_width_<Tag, Fields...>;

    template <class Tag>
    static constexpr std::size_t shift_of_ = detail::field_shift_<Tag, Fields...>();

    // The bits of the field of Tag, in place.
    template <class Tag>
    static constexpr std::uint64_t mask_of_ = detail::low_bits_(width_of_<Tag>) << shift_of_<Tag>;

    Word bits_ = 0;

    constexpr explicit PackedWord(Word bits) noexcept : bits_{bits} {}

public:
    using word_type = Word;

    // The word of each field of a tag.
    template <class Tag>
        requires NamesOneField<Tag, Fields...>
    using field_type = detail::FieldWordOf<width_of_<Tag>>;

    // Each field zero.
    constexpr PackedWord() noexcept = default;

    // The packed word of bits, or no value when bits holds a bit above the
    // last field.  One compare.  The result is an Option<PackedWord>.  The
    // return type is deduced, so the Option is named only where this class
    // is complete.
    [[nodiscard]] static constexpr auto from_bits(Word bits) noexcept {
        using Result = Option<PackedWord>;
        if ((std::uint64_t{bits} & ~used_bits_) != 0) {
            return Result{none};
        }
        return Result::some(PackedWord{bits});
    }

    [[nodiscard]] constexpr Word bits() const noexcept { return bits_; }

    // The field of Tag, moved to bit 0.  A shift and a mask.
    template <class Tag>
        requires NamesOneField<Tag, Fields...>
    [[nodiscard]] constexpr field_type<Tag> get() const noexcept {
        std::uint64_t const field = (std::uint64_t{bits_} >> shift_of_<Tag>)&detail::low_bits_(width_of_<Tag>);
        return detail::packed_word_from_<field_type<Tag>>(field);
    }

    // The same word with the field of Tag set to value, or no value when value
    // holds a bit past the width of the field.  One compare, a mask and an or.
    // The result is an Option<PackedWord>, as from_bits gives.
    template <class Tag>
        requires NamesOneField<Tag, Fields...>
    [[nodiscard]] constexpr auto with(field_type<Tag> value) const noexcept {
        using Result = Option<PackedWord>;
        if ((std::uint64_t{value} & ~detail::low_bits_(width_of_<Tag>)) != 0) {
            return Result{none};
        }
        // The mask of the used bits states the invariant to the compiler, so
        // the niche test of the Option folds.  It merges into the mask of the
        // field.
        std::uint64_t const placed =
            (std::uint64_t{bits_} & used_bits_ & ~mask_of_<Tag>) | (std::uint64_t{value} << shift_of_<Tag>);
        return Result::some(PackedWord{detail::packed_word_from_<Word>(placed)});
    }

    // The same word with the field of Tag set to value, for an unsigned value
    // whose type has no more bits than the field.  The constraint proves that
    // each value fits, so the write has no test and gives the word itself.
    // The mask of the used bits keeps each free bit zero, as with does.
    template <class Tag, class Value>
        requires NamesOneField<Tag, Fields...> && UnsignedInteger<Value> && (sizeof(Value) * 8 <= width_of_<Tag>)
    [[nodiscard]] constexpr PackedWord with_fitting(Value value) const noexcept {
        std::uint64_t const placed =
            (std::uint64_t{bits_} & used_bits_ & ~mask_of_<Tag>) | (std::uint64_t{value} << shift_of_<Tag>);
        return PackedWord{detail::packed_word_from_<Word>(placed)};
    }

    [[nodiscard]] friend constexpr bool operator==(PackedWord left, PackedWord right) noexcept {
        return left.bits_ == right.bits_;
    }

    // The niche protocol: the word of each bit set holds a free bit, which no
    // door sets.  A layout that fills its word has no niche.
    [[nodiscard]] static constexpr PackedWord niche_empty(niche_key<PackedWord> const&) noexcept
        requires has_free_bit_
    {
        return PackedWord{detail::packed_word_from_<Word>(~std::uint64_t{0})};
    }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<PackedWord> const&) const noexcept
        requires has_free_bit_
    {
        return (std::uint64_t{bits_} & ~used_bits_) != 0;
    }
};

}  // namespace foundation::core
