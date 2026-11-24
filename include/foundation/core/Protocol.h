#pragma once

// The Protocol family, its upper part: the hash, the byte codec and the
// format of a value, derived from the parts of its type.
//
//   Encodable<T>        a class with the annotation derive_protocol whose
//                       value has a byte image
//   Decodable<T>        an Encodable class that decode can build
//   Hashable<T>         an Encodable class, or a leaf of the image that is no
//                       class, such as an integer or an enumeration
//   encoded_size_v<T>   the count of bytes of the image of a T
//   hash(value)         the Digest<T> of the value: 64 bits, with no key
//   encode(value, out)  writes the image of the value into out, the ViewMut
//                       of a frame
//   decode<T>(bytes)    the T of an image, or the DecodeFault of the first
//                       part that holds no value
//   format(out, value)  writes each part of the value with its name into a
//                       FixedText, and gives the text or Full<char>
//
// foundation/core/Equatable.h gives equal and order, the annotations
// derive_protocol, local_part and pad_part, and the rules of the walk: the
// parts of a class are its bases and then its members, in declaration order.
//
// Image
//   A value has a byte image when each part has one: an integer, a bool, a
//   character, an enumeration, a float or a double, a bounded C array or a
//   FixedArray of such parts, a sum of such parts (the sums), a FixedList of
//   such parts and a FixedText (the bounded lists), and a class with the
//   annotation derive_protocol.  The walk refuses a pointer, a reference, a union, a
//   named bit-field and each other class, as decode of the Scalar family
//   does.  A local part has no place in the image.  The image holds no
//   padding byte: each part takes the bytes of its value and no more, in
//   declaration order.  A leaf takes its size in bytes, in little-endian
//   order: an integer its bits, a bool one byte of 0 or 1, an enumeration
//   its underlying integer, and a float or a double its IEEE 754 bits.  So
//   one value gives the same image on each target.
//
// Hash
//   hash gives 64 bits with no key.  It is an id inside one process, and a
//   hash of the store that the format version of the store pins, such as
//   the hash of a guard in the merkle hash of a branch.  The hash is a
//   function of the image: a pad part takes part, and a local
//   part does not.  The fold divides the image into blocks of 16 bytes and
//   fills the last block with zero bytes.  Each block is two words, low and
//   high, each of 8 bytes in little-endian order:
//
//     acc = 0x13198a2e03707344
//     for each block: acc = fold(low ^ acc, high ^ 0x243f6a8885a308d3)
//     hash = acc
//
//   fold(a, b) is the low word of the 128-bit product a * b, xored with its
//   high word.  The step and the two seeds are those of foldhash-f (Orson
//   Peters, 2024), so a record whose leaves fill whole blocks gets the bits
//   of foldhash-f with those seeds.  An image of no byte gives the first
//   seed.  The hash of a View of values folds their images one after the
//   other as one image, and then one block with the count of the values in
//   its low word.  The fold reads the words of each leaf at the offsets of
//   the image, with no write of the image: one fold for each 16 bytes, and
//   one shift and one or for each leaf.  It is integer arithmetic only, so it gives
//   the same bits on each target.  A peer that chooses the input can find
//   two values of one hash, so a key that crosses a trust boundary is a
//   keyed hash of the bytes of encode, with its width and its key in its
//   type, and never this hash.
//
// Sums
//   An Option, a Result or a OneOf (foundation/core/Choice.h and OneOf.h)
//   whose alternatives have an image has an image too: one tag byte, and then
//   room for the largest image of its alternatives.  The tag of an Option is 0
//   for none and 1 for some, the tag of a Result is 0 for the value and 1 for
//   the error, and the tag of a OneOf is the index of its alternative.  The
//   active alternative takes the bytes after the tag, and each byte after its
//   image is zero, so one value gives one image.  The hash, the codec and the
//   format read a sum through its match, and decode builds it through its
//   constructors.  decode of a sum needs each alternative to have a default
//   constructor with no throw, and it refuses a tag of no alternative and a
//   byte after the active alternative that is not zero.
//
// Bounded lists
//   A FixedList of N elements with an image, and a FixedText of N characters
//   (foundation/core/List.h and Text.h), have an image too: the count, and
//   then room for N elements.  The count takes the smallest width of 1, 2, 4
//   or 8 bytes that holds N, in little-endian order.  The first count rooms
//   hold the images of the elements in order, and each byte of the other
//   rooms is zero, so one value gives one image.  decode builds the list
//   through its default constructor and its append, so an element needs a
//   default constructor with no throw.  It refuses a count above N and a
//   byte after the last element that is not zero.
//
//   An image holds at most 2^32 - 1 bytes, so the offset of each fault fits
//   the 32 bits of a DecodeFault.
//
// Heap parts
//   A HeapArray of elements with an image of one byte or more, and a
//   HeapText (foundation/core/Array.h and Grow.h), have an image whose size
//   is known only at run time: the count of the elements in 8 bytes, in
//   little-endian order, and then the image of each element in order.  A
//   class with the annotation derive_protocol whose parts have an image, and
//   whose parts hold one or more heap parts, is HeapEncodable.  It is not
//   Encodable, so each type has one image.  Its image holds its parts one
//   after the other, in the order of the image, and a part with an image of
//   fixed size takes the bytes of the image.
//
//   encoded_size(value) gives the count of bytes of the image.  encode writes
//   the image into a View of exactly that count, and an image of more than
//   2^32 - 1 bytes or a View of a different size ends the process.  decode
//   takes the allocation capability, a budget of bytes of storage, and the
//   bytes.  It refuses each fault of the codec, bytes that end inside a part,
//   bytes after the end of the image, more than 2^32 - 1 bytes, and heap
//   parts that need more storage than the budget.  An element needs a
//   default constructor with no throw, and no element is linear.
//
//   decode reads the count of a heap part, and does two checks before it
//   allocates.  First, the elements of the count must fit in the bytes that
//   remain, so the count is at most those bytes divided by the bytes of the
//   image of one element.  Second, the storage of the part must fit in the
//   budget that remains.  The storage is the bytes that the mint of the part
//   allocates: the count times the size of the element, rounded up to the
//   alignment of the array, and the count of the characters of a text.  A
//   local part makes an element larger than its image, so only the budget
//   bounds that storage.  Each refusal names the offset of the count.  Each
//   heap part allocates one time, so the storage of a decode, also at its
//   peak, is at most the budget.  A fault after an allocation gives the
//   storage back.  A budget above the largest count of a View of bytes
//   holds more than one allocation can hold, so decode reads it as that
//   count.
//
//   The hash folds the image as the hash folds an image, and then one block
//   with the count of the bytes of the image in its low word.  A GrowList
//   and a GrowText are builders, and no part: freeze a GrowText into a
//   HeapText first.
//
// Codec
//   encode writes the image into a View of exactly encoded_size_v<T> bytes,
//   so it cannot fail.  decode reads such a View into a T.  It refuses a
//   bool byte other than 0 and 1, a value of a closed enumeration that names
//   no enumerator, a byte of a pad part that is not zero, a part that declares a
//   niche and holds its empty value, a sum of the sums with a tag or a tail
//   byte that holds no value, and a list of the bounded lists with a count or
//   a tail byte that holds no value.  The DecodeFault names the
//   reason and the byte offset of the part in the image.  The first fault in
//   the order of the parts wins.  A local part of the result keeps the value
//   of the default constructor of T.  Decodable refuses a class with a const
//   member and a class that carries the annotation no_start_over_bytes,
//   whose values only its own constructors make.
//
// Format
//   format writes `{name = value, name = value}`, with the parts of each base
//   first.  An integer is written in decimal, a bool as true or false, a
//   float as the text of report(), an enumerator of a scoped enumeration as
//   its name, a value of an open enumeration that names no enumerator in
//   decimal, an array as `[a, b, c]`, an Option as `some(a)` or `none`, a
//   Result as `ok(a)` or `err(e)`, a OneOf as the index of its alternative
//   and its value, `#1(a)`, a FixedList and a HeapArray as `[a, b]`, and a
//   FixedText and a HeapText between two quotes, with a backslash before each
//   quote and each backslash of the text.  A local part and a pad part are
//   not written.  format is no part of a hot path.

#include <foundation/ByteSeal.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Equatable.h>
#include <foundation/core/Format.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Id.h>
#include <foundation/core/List.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Text.h>
#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation::core {

// The reason that decode refuses an image.
enum class DecodeRefusal : std::uint8_t {
    // A bool byte other than 0 and 1.
    bool_byte,
    // A value of a closed enumeration that names no enumerator.
    enumerator,
    // A byte of a pad part that is not zero.
    pad_byte,
    // A part that declares a niche holds its empty value.
    empty_niche,
    // The tag byte of a sum names no alternative (the sums).
    sum_tag,
    // A byte after the active alternative of a sum, or after the last
    // element of a bounded list, is not zero (the sums, the bounded lists).
    tail_byte,
    // The count of a bounded list is more than its capacity (the bounded
    // lists).
    list_count,
    // The bytes end inside a part of an image of run-time size (the heap
    // parts).
    image_end,
    // A byte follows the end of an image of run-time size (the heap parts).
    trailing_byte,
    // An image of run-time size holds more than 2^32 - 1 bytes (the heap
    // parts).
    image_size,
    // The storage of a heap part needs more bytes than remain in the budget
    // of the decode (the heap parts).
    allocation_budget,
};

// The first fault of an image: the reason, and the byte offset of the part
// that holds no value.  Its == is the defaulted compare of each member.  No
// member is a float, so it agrees with equal of the family, and a unit that
// includes this header instantiates no walk for it.  Its pad is two named
// members and no array, so a function that holds a fault on its stack gets
// no stack canary.
class[[= derive_protocol{}]] DecodeFault final {
public:
    constexpr DecodeFault(DecodeRefusal refusal, std::uint32_t offset) noexcept : offset_{offset}, refusal_{refusal} {}

    [[nodiscard]] constexpr DecodeRefusal refusal() const noexcept { return refusal_; }
    [[nodiscard]] constexpr std::uint32_t offset() const noexcept { return offset_; }

    [[nodiscard]] friend constexpr bool operator==(DecodeFault const& left,
                                                   DecodeFault const& right) noexcept = default;

private:
    std::uint32_t offset_ = 0;
    DecodeRefusal refusal_ = DecodeRefusal::bool_byte;
    [[= pad_part{}]] std::uint8_t pad_byte_ = 0;
    [[= pad_part{}]] std::uint16_t pad_word_ = 0;
};

namespace detail {

// True for a FixedArray of the Region family.  An alias reads as the type
// that it names.
[[nodiscard]] consteval bool is_fixed_array_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    return std::meta::is_class_type(type) && std::meta::has_template_arguments(type)
        && std::meta::template_of(type) == ^^FixedArray;
}

[[nodiscard]] consteval std::meta::info fixed_array_element_at(std::meta::info spelled) {
    return std::meta::template_arguments_of(std::meta::remove_cv(std::meta::dealias(spelled)))[0];
}

[[nodiscard]] consteval std::size_t fixed_array_count_at(std::meta::info spelled) {
    return std::meta::extract<std::size_t>(
        std::meta::template_arguments_of(std::meta::remove_cv(std::meta::dealias(spelled)))[1]);
}

// True for a sum (see the sums): an Option, a Result or a OneOf.  An alias
// reads as the type that it names.
[[nodiscard]] consteval bool is_sum_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (!std::meta::is_class_type(type) || !std::meta::has_template_arguments(type)) return false;
    std::meta::info const family = std::meta::template_of(type);
    return family == ^^Option || family == ^^Result || family == ^^OneOf;
}

// The count of the alternatives of a sum, and the alternative at an index:
// the payload of an Option, the value and the error of a Result, and each
// alternative of a OneOf, in the order of the tag.
[[nodiscard]] consteval std::size_t sum_alternative_count_at(std::meta::info spelled) {
    return std::meta::template_arguments_of(std::meta::remove_cv(std::meta::dealias(spelled))).size();
}

[[nodiscard]] consteval std::meta::info sum_alternative_at(std::meta::info spelled, std::size_t index) {
    return std::meta::template_arguments_of(std::meta::remove_cv(std::meta::dealias(spelled)))[index];
}

// True for a bounded list: a FixedList or a FixedText.  An
// alias reads as the type that it names.
[[nodiscard]] consteval bool is_bounded_list_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (!std::meta::is_class_type(type) || !std::meta::has_template_arguments(type)) return false;
    std::meta::info const family = std::meta::template_of(type);
    return family == ^^FixedList || family == ^^FixedText;
}

// The element and the capacity of a bounded list.  The argument of a
// FixedText is its capacity, a value, and its element is char.
[[nodiscard]] consteval std::meta::info bounded_element_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (std::meta::template_of(type) == ^^FixedText) return ^^char;
    return std::meta::template_arguments_of(type)[0];
}

[[nodiscard]] consteval std::size_t bounded_capacity_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    std::size_t const position = std::meta::template_of(type) == ^^FixedText ? 0 : 1;
    return std::meta::extract<std::size_t>(std::meta::template_arguments_of(type)[position]);
}

// The width in bytes of the count of a bounded list of the capacity: the
// smallest of 1, 2, 4 and 8 that holds the capacity.
[[nodiscard]] consteval std::size_t count_bytes_for(std::size_t capacity) {
    if (capacity <= 0xFFU) return 1;
    if (capacity <= 0xFFFFU) return 2;
    if (capacity <= 0xFFFFFFFFU) return 4;
    return 8;
}

// True when a value of the type has a byte image (the image).  Linear in the
// number of parts that the walk reaches.
[[nodiscard]] consteval bool has_image_at(std::meta::info spelled, int depth) {
    if (depth > max_protocol_depth) return false;
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (type == ^^float || type == ^^double) return true;
    if (std::meta::is_floating_point_type(type)) return false;
    if (std::meta::is_integral_type(type) || std::meta::is_enum_type(type)) return true;
    if (std::meta::is_array_type(type)) {
        return std::meta::is_bounded_array_type(type) && has_image_at(std::meta::remove_all_extents(type), depth + 1);
    }
    if (is_fixed_array_at(type)) return has_image_at(fixed_array_element_at(type), depth + 1);
    if (is_sum_at(type)) {
        for (std::size_t index = 0; index < sum_alternative_count_at(type); ++index) {
            if (!has_image_at(sum_alternative_at(type, index), depth + 1)) return false;
        }
        return true;
    }
    if (is_bounded_list_at(type)) return has_image_at(bounded_element_at(type), depth + 1);
    if (!derives_protocol_at(type)) return false;
    std::size_t const bases = base_count(type);
    std::size_t const members = member_count(type);
    for (std::size_t index = 0; index < bases; ++index) {
        if (!has_image_at(base_type_at(type, index), depth + 1)) return false;
    }
    for (std::size_t index = 0; index < members; ++index) {
        if (member_carries_at(type, index, ^^local_part)) continue;
        if (member_is_bit_field_at(type, index) || !has_image_at(member_type_at(type, index), depth + 1)) return false;
    }
    return true;
}

// True when decode can write each part of the image in place: no member that
// the image holds is const, and no class carries the annotation
// no_start_over_bytes.  The caller makes sure that the type has an image.
[[nodiscard]] consteval bool decodes_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (std::meta::is_array_type(type)) return decodes_at(std::meta::remove_all_extents(type));
    if (is_fixed_array_at(type)) return decodes_at(fixed_array_element_at(type));
    if (is_sum_at(type)) {
        for (std::size_t index = 0; index < sum_alternative_count_at(type); ++index) {
            std::meta::info const alternative = sum_alternative_at(type, index);
            if (!std::meta::is_nothrow_default_constructible_type(alternative) || !decodes_at(alternative)) {
                return false;
            }
        }
        return true;
    }
    if (is_bounded_list_at(type)) {
        std::meta::info const element = bounded_element_at(type);
        return std::meta::is_nothrow_default_constructible_type(element) && decodes_at(element);
    }
    if (!derives_protocol_at(type)) return true;
    if (!std::meta::annotations_of_with_type(type, ^^::foundation::lifetime::no_start_over_bytes).empty()) {
        return false;
    }
    std::size_t const bases = base_count(type);
    std::size_t const members = member_count(type);
    for (std::size_t index = 0; index < bases; ++index) {
        if (!decodes_at(base_type_at(type, index))) return false;
    }
    for (std::size_t index = 0; index < members; ++index) {
        if (member_carries_at(type, index, ^^local_part)) continue;
        std::meta::info const member = member_type_at(type, index);
        if (std::meta::is_const_type(member) || !decodes_at(member)) return false;
    }
    return true;
}

// An image holds at most this count of bytes (the bounded lists).  The walk
// keeps each count above it at max_image_bytes + 1, so no sum and no product
// of two counts wraps.
inline constexpr std::size_t max_image_bytes = 0xFFFFFFFFU;

[[nodiscard]] consteval std::size_t clamp_image_bytes_(std::size_t bytes) {
    return bytes > max_image_bytes ? max_image_bytes + 1 : bytes;
}

// The bytes of count images of the given bytes each, clamped.  bytes is at
// most max_image_bytes + 1.
[[nodiscard]] consteval std::size_t image_product_(std::size_t count, std::size_t bytes) {
    if (bytes != 0 && count > max_image_bytes / bytes) return max_image_bytes + 1;
    return count * bytes;
}

// The count of bytes of the image of a type with an image, or
// max_image_bytes + 1 when the image holds more bytes than the cap.
[[nodiscard]] consteval std::size_t image_bytes_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (std::meta::is_array_type(type)) {
        std::meta::info const element = std::meta::remove_extent(type);
        return image_product_(std::meta::size_of(type) / std::meta::size_of(element), image_bytes_at(element));
    }
    if (is_fixed_array_at(type)) {
        return image_product_(fixed_array_count_at(type), image_bytes_at(fixed_array_element_at(type)));
    }
    if (is_sum_at(type)) {
        std::size_t largest = 0;
        for (std::size_t index = 0; index < sum_alternative_count_at(type); ++index) {
            std::size_t const bytes = image_bytes_at(sum_alternative_at(type, index));
            if (bytes > largest) largest = bytes;
        }
        return clamp_image_bytes_(1 + largest);
    }
    if (is_bounded_list_at(type)) {
        std::size_t const capacity = bounded_capacity_at(type);
        return clamp_image_bytes_(count_bytes_for(capacity)
                                  + image_product_(capacity, image_bytes_at(bounded_element_at(type))));
    }
    if (!derives_protocol_at(type)) return std::meta::size_of(type);
    std::size_t total = 0;
    std::size_t const bases = base_count(type);
    std::size_t const members = member_count(type);
    for (std::size_t index = 0; index < bases; ++index) {
        total = clamp_image_bytes_(total + image_bytes_at(base_type_at(type, index)));
    }
    for (std::size_t index = 0; index < members; ++index) {
        if (!member_carries_at(type, index, ^^local_part)) {
            total = clamp_image_bytes_(total + image_bytes_at(member_type_at(type, index)));
        }
    }
    return total;
}

// The byte offset of a base or of a member in the image of its class: the
// bytes of each part before it.  A local member has no place, and the
// caller does not ask for one.
[[nodiscard]] consteval std::size_t base_image_offset_at(std::meta::info type, std::size_t index) {
    std::size_t offset = 0;
    for (std::size_t before = 0; before < index; ++before) {
        offset += image_bytes_at(base_type_at(type, before));
    }
    return offset;
}

[[nodiscard]] consteval std::size_t member_image_offset_at(std::meta::info type, std::size_t index) {
    std::size_t offset = 0;
    std::size_t const bases = base_count(type);
    for (std::size_t before = 0; before < bases; ++before) {
        offset += image_bytes_at(base_type_at(type, before));
    }
    for (std::size_t before = 0; before < index; ++before) {
        if (!member_carries_at(type, before, ^^local_part)) offset += image_bytes_at(member_type_at(type, before));
    }
    return offset;
}

// The count of the members of a class that its image holds: each member
// that is not a local part.
[[nodiscard]] consteval std::size_t image_member_count_at(std::meta::info type) {
    std::size_t count = 0;
    std::size_t const members = member_count(type);
    for (std::size_t index = 0; index < members; ++index) {
        if (!member_carries_at(type, index, ^^local_part)) ++count;
    }
    return count;
}

// The indexes of the members of T that its image holds, in declaration
// order.  The folds of the hash and of encode read them as a pack, so each
// step is one statement of the fold, as in a hand fold.
template <class T>
struct ImageMembers final {
    static constexpr std::size_t count = image_member_count_at(^^T);

    struct List final {
        std::size_t at[count == 0 ? 1 : count];
    };

    static constexpr List list = [] consteval {
        List found{};
        std::size_t next = 0;
        for (std::size_t index = 0; index < member_count(^^T); ++index) {
            if (!member_carries_at(^^T, index, ^^local_part)) found.at[next++] = index;
        }
        return found;
    }();
};

template <class T, std::size_t... Positions>
[[nodiscard]] consteval auto image_members_of_(std::index_sequence<Positions...> /*positions*/) noexcept {
    return std::index_sequence<ImageMembers<T>::list.at[Positions]...>{};
}

// The index sequence of the members of T that its image holds.
template <class T>
using image_member_sequence_ =
    decltype(detail::image_members_of_<T>(std::make_index_sequence<ImageMembers<T>::count>{}));

// The member at Index of an object, to write.
template <std::size_t Index, class T>
[[nodiscard]] constexpr auto& member_mut_at_(T& object) noexcept {
    return object.[:std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())[Index]:];
}

template <std::size_t Index, class T>
[[nodiscard]] constexpr auto& base_mut_at_(T& object) noexcept {
    return object.[:std::meta::bases_of(^^T, std::meta::access_context::unchecked())[Index]:];
}

// The unsigned word of a leaf: its bits, as an unsigned integer of its size.
template <class T>
using LeafWord =
    std::conditional_t<sizeof(T) == 1, std::uint8_t,
                       std::conditional_t<sizeof(T) == 2, std::uint16_t,
                                          std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;

// The bits of a leaf as its unsigned word.  A bool gives 0 or 1, and an
// enumeration the bits of its underlying integer.
template <class T>
[[nodiscard]] constexpr LeafWord<T> leaf_word_(T value) noexcept {
    if constexpr (std::is_same_v<T, bool>) {
        return static_cast<LeafWord<T>>(value ? 1U : 0U);
    } else if constexpr (std::is_enum_v<T>) {
        return __builtin_bit_cast(LeafWord<T>, std::to_underlying(value));
    } else {
        return __builtin_bit_cast(LeafWord<T>, value);
    }
}

// ── The walk of the heap parts ──────────────────────────────────────────

// The bytes of the count of a heap part (the heap parts).
inline constexpr std::size_t heap_count_bytes = 8;

// True for a HeapArray and for a HeapText.  An alias reads as the type that
// it names.
[[nodiscard]] consteval bool is_heap_array_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    return std::meta::is_class_type(type) && std::meta::has_template_arguments(type)
        && std::meta::template_of(type) == ^^HeapArray;
}

[[nodiscard]] consteval bool is_heap_text_at(std::meta::info spelled) {
    return std::meta::remove_cv(std::meta::dealias(spelled)) == ^^HeapText;
}

// The element of a heap part: the element of a HeapArray, and char for a
// HeapText.
[[nodiscard]] consteval std::meta::info heap_element_at(std::meta::info spelled) {
    if (is_heap_text_at(spelled)) return ^^char;
    return std::meta::template_arguments_of(std::meta::remove_cv(std::meta::dealias(spelled)))[0];
}

// True for a class of the family whose image has a run-time size: no image
// of fixed size, and each part with an image of either size.
[[nodiscard]] consteval bool has_heap_image_at(std::meta::info spelled, int depth);

// True when each part of a class has an image of fixed size or of run-time
// size.
[[nodiscard]] consteval bool parts_have_heap_images_at(std::meta::info type, int depth) {
    std::size_t const bases = base_count(type);
    std::size_t const members = member_count(type);
    for (std::size_t index = 0; index < bases; ++index) {
        std::meta::info const base = base_type_at(type, index);
        if (!has_image_at(base, depth + 1) && !has_heap_image_at(base, depth + 1)) return false;
    }
    for (std::size_t index = 0; index < members; ++index) {
        if (member_carries_at(type, index, ^^local_part)) continue;
        std::meta::info const member = member_type_at(type, index);
        if (member_is_bit_field_at(type, index)) return false;
        if (!has_image_at(member, depth + 1) && !has_heap_image_at(member, depth + 1)) return false;
    }
    return true;
}

// A heap part, or a class of the family with no image of fixed size whose
// parts have images.  The element of a HeapArray has an image of one byte
// or more, so a count of elements needs as many bytes or more.  Linear in
// the number of parts that the walk reaches.
[[nodiscard]] consteval bool has_heap_image_at(std::meta::info spelled, int depth) {
    if (depth > max_protocol_depth) return false;
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (is_heap_text_at(type)) return true;
    if (is_heap_array_at(type)) {
        std::meta::info const element = heap_element_at(type);
        if (!has_image_at(element, depth + 1)) return false;
        std::size_t const bytes = image_bytes_at(element);
        return bytes >= 1 && bytes <= max_image_bytes;
    }
    if (!derives_protocol_at(type) || has_image_at(type, depth)) return false;
    return parts_have_heap_images_at(type, depth);
}

// True when decode can build each heap part and write each other part of a
// type with an image of run-time size: an element of a HeapArray has a
// default constructor with no throw, a decode and no linear marker, and no
// class carries a const member or the annotation no_start_over_bytes.
[[nodiscard]] consteval bool heap_decodes_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (is_heap_text_at(type)) return true;
    if (is_heap_array_at(type)) {
        std::meta::info const element = heap_element_at(type);
        return std::meta::is_nothrow_default_constructible_type(element) && decodes_at(element)
            && !::foundation::linear::is_linear_type(element);
    }
    if (!derives_protocol_at(type) || has_image_at(type, 0)) return decodes_at(type);
    if (!std::meta::annotations_of_with_type(type, ^^::foundation::lifetime::no_start_over_bytes).empty()) {
        return false;
    }
    std::size_t const bases = base_count(type);
    std::size_t const members = member_count(type);
    for (std::size_t index = 0; index < bases; ++index) {
        if (!heap_decodes_at(base_type_at(type, index))) return false;
    }
    for (std::size_t index = 0; index < members; ++index) {
        if (member_carries_at(type, index, ^^local_part)) continue;
        std::meta::info const member = member_type_at(type, index);
        if (std::meta::is_const_type(member) || !heap_decodes_at(member)) return false;
    }
    return true;
}

// ── The codec ───────────────────────────────────────────────────────────

template <class T>
constexpr void encode_part_(T const& part, std::byte* first) noexcept;

// Writes the tag of a sum at first and zero bytes in the room after it, so
// each byte after the active alternative is zero (the sums).
template <class Sum>
constexpr void put_sum_tag_(std::byte* first, std::size_t tag) noexcept {
    first[0] = static_cast<std::byte>(static_cast<std::uint8_t>(tag));
    for (std::size_t index = 1; index < image_bytes_at(^^Sum); ++index) {
        first[index] = std::byte{0};
    }
}

// The image of a sum: its tag, and then its active alternative.  The match
// reads the alternative and changes no part of the sum.
template <class T>
constexpr void encode_sum_(Option<T> const& part, std::byte* first) noexcept {
    part.match(
        [first](T const& payload) noexcept {
            detail::put_sum_tag_<Option<T>>(first, 1);
            detail::encode_part_(payload, first + 1);
        },
        [first]() noexcept { detail::put_sum_tag_<Option<T>>(first, 0); });
}

template <class T, class E>
constexpr void encode_sum_(Result<T, E> const& part, std::byte* first) noexcept {
    part.match(
        [first](T const& value) noexcept {
            detail::put_sum_tag_<Result<T, E>>(first, 0);
            detail::encode_part_(value, first + 1);
        },
        [first](E const& error) noexcept {
            detail::put_sum_tag_<Result<T, E>>(first, 1);
            detail::encode_part_(error, first + 1);
        });
}

template <class... Ts>
constexpr void encode_sum_(OneOf<Ts...> const& part, std::byte* first) noexcept {
    detail::put_sum_tag_<OneOf<Ts...>>(first, part.which().raw());
    auto const write = [first](auto const& alternative) noexcept { detail::encode_part_(alternative, first + 1); };
    part.match(((void)sizeof(Ts), write)...);
}

// Writes count in Width bytes from first, in little-endian order.
template <std::size_t Width>
constexpr void put_count_(std::byte* first, std::size_t count) noexcept {
    for (std::size_t index = 0; index < Width; ++index) {
        first[index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(static_cast<std::uint64_t>(count) >> (8 * index)));
    }
}

// The first element of a bounded list, or no element of an empty list.  The
// caller reads only the elements below the count.
template <class T, std::size_t N>
[[nodiscard]] constexpr T const* list_first_(FixedList<T, N> const& list CRUCIBLE_LIFETIMEBOUND) noexcept {
    return first_of_(detail::BaseLayerRead{}, list.view());
}
template <class T, std::size_t N>
void list_first_(FixedList<T, N> const&&) =
    delete("the address names an element of the list, and a temporary list ends with the full expression");

template <std::size_t N>
[[nodiscard]] constexpr char const* list_first_(FixedText<N> const& text CRUCIBLE_LIFETIMEBOUND) noexcept {
    return detail::text_first_(text.view());
}
template <std::size_t N>
void list_first_(FixedText<N> const&&) =
    delete("the address names a character of the text, and a temporary text ends with the full expression");

// The image of a bounded list: its count, the image of each element, and
// zero bytes in each other room (the bounded lists).  The loop runs over the
// rooms of the capacity, a bound that the compiler knows, and it reads no
// element past the count.  Complexity: one store for each byte of the image.
template <class List>
constexpr void encode_list_(List const& part, std::byte* first) noexcept {
    constexpr std::meta::info type = ^^List;
    constexpr std::size_t capacity = bounded_capacity_at(type);
    constexpr std::size_t count_bytes = count_bytes_for(capacity);
    constexpr std::size_t step = image_bytes_at(bounded_element_at(type));
    std::size_t const count = part.size();
    detail::put_count_<count_bytes>(first, count);
    auto const* const elements = detail::list_first_(part);
    for (std::size_t index = 0; index < capacity; ++index) {
        std::byte* const room = first + count_bytes + index * step;
        if (index < count) {
            detail::encode_part_(elements[index], room);
        } else {
            for (std::size_t at = 0; at < step; ++at) {
                room[at] = std::byte{0};
            }
        }
    }
}

template <class T, std::size_t... Bases, std::size_t... Members>
constexpr void encode_parts_([[maybe_unused]] T const& object, [[maybe_unused]] std::byte* first,
                             std::index_sequence<Bases...> /*bases*/,
                             std::index_sequence<Members...> /*members*/) noexcept {
    [[maybe_unused]] constexpr std::meta::info type = ^^T;
    (detail::encode_part_(detail::base_at_<Bases>(object), first + base_image_offset_at(type, Bases)), ...);
    (detail::encode_part_(detail::member_at_<Members>(object), first + member_image_offset_at(type, Members)), ...);
}

// Writes the image of one part from first.  Complexity: one store for each
// byte of the image, which the compiler joins into the stores of words.
template <class T>
constexpr void encode_part_(T const& part, std::byte* first) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (std::is_array_v<Bare>) {
        constexpr std::size_t step = image_bytes_at(^^std::remove_extent_t<Bare>);
        for (std::size_t index = 0; index < std::extent_v<Bare>; ++index) {
            detail::encode_part_(part[index], first + index * step);
        }
    } else if constexpr (is_fixed_array_at(^^Bare)) {
        constexpr std::size_t step = image_bytes_at(fixed_array_element_at(^^Bare));
        std::size_t index = 0;
        for (auto const& element : part.view()) {
            detail::encode_part_(element, first + index * step);
            ++index;
        }
    } else if constexpr (is_sum_at(^^Bare)) {
        detail::encode_sum_(part, first);
    } else if constexpr (is_bounded_list_at(^^Bare)) {
        detail::encode_list_(part, first);
    } else if constexpr (derives_protocol_at(^^Bare)) {
        constexpr std::meta::info type = std::meta::dealias(^^Bare);
        detail::encode_parts_(part, first, std::make_index_sequence<base_count(type)>{},
                              image_member_sequence_<Bare>{});
    } else {
        LeafWord<Bare> const word = detail::leaf_word_(part);
        // The bytes of the word on a little-endian host are its image, so a
        // run-time write is one store of the word.
        if !consteval {
            if constexpr (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) {
                __builtin_memcpy(first, &word, sizeof(word));
                return;
            }
        }
        for (std::size_t index = 0; index < sizeof(Bare); ++index) {
            first[index] = static_cast<std::byte>(static_cast<std::uint8_t>(word >> (8 * index)));
        }
    }
}

// The leaf of the bytes from first, as its unsigned word.  On a
// little-endian host a run-time read is one load of the word.
template <class T>
[[nodiscard]] constexpr LeafWord<T> read_leaf_word_(std::byte const* first) noexcept {
    LeafWord<T> word = 0;
    if !consteval {
        if constexpr (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) {
            __builtin_memcpy(&word, first, sizeof(word));
            return word;
        }
    }
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        word = static_cast<LeafWord<T>>(
            word | (static_cast<LeafWord<T>>(static_cast<std::uint8_t>(first[index])) << (8 * index)));
    }
    return word;
}

template <class T>
[[nodiscard]] constexpr bool decode_part_(T& part, std::byte const* first, std::size_t offset,
                                          DecodeFault& fault) noexcept;

// True when each of the count bytes from first is zero.
[[nodiscard]] constexpr bool are_zero_bytes_(std::byte const* first, std::size_t count) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (first[index] != std::byte{0}) return false;
    }
    return true;
}

// The fault at offset.  An image of a class is far below 4 GiB, so the
// offset fits 32 bits.
[[nodiscard]] constexpr DecodeFault fault_at_(DecodeRefusal refusal, std::size_t offset) noexcept {
    return DecodeFault{refusal, static_cast<std::uint32_t>(offset)};
}

template <class T, std::size_t Index>
[[nodiscard]] constexpr bool decode_member_(T& object, std::byte const* first, std::size_t offset,
                                            DecodeFault& fault) noexcept {
    constexpr std::meta::info type = ^^T;
    if constexpr (member_carries_at(type, Index, ^^local_part)) {
        return true;
    } else {
        constexpr std::size_t at = member_image_offset_at(type, Index);
        if constexpr (member_carries_at(type, Index, ^^pad_part)) {
            if (!are_zero_bytes_(first + at, image_bytes_at(member_type_at(type, Index)))) {
                fault = fault_at_(DecodeRefusal::pad_byte, offset + at);
                return false;
            }
        }
        return detail::decode_part_(detail::member_mut_at_<Index>(object), first + at, offset + at, fault);
    }
}

// Reads the alternative Alt of a sum from the bytes after its tag into value,
// and refuses a byte after its image in the room of the sum that is not zero
// (the sums).  first is the tag byte of the sum, at offset in the image.
template <class Sum, class Alt>
[[nodiscard]] constexpr bool decode_alternative_(Alt& value, std::byte const* first, std::size_t offset,
                                                 DecodeFault& fault) noexcept {
    constexpr std::size_t room = image_bytes_at(^^Sum) - 1;
    constexpr std::size_t taken = image_bytes_at(^^Alt);
    if (!detail::decode_part_(value, first + 1, offset + 1, fault)) return false;
    if (!are_zero_bytes_(first + 1 + taken, room - taken)) {
        fault = fault_at_(DecodeRefusal::tail_byte, offset + 1 + taken);
        return false;
    }
    return true;
}

template <class T>
[[nodiscard]] constexpr bool decode_sum_(Option<T>& part, std::byte const* first, std::size_t offset,
                                         DecodeFault& fault) noexcept {
    std::uint8_t const tag = static_cast<std::uint8_t>(first[0]);
    if (tag == 0) {
        if (!are_zero_bytes_(first + 1, image_bytes_at(^^T))) {
            fault = fault_at_(DecodeRefusal::tail_byte, offset + 1);
            return false;
        }
        part = Option<T>{none};
        return true;
    }
    if (tag != 1) {
        fault = fault_at_(DecodeRefusal::sum_tag, offset);
        return false;
    }
    T value{};
    if (!detail::decode_alternative_<Option<T>>(value, first, offset, fault)) return false;
    part = Option<T>::some(static_cast<T&&>(value));
    return true;
}

template <class T, class E>
[[nodiscard]] constexpr bool decode_sum_(Result<T, E>& part, std::byte const* first, std::size_t offset,
                                         DecodeFault& fault) noexcept {
    std::uint8_t const tag = static_cast<std::uint8_t>(first[0]);
    if (tag == 0) {
        T value{};
        if (!detail::decode_alternative_<Result<T, E>>(value, first, offset, fault)) return false;
        part = Result<T, E>{static_cast<T&&>(value)};
        return true;
    }
    if (tag == 1) {
        E error{};
        if (!detail::decode_alternative_<Result<T, E>>(error, first, offset, fault)) return false;
        part = Result<T, E>{Err<E>{static_cast<E&&>(error)}};
        return true;
    }
    fault = fault_at_(DecodeRefusal::sum_tag, offset);
    return false;
}

// Reads the alternative at Index when the tag names it.  The fold over the
// indexes reads the one alternative of the tag.
template <class Sum, std::size_t Index>
[[nodiscard]] constexpr bool decode_one_of_at_(Sum& part, std::uint8_t tag, std::byte const* first, std::size_t offset,
                                               DecodeFault& fault) noexcept {
    if (tag != Index) return true;
    using Alt = typename[:sum_alternative_at(^^Sum, Index):];
    Alt value{};
    if (!detail::decode_alternative_<Sum>(value, first, offset, fault)) return false;
    part = Sum{static_cast<Alt&&>(value)};
    return true;
}

template <class Sum, std::size_t... Indexes>
[[nodiscard]] constexpr bool decode_one_of_(Sum& part, std::uint8_t tag, std::byte const* first, std::size_t offset,
                                            DecodeFault& fault, std::index_sequence<Indexes...> /*indexes*/) noexcept {
    return (detail::decode_one_of_at_<Sum, Indexes>(part, tag, first, offset, fault) && ...);
}

template <class... Ts>
[[nodiscard]] constexpr bool decode_sum_(OneOf<Ts...>& part, std::byte const* first, std::size_t offset,
                                         DecodeFault& fault) noexcept {
    std::uint8_t const tag = static_cast<std::uint8_t>(first[0]);
    if (tag >= sizeof...(Ts)) {
        fault = fault_at_(DecodeRefusal::sum_tag, offset);
        return false;
    }
    return detail::decode_one_of_<OneOf<Ts...>>(part, tag, first, offset, fault, std::index_sequence_for<Ts...>{});
}

// Reads Width bytes from first as a count, in little-endian order.
template <std::size_t Width>
[[nodiscard]] constexpr std::uint64_t read_count_(std::byte const* first) noexcept {
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < Width; ++index) {
        count |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(first[index])) << (8 * index);
    }
    return count;
}

// Puts one decoded element after the last element of a list.  The caller
// read a count of at most the capacity, so the list has room.
template <class T, std::size_t N>
constexpr void put_element_(FixedList<T, N>& list, T&& element) noexcept {
    bool const is_put = list.append(static_cast<T&&>(element)).is_ok();
    if (!is_put) ::foundation::core::unreachable();
}

template <std::size_t N>
constexpr void put_element_(FixedText<N>& text, char&& element) noexcept {
    bool const is_put = text.append(element).is_ok();
    if (!is_put) ::foundation::core::unreachable();
}

// Reads a bounded list: its count, each element in order, and zero bytes in
// each other room (the bounded lists).  The list grows through its append,
// and part takes it at the end.  Complexity: one load for each byte of the
// image, and one append for each element.
template <class List>
[[nodiscard]] constexpr bool decode_list_(List& part, std::byte const* first, std::size_t offset,
                                          DecodeFault& fault) noexcept {
    constexpr std::meta::info type = ^^List;
    constexpr std::size_t capacity = bounded_capacity_at(type);
    constexpr std::size_t count_bytes = count_bytes_for(capacity);
    constexpr std::size_t total = image_bytes_at(type);
    using Element = typename[:bounded_element_at(type):];
    constexpr std::size_t step = image_bytes_at(^^Element);
    std::uint64_t const count = detail::read_count_<count_bytes>(first);
    if (count > capacity) {
        fault = fault_at_(DecodeRefusal::list_count, offset);
        return false;
    }
    List list{};
    std::size_t at = count_bytes;
    for (std::uint64_t index = 0; index < count; ++index) {
        Element element{};
        if (!detail::decode_part_(element, first + at, offset + at, fault)) return false;
        detail::put_element_(list, static_cast<Element&&>(element));
        at += step;
    }
    if (!are_zero_bytes_(first + at, total - at)) {
        fault = fault_at_(DecodeRefusal::tail_byte, offset + at);
        return false;
    }
    part = static_cast<List&&>(list);
    return true;
}

template <class T, std::size_t... Bases, std::size_t... Members>
[[nodiscard]] constexpr bool decode_parts_([[maybe_unused]] T& object, [[maybe_unused]] std::byte const* first,
                                           [[maybe_unused]] std::size_t offset, [[maybe_unused]] DecodeFault& fault,
                                           std::index_sequence<Bases...> /*bases*/,
                                           std::index_sequence<Members...> /*members*/) noexcept {
    [[maybe_unused]] constexpr std::meta::info type = ^^T;
    return (detail::decode_part_(detail::base_mut_at_<Bases>(object), first + base_image_offset_at(type, Bases),
                                 offset + base_image_offset_at(type, Bases), fault)
            && ...)
        && (detail::decode_member_<T, Members>(object, first, offset, fault) && ...);
}

// Reads one part from the bytes from first, which are at offset in the
// image.  Gives false, with the fault written, at the first part that holds
// no value.  Complexity: one load for each byte, which the compiler joins
// into the loads of words, and one test for each bool, enumeration and pad
// byte.
template <class T>
[[nodiscard]] constexpr bool decode_part_(T& part, std::byte const* first, std::size_t offset,
                                          DecodeFault& fault) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (std::is_array_v<Bare>) {
        constexpr std::size_t step = image_bytes_at(^^std::remove_extent_t<Bare>);
        for (std::size_t index = 0; index < std::extent_v<Bare>; ++index) {
            if (!detail::decode_part_(part[index], first + index * step, offset + index * step, fault)) return false;
        }
        return true;
    } else if constexpr (is_fixed_array_at(^^Bare)) {
        constexpr std::size_t step = image_bytes_at(fixed_array_element_at(^^Bare));
        for (auto const index : Bare::indices()) {
            std::size_t const at = index.raw() * step;
            bool const is_decoded =
                part.with_element(index, [first, offset, at, &fault](typename Bare::element_type& element) noexcept {
                    return detail::decode_part_(element, first + at, offset + at, fault);
                });
            if (!is_decoded) return false;
        }
        return true;
    } else if constexpr (is_sum_at(^^Bare)) {
        return detail::decode_sum_(part, first, offset, fault);
    } else if constexpr (is_bounded_list_at(^^Bare)) {
        return detail::decode_list_(part, first, offset, fault);
    } else if constexpr (derives_protocol_at(^^Bare)) {
        constexpr std::meta::info type = std::meta::dealias(^^Bare);
        if (!detail::decode_parts_(part, first, offset, fault, std::make_index_sequence<base_count(type)>{},
                                   std::make_index_sequence<member_count(type)>{})) {
            return false;
        }
        if constexpr (declares_niche_at(type)) {
            if (NicheRead<Bare>::holds_empty(part)) {
                fault = fault_at_(DecodeRefusal::empty_niche, offset);
                return false;
            }
        }
        return true;
    } else if constexpr (std::is_same_v<Bare, bool>) {
        std::uint8_t const byte = detail::read_leaf_word_<bool>(first);
        if (byte > 1) {
            fault = fault_at_(DecodeRefusal::bool_byte, offset);
            return false;
        }
        part = byte == 1;
        return true;
    } else if constexpr (std::is_enum_v<Bare>) {
        using Underlying = std::underlying_type_t<Bare>;
        Underlying const raw = __builtin_bit_cast(Underlying, detail::read_leaf_word_<Bare>(first));
        if constexpr (is_closed_enumeration_at(^^Bare)) {
            if (!detail::names_enumerator<Bare>(raw)) {
                fault = fault_at_(DecodeRefusal::enumerator, offset);
                return false;
            }
        }
        part = static_cast<Bare>(raw);
        return true;
    } else {
        part = __builtin_bit_cast(Bare, detail::read_leaf_word_<Bare>(first));
        return true;
    }
}

// ── The hash ────────────────────────────────────────────────────────────

// The two seeds of the fold: the first two words of the fraction of pi in
// hex, which foldhash takes as its seeds.
inline constexpr std::uint64_t hash_fold_seed = 0x243f6a8885a308d3ULL;
inline constexpr std::uint64_t hash_start_seed = 0x13198a2e03707344ULL;

// The bytes of one block of the fold.
inline constexpr std::size_t hash_block_bytes = 16;

// An array of at most this count of elements folds each element at its own
// offset.  A longer array folds in a loop.
inline constexpr std::size_t hash_unroll_limit = 16;

// The low word and the high word of the full product, xored.
[[nodiscard]] constexpr std::uint64_t folded_multiply_(std::uint64_t left, std::uint64_t right) noexcept {
    WideProduct<std::uint64_t> const product = widening_mul(left, right);
    return product.low ^ product.high;
}

// The state of the fold: the accumulator, and the two little-endian words of
// the block that the fold fills.  The position in the block is a constant of
// each caller, so the state holds no position.
struct HashState final {
    std::uint64_t accumulator = hash_start_seed;
    std::uint64_t low = 0;
    std::uint64_t high = 0;

    // Folds the block into the accumulator, and empties the block.
    constexpr void fold_block() noexcept {
        accumulator = folded_multiply_(low ^ accumulator, high ^ hash_fold_seed);
        low = 0;
        high = 0;
    }
};

// Puts the low Width bytes of word at the byte Position of the block.  The
// bytes past the end of the block are not put.
template <std::size_t Position, std::size_t Width>
    requires(Position < hash_block_bytes && Width <= sizeof(std::uint64_t))
constexpr void put_in_block_(HashState& state, std::uint64_t word) noexcept {
    if constexpr (Position >= sizeof(std::uint64_t)) {
        state.high |= word << (8 * (Position - sizeof(std::uint64_t)));
    } else if constexpr (Position + Width <= sizeof(std::uint64_t)) {
        state.low |= word << (8 * Position);
    } else {
        state.low |= word << (8 * Position);
        state.high |= word >> (8 * (sizeof(std::uint64_t) - Position));
    }
}

// Puts the Width bytes of a leaf at the byte Offset of the image.  A leaf
// that fills its block folds the block.  A leaf that crosses the end of its
// block puts its first bytes, folds the block, and puts its last bytes at the
// start of the next block.
template <std::size_t Offset, std::size_t Width>
constexpr void put_leaf_(HashState& state, std::uint64_t word) noexcept {
    constexpr std::size_t position = Offset % hash_block_bytes;
    if constexpr (position + Width < hash_block_bytes) {
        detail::put_in_block_<position, Width>(state, word);
    } else if constexpr (position + Width == hash_block_bytes) {
        detail::put_in_block_<position, Width>(state, word);
        state.fold_block();
    } else {
        constexpr std::size_t first = hash_block_bytes - position;
        detail::put_in_block_<position, first>(state, word);
        state.fold_block();
        detail::put_in_block_<0, Width - first>(state, word >> (8 * first));
    }
}

// Puts one byte at a position of the block that the caller holds, and folds
// the block when the byte fills it.
constexpr void put_byte_(HashState& state, std::size_t& position, std::byte byte) noexcept {
    std::uint64_t const word = static_cast<std::uint8_t>(byte);
    if (position < sizeof(std::uint64_t)) {
        state.low |= word << (8 * position);
    } else {
        state.high |= word << (8 * (position - sizeof(std::uint64_t)));
    }
    if (++position == hash_block_bytes) {
        state.fold_block();
        position = 0;
    }
}

// The writers of the images of parts (the definitions below).
// ProtocolFrameKeyDoor names them as friends, so these declarations stand
// before the door.
template <std::size_t Offset, class Element, std::size_t Count>
constexpr void hash_array_(HashState& state, Element const* first) noexcept;

template <std::size_t Offset, class T>
constexpr void hash_part_(HashState& state, T const& part) noexcept;

template <class T>
constexpr void hash_run_(HashState& state, T const* first, std::size_t count) noexcept;

struct HeapCursor;
struct HeapFold;

template <class T, class Marker>
[[nodiscard]] bool decode_heap_part_(T& part, Marker allocation, HeapCursor& cursor, DecodeFault& fault) noexcept;

template <class T>
constexpr void fold_fixed_part_(HeapFold& fold, T const& part) noexcept;

}  // namespace detail

// The door of the writers of the images of parts to the key of the frame
// doors (the doors of the owners above foundation/core/Frame.h).  Its one
// member is private, and hash_array_, hash_part_, hash_run_,
// decode_heap_part_ and fold_fixed_part_ are its friends.  The invariant: a
// writer reads the run of an array that it builds in its own frame, the
// image of one part or the new HeapArray of a heap part, and no other name
// reaches that array while the writer runs.  A home owner row of
// utils/rule-table.txt keeps this definition in this header.
class ProtocolFrameKeyDoor final : ::foundation::NoObject<ProtocolFrameKeyDoor> {
    template <std::size_t Offset, class Element, std::size_t Count>
    friend constexpr void detail::hash_array_(detail::HashState& state, Element const* first) noexcept;
    template <std::size_t Offset, class T>
    friend constexpr void detail::hash_part_(detail::HashState& state, T const& part) noexcept;
    template <class T>
    friend constexpr void detail::hash_run_(detail::HashState& state, T const* first, std::size_t count) noexcept;
    template <class T, class Marker>
    friend auto detail::decode_heap_part_(T& part, Marker allocation, detail::HeapCursor& cursor,
                                          DecodeFault& fault) noexcept -> bool;
    template <class T>
    friend constexpr void detail::fold_fixed_part_(detail::HeapFold& fold, T const& part) noexcept;

    // The key of the frame doors, for the run of an array of a writer.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

namespace detail {

// The fold of a class: each base and each member of its image, at its offset
// in the image.
template <std::size_t Offset, class T, std::size_t... Bases, std::size_t... Members>
constexpr void hash_parts_([[maybe_unused]] HashState& state, [[maybe_unused]] T const& object,
                           std::index_sequence<Bases...> /*bases*/,
                           std::index_sequence<Members...> /*members*/) noexcept {
    [[maybe_unused]] constexpr std::meta::info type = ^^T;
    (detail::hash_part_<Offset + base_image_offset_at(type, Bases)>(state, detail::base_at_<Bases>(object)), ...);
    (detail::hash_part_<Offset + member_image_offset_at(type, Members)>(state, detail::member_at_<Members>(object)),
     ...);
}

// The elements at Indexes of an array of elements of Step bytes, from first,
// each at its offset in the image.
template <std::size_t Offset, std::size_t Step, class Element, std::size_t... Indexes>
constexpr void hash_elements_([[maybe_unused]] HashState& state, [[maybe_unused]] Element const* first,
                              std::index_sequence<Indexes...> /*indexes*/) noexcept {
    (detail::hash_part_<Offset + Indexes * Step>(state, first[Indexes]), ...);
}

// The leaves of one full block, from first, which start the block.
template <std::size_t Step, class Element, std::size_t... Lanes>
constexpr void fill_block_(HashState& state, Element const* first, std::index_sequence<Lanes...> /*lanes*/) noexcept {
    (detail::put_in_block_<Lanes * Step, Step>(state, static_cast<std::uint64_t>(detail::leaf_word_(first[Lanes]))),
     ...);
}

// True for a leaf of the image: an integer, a bool, a character, a float, a
// double or an enumeration.  Each other part with an image is an array, a
// sum, a list or a class, and its bytes are no leaf word.
template <class T>
inline constexpr bool is_hash_leaf_ = std::is_arithmetic_v<T> || std::is_enum_v<T>;

// The fold of Count elements from first, at the byte Offset of the image.
// A short array folds each element at its offset.  A long array of leaves
// whose first leaf starts at a multiple of its size folds its leaves block
// by block in a loop.  Each other long array writes the image of each
// element and folds its bytes at a position that the loop holds.
// Complexity: one step for each byte of the image of the array.
template <std::size_t Offset, class Element, std::size_t Count>
constexpr void hash_array_(HashState& state, Element const* first) noexcept {
    constexpr std::size_t step = image_bytes_at(^^Element);
    if constexpr (Count <= hash_unroll_limit) {
        detail::hash_elements_<Offset, step>(state, first, std::make_index_sequence<Count>{});
    } else if constexpr (is_hash_leaf_<Element> && hash_block_bytes % step == 0 && Offset % step == 0) {
        constexpr std::size_t per_block = hash_block_bytes / step;
        constexpr std::size_t head = (hash_block_bytes - Offset % hash_block_bytes) % hash_block_bytes / step;
        constexpr std::size_t blocks = (Count - head) / per_block;
        constexpr std::size_t tail_first = head + blocks * per_block;
        detail::hash_elements_<Offset, step>(state, first, std::make_index_sequence<head>{});
        for (std::size_t block = 0; block < blocks; ++block) {
            detail::fill_block_<step>(state, first + head + block * per_block, std::make_index_sequence<per_block>{});
            state.fold_block();
        }
        detail::hash_elements_<Offset + tail_first * step, step>(state, first + tail_first,
                                                                 std::make_index_sequence<Count - tail_first>{});
    } else {
        std::size_t position = Offset % hash_block_bytes;
        for (std::size_t index = 0; index < Count; ++index) {
            FixedArray<std::byte, step> image{};
            detail::encode_part_(first[index],
                                 first_of_(detail::BaseLayerRead{}, frame_run_(ProtocolFrameKeyDoor::key_(), image)));
            for (std::byte const byte : image.view()) {
                detail::put_byte_(state, position, byte);
            }
        }
    }
}

// The fold of one part at the byte Offset of the image (the hash).
template <std::size_t Offset, class T>
constexpr void hash_part_(HashState& state, T const& part) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (std::is_array_v<Bare>) {
        detail::hash_array_<Offset, std::remove_extent_t<Bare>, std::extent_v<Bare>>(state, part);
    } else if constexpr (is_fixed_array_at(^^Bare)) {
        detail::hash_array_<Offset, typename[:fixed_array_element_at(^^Bare):], fixed_array_count_at(^^Bare)>(
            state, first_of_(detail::BaseLayerRead{}, part.view()));
    } else if constexpr (is_sum_at(^^Bare) || is_bounded_list_at(^^Bare)) {
        // The active alternative of a sum and the count of a list are known
        // only at run time, so the fold writes the image of the part on the
        // stack and folds it as an array of bytes at the same offset.
        constexpr std::size_t bytes = image_bytes_at(^^Bare);
        FixedArray<std::byte, bytes> image{};
        std::byte* const first = first_of_(detail::BaseLayerRead{}, frame_run_(ProtocolFrameKeyDoor::key_(), image));
        detail::encode_part_(part, first);
        detail::hash_array_<Offset, std::byte, bytes>(state, first);
    } else if constexpr (derives_protocol_at(^^Bare)) {
        constexpr std::meta::info type = std::meta::dealias(^^Bare);
        detail::hash_parts_<Offset>(state, part, std::make_index_sequence<base_count(type)>{},
                                    image_member_sequence_<Bare>{});
    } else {
        detail::put_leaf_<Offset, sizeof(Bare)>(state, static_cast<std::uint64_t>(detail::leaf_word_(part)));
    }
}

// The fold of count elements from first as one image, and then one block
// with the count in its low word (the hash of a run).  An element whose
// image holds a whole number of blocks folds at the start of its block.
// When 16 is a multiple of the size of an image, each group of elements
// that fills a block folds with the offsets of its elements.  Each other
// element writes its image and folds its bytes at a position that the loop
// holds.  Complexity: one fold for each 16 bytes of the images, and one more.
template <class T>
constexpr void hash_run_(HashState& state, T const* first, std::size_t count) noexcept {
    constexpr std::size_t step = image_bytes_at(^^T);
    if constexpr (step != 0) {
        std::size_t index = 0;
        if constexpr (step % hash_block_bytes == 0) {
            for (; index < count; ++index) {
                detail::hash_part_<0>(state, first[index]);
            }
        } else if constexpr (hash_block_bytes % step == 0) {
            constexpr std::size_t per_block = hash_block_bytes / step;
            for (; index + per_block <= count; index += per_block) {
                detail::hash_elements_<0, step>(state, first + index, std::make_index_sequence<per_block>{});
            }
        }
        std::size_t position = 0;
        for (; index < count; ++index) {
            FixedArray<std::byte, step> image{};
            detail::encode_part_(first[index],
                                 first_of_(detail::BaseLayerRead{}, frame_run_(ProtocolFrameKeyDoor::key_(), image)));
            for (std::byte const byte : image.view()) {
                detail::put_byte_(state, position, byte);
            }
        }
        if (position != 0) state.fold_block();
    }
    state.low = static_cast<std::uint64_t>(count);
    state.fold_block();
}

// ── The heap parts ──────────────────────────────────────────────────────

// The count of bytes of the image of one part of a value with an image of
// run-time size (the heap parts).  Complexity: one step for each part of
// the walk, and none for each element.
template <class T>
[[nodiscard]] constexpr std::size_t heap_size_(T const& part) noexcept;

template <class T, std::size_t... Bases, std::size_t... Members>
[[nodiscard]] constexpr std::size_t heap_parts_size_([[maybe_unused]] T const& object,
                                                     std::index_sequence<Bases...> /*bases*/,
                                                     std::index_sequence<Members...> /*members*/) noexcept {
    return (std::size_t{0} + ... + detail::heap_size_(detail::base_at_<Bases>(object)))
         + (std::size_t{0} + ... + detail::heap_size_(detail::member_at_<Members>(object)));
}

template <class T>
[[nodiscard]] constexpr std::size_t heap_size_(T const& part) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (is_heap_text_at(^^Bare)) {
        return heap_count_bytes + part.size();
    } else if constexpr (is_heap_array_at(^^Bare)) {
        return heap_count_bytes + part.size() * image_bytes_at(heap_element_at(^^Bare));
    } else if constexpr (has_heap_image_at(^^Bare, 0)) {
        constexpr std::meta::info type = std::meta::dealias(^^Bare);
        return detail::heap_parts_size_(part, std::make_index_sequence<base_count(type)>{},
                                        image_member_sequence_<Bare>{});
    } else {
        return image_bytes_at(^^Bare);
    }
}

// Writes the image of one part from first, and gives the first byte after
// it.  A part with an image of fixed size writes the image.  Complexity: one
// store for each byte of the image.
template <class T>
constexpr std::byte* encode_heap_part_(T const& part, std::byte* first) noexcept;

template <class T, std::size_t... Bases, std::size_t... Members>
constexpr std::byte* encode_heap_parts_([[maybe_unused]] T const& object, std::byte* first,
                                        std::index_sequence<Bases...> /*bases*/,
                                        std::index_sequence<Members...> /*members*/) noexcept {
    ((first = detail::encode_heap_part_(detail::base_at_<Bases>(object), first)), ...);
    ((first = detail::encode_heap_part_(detail::member_at_<Members>(object), first)), ...);
    return first;
}

template <class T>
constexpr std::byte* encode_heap_part_(T const& part, std::byte* first) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (is_heap_text_at(^^Bare)) {
        detail::put_count_<heap_count_bytes>(first, part.size());
        std::byte* at = first + heap_count_bytes;
        for (char const character : part.view()) {
            *at = static_cast<std::byte>(static_cast<unsigned char>(character));
            ++at;
        }
        return at;
    } else if constexpr (is_heap_array_at(^^Bare)) {
        constexpr std::size_t step = image_bytes_at(heap_element_at(^^Bare));
        detail::put_count_<heap_count_bytes>(first, part.size());
        std::byte* at = first + heap_count_bytes;
        for (auto const& element : part.view()) {
            detail::encode_part_(element, at);
            at += step;
        }
        return at;
    } else if constexpr (has_heap_image_at(^^Bare, 0)) {
        constexpr std::meta::info type = std::meta::dealias(^^Bare);
        return detail::encode_heap_parts_(part, first, std::make_index_sequence<base_count(type)>{},
                                          image_member_sequence_<Bare>{});
    } else {
        detail::encode_part_(part, first);
        return first + image_bytes_at(^^Bare);
    }
}

// The place of the decode in the bytes of an image of run-time size: the
// next byte, the count of the bytes after it, its offset in the image, and
// the bytes of storage that the heap parts can still allocate.
struct HeapCursor final {
    std::byte const* first = nullptr;
    std::size_t remaining = 0;
    std::size_t offset = 0;
    std::size_t budget = 0;

    constexpr void advance(std::size_t bytes) noexcept {
        first += bytes;
        remaining -= bytes;
        offset += bytes;
    }
};

// The budget of a decode as a count of bytes, at most the largest count of a
// View of bytes (the heap parts).  A larger budget holds more bytes than one
// allocation can give, so the mint of a part that fits it never ends the
// process for its size.
[[nodiscard]] constexpr std::size_t heap_budget_of_(Quantity<unit::Byte, std::size_t> budget) noexcept {
    std::size_t const largest = max_view_count(1);
    return budget.raw() < largest ? budget.raw() : largest;
}

// Takes the storage of count elements from the budget of the cursor: the
// bytes that the mint of a HeapArray allocates for count elements of
// ElementBytes bytes at the alignment Align.  That is the bytes of the
// elements rounded up to the alignment, and zero for no element.  Gives
// false, and takes nothing, when the storage needs more bytes than the
// budget holds.  The first compare divides the budget, so no product
// overflows.  Complexity: O(1), with a division by a constant.
template <std::size_t ElementBytes, std::size_t Align>
[[nodiscard]] constexpr bool take_heap_storage_(HeapCursor& cursor, std::uint64_t count) noexcept {
    if (count > cursor.budget / ElementBytes) return false;
    std::size_t const bytes = count * ElementBytes;
    std::size_t const padding = (Align - bytes % Align) % Align;
    if (padding > cursor.budget - bytes) return false;
    cursor.budget -= bytes + padding;
    return true;
}

// Reads the count of a heap part whose elements take Step bytes of the image
// and ElementBytes bytes of storage at the alignment Align.  It refuses bytes
// that end before the count or before the elements of the count, and then a
// storage of the elements that needs more bytes than the budget holds.  Each
// refusal names the offset of the count.  Gives the count, takes its storage
// from the budget, and moves the cursor past the count.
template <std::size_t Step, std::size_t ElementBytes, std::size_t Align>
[[nodiscard]] constexpr bool read_heap_count_(HeapCursor& cursor, std::uint64_t& count, DecodeFault& fault) noexcept {
    if (cursor.remaining < heap_count_bytes) {
        fault = fault_at_(DecodeRefusal::image_end, cursor.offset);
        return false;
    }
    count = detail::read_count_<heap_count_bytes>(cursor.first);
    if (count > (cursor.remaining - heap_count_bytes) / Step) {
        fault = fault_at_(DecodeRefusal::image_end, cursor.offset);
        return false;
    }
    if (!detail::take_heap_storage_<ElementBytes, Align>(cursor, count)) {
        fault = fault_at_(DecodeRefusal::allocation_budget, cursor.offset);
        return false;
    }
    cursor.advance(heap_count_bytes);
    return true;
}

template <class T, std::size_t Index, class Marker>
[[nodiscard]] bool decode_heap_member_(T& object, Marker allocation, HeapCursor& cursor, DecodeFault& fault) noexcept {
    constexpr std::meta::info type = ^^T;
    if constexpr (member_carries_at(type, Index, ^^local_part)) {
        return true;
    } else {
        if constexpr (member_carries_at(type, Index, ^^pad_part)) {
            constexpr std::size_t bytes = image_bytes_at(member_type_at(type, Index));
            if (cursor.remaining >= bytes && !are_zero_bytes_(cursor.first, bytes)) {
                fault = fault_at_(DecodeRefusal::pad_byte, cursor.offset);
                return false;
            }
        }
        return detail::decode_heap_part_(detail::member_mut_at_<Index>(object), allocation, cursor, fault);
    }
}

template <class T, class Marker, std::size_t... Bases, std::size_t... Members>
[[nodiscard]] bool decode_heap_parts_([[maybe_unused]] T& object, [[maybe_unused]] Marker allocation,
                                      [[maybe_unused]] HeapCursor& cursor, [[maybe_unused]] DecodeFault& fault,
                                      std::index_sequence<Bases...> /*bases*/,
                                      std::index_sequence<Members...> /*members*/) noexcept {
    return (detail::decode_heap_part_(detail::base_mut_at_<Bases>(object), allocation, cursor, fault) && ...)
        && (detail::decode_heap_member_<T, Members>(object, allocation, cursor, fault) && ...);
}

// The count characters of the bytes from first, in a GrowText of at most
// count characters.  One append puts all the characters, so the text
// allocates one time, exactly count bytes, and nothing for no character.  A
// char reads each byte of an object, so the characters are the bytes.  The
// text comes out as a value, so a freeze takes it with no cast of the text.
// The caller checked that the bytes hold count characters.  The bytes come
// from an image, which is outside data, so their TextView comes through
// mint_foreign_text.  Complexity: one allocation and one copy of the
// characters.
template <class Marker>
[[nodiscard]] GrowText grow_text_of_(Marker allocation, std::byte const* first, std::uint64_t count) noexcept {
    GrowText text = mint_grow_text(count);
    TextView const characters =
        mint_foreign_text(static_cast<char const*>(static_cast<void const*>(first)), static_cast<std::size_t>(count))
            .characters();
    if (text.append(allocation, characters).is_err()) ::foundation::core::unreachable();
    return text;
}

// Reads one part at the cursor, and moves the cursor past it.  Gives false,
// with the fault written, at the first part that holds no value.  A heap
// part allocates only after its count passed the check of the bytes that
// remain and the check of the budget.  An array that a later fault drops
// gives its storage back at the end of its scope.  Complexity: one load for
// each byte of the image, and one allocation for each heap part.
template <class T, class Marker>
[[nodiscard]] bool decode_heap_part_(T& part, Marker allocation, HeapCursor& cursor, DecodeFault& fault) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (is_heap_text_at(^^Bare)) {
        std::uint64_t count = 0;
        if (!detail::read_heap_count_<1, 1, 1>(cursor, count, fault)) return false;
        part = freeze(detail::grow_text_of_(allocation, cursor.first, count));
        cursor.advance(count);
        return true;
    } else if constexpr (is_heap_array_at(^^Bare)) {
        using Element = typename[:heap_element_at(^^Bare):];
        constexpr std::size_t step = image_bytes_at(^^Element);
        constexpr std::size_t align =
            std::meta::extract<std::size_t>(std::meta::template_arguments_of(std::meta::dealias(^^Bare))[1]);
        std::uint64_t count = 0;
        if (!detail::read_heap_count_<step, sizeof(Element), align>(cursor, count, fault)) return false;
        auto build = [](std::size_t /*position*/) noexcept { return Element{}; };
        Bare array = mint_heap_array_with<Element, align>(allocation, count, build);
        // The decode writes each element in turn and returns at the first
        // fault, so it takes the keyed run of the new array.
        for (Element& element : frame_run_(ProtocolFrameKeyDoor::key_(), array)) {
            if (!detail::decode_part_(element, cursor.first, cursor.offset, fault)) return false;
            cursor.advance(step);
        }
        part = static_cast<Bare&&>(array);
        return true;
    } else if constexpr (has_heap_image_at(^^Bare, 0)) {
        constexpr std::meta::info type = std::meta::dealias(^^Bare);
        return detail::decode_heap_parts_(part, allocation, cursor, fault, std::make_index_sequence<base_count(type)>{},
                                          std::make_index_sequence<member_count(type)>{});
    } else {
        constexpr std::size_t bytes = image_bytes_at(^^Bare);
        if (cursor.remaining < bytes) {
            fault = fault_at_(DecodeRefusal::image_end, cursor.offset);
            return false;
        }
        if (!detail::decode_part_(part, cursor.first, cursor.offset, fault)) return false;
        cursor.advance(bytes);
        return true;
    }
}

// The fold of an image of run-time size (the heap parts): the state of the
// hash, the position in its block, and the count of the bytes that it took.
struct HeapFold final {
    HashState state{};
    std::size_t position = 0;
    std::size_t total = 0;

    constexpr void put_byte(std::byte byte) noexcept {
        detail::put_byte_(state, position, byte);
        ++total;
    }

    // Puts the Width bytes of a leaf word at the position.  A leaf of 1, 2, 4
    // or 8 bytes at a position that is a multiple of its width lies inside
    // one word of the block, so it takes one shift and one or.  Each other
    // leaf takes one step for each byte.
    template <std::size_t Width>
    constexpr void put_leaf(std::uint64_t word) noexcept {
        if constexpr (Width == 1 || Width == 2 || Width == 4 || Width == 8) {
            if (position % Width == 0) {
                if (position < sizeof(std::uint64_t)) {
                    state.low |= word << (8 * position);
                } else {
                    state.high |= word << (8 * (position - sizeof(std::uint64_t)));
                }
                position += Width;
                total += Width;
                if (position == hash_block_bytes) {
                    state.fold_block();
                    position = 0;
                }
                return;
            }
        }
        for (std::size_t index = 0; index < Width; ++index) {
            put_byte(static_cast<std::byte>(static_cast<std::uint8_t>(word >> (8 * index))));
        }
    }
};

// True for a leaf whose word folds in place in a block: a leaf of 1, 2, 4 or
// 8 bytes, whose size divides the 16 bytes of a block.
template <class Element>
inline constexpr bool is_block_leaf_ = is_hash_leaf_<Element> && sizeof(Element) <= sizeof(std::uint64_t);

// The fold of a part with an image of fixed size: a leaf of at most 8 bytes
// as its word, and each other part as the bytes of its image.
template <class T>
constexpr void fold_fixed_part_(HeapFold& fold, T const& part) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (is_block_leaf_<Bare>) {
        fold.put_leaf<sizeof(Bare)>(static_cast<std::uint64_t>(detail::leaf_word_(part)));
    } else {
        constexpr std::size_t bytes = image_bytes_at(^^Bare);
        FixedArray<std::byte, bytes> image{};
        detail::encode_part_(part, first_of_(detail::BaseLayerRead{}, frame_run_(ProtocolFrameKeyDoor::key_(), image)));
        for (std::byte const byte : image.view()) {
            fold.put_byte(byte);
        }
    }
}

// The fold of count leaves from first, one after the other in the image.
// The leaves before the start of the next block go one at a time.  Then each
// group of leaves that fills a block folds the block with the offsets of its
// leaves, as the fold of an array does, and the remaining leaves go one at a
// time.  A position that is no multiple of the size of a leaf never starts a
// block, so each leaf then goes one at a time.  Complexity: one fold for
// each 16 bytes, and one step for each leaf outside a full block.
template <class Element>
constexpr void fold_leaf_run_(HeapFold& fold, Element const* first, std::size_t count) noexcept {
    constexpr std::size_t step = sizeof(Element);
    constexpr std::size_t per_block = hash_block_bytes / step;
    std::size_t index = 0;
    for (; index < count && fold.position != 0; ++index) {
        fold.put_leaf<step>(static_cast<std::uint64_t>(detail::leaf_word_(first[index])));
    }
    if (fold.position == 0) {
        for (; index + per_block <= count; index += per_block) {
            detail::fill_block_<step>(fold.state, first + index, std::make_index_sequence<per_block>{});
            fold.state.fold_block();
            fold.total += hash_block_bytes;
        }
    }
    for (; index < count; ++index) {
        fold.put_leaf<step>(static_cast<std::uint64_t>(detail::leaf_word_(first[index])));
    }
}

template <class T>
constexpr void fold_heap_part_(HeapFold& fold, T const& part) noexcept;

template <class T, std::size_t... Bases, std::size_t... Members>
constexpr void fold_heap_parts_([[maybe_unused]] HeapFold& fold, [[maybe_unused]] T const& object,
                                std::index_sequence<Bases...> /*bases*/,
                                std::index_sequence<Members...> /*members*/) noexcept {
    (detail::fold_heap_part_(fold, detail::base_at_<Bases>(object)), ...);
    (detail::fold_heap_part_(fold, detail::member_at_<Members>(object)), ...);
}

// Folds the image of one part, byte for byte as encode_heap_part_ writes it.
// The characters of a text and the leaves of an array fold as a run of
// leaves.  Complexity: one fold for each 16 bytes, and one step for each
// other element.
template <class T>
constexpr void fold_heap_part_(HeapFold& fold, T const& part) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (is_heap_text_at(^^Bare)) {
        fold.put_leaf<heap_count_bytes>(static_cast<std::uint64_t>(part.size()));
        TextView const text = part.view();
        detail::fold_leaf_run_(fold, detail::text_first_(text), text.size());
    } else if constexpr (is_heap_array_at(^^Bare)) {
        using Element = typename[:heap_element_at(^^Bare):];
        fold.put_leaf<heap_count_bytes>(static_cast<std::uint64_t>(part.size()));
        if constexpr (is_block_leaf_<Element>) {
            View<Element const> const run = part.view();
            detail::fold_leaf_run_(fold, first_of_(detail::BaseLayerRead{}, run), run.size());
        } else {
            for (auto const& element : part.view()) {
                detail::fold_fixed_part_(fold, element);
            }
        }
    } else if constexpr (has_heap_image_at(^^Bare, 0)) {
        constexpr std::meta::info type = std::meta::dealias(^^Bare);
        detail::fold_heap_parts_(fold, part, std::make_index_sequence<base_count(type)>{},
                                 image_member_sequence_<Bare>{});
    } else {
        detail::fold_fixed_part_(fold, part);
    }
}

// Ends the process: encode of an image of run-time size got a run of a
// different size, or the image holds more bytes than the cap.
[[noreturn]] CRUCIBLE_COLD inline void refuse_heap_encode_(std::size_t given, std::size_t needed) noexcept {
    fatal("encode got a run of {} bytes for an image of {} bytes, and an image of run-time size holds at most {} "
          "bytes",
          given, needed, max_image_bytes);
}

// ── The format ──────────────────────────────────────────────────────────

// The name of the member at Index, with static storage.
template <class T, std::size_t Index>
inline constexpr ::foundation::reflect::detail::NameChars member_name_ =
    ::foundation::reflect::detail::identifier_chars<std::meta::nonstatic_data_members_of(
        ^^T, std::meta::access_context::unchecked())[Index]>();

template <std::size_t Capacity>
[[nodiscard]] constexpr bool put_text_(FixedText<Capacity>& out, TextView text) noexcept {
    return out.append(text).is_ok();
}

template <std::size_t Capacity, class Value>
[[nodiscard]] bool put_number_(FixedText<Capacity>& out, Value value) noexcept {
    FixedText<32> digits{};
    return ::foundation::core::format(digits, "{}", value).is_ok() && detail::put_text_(out, digits.view());
}

template <std::size_t Capacity, class T>
[[nodiscard]] bool put_part_(FixedText<Capacity>& out, T const& part) noexcept;

template <std::size_t Capacity, class T, std::size_t Index>
[[nodiscard]] bool put_member_(FixedText<Capacity>& out, T const& object, bool& is_first) noexcept {
    if constexpr (member_carries_at(^^T, Index, ^^local_part) || member_carries_at(^^T, Index, ^^pad_part)) {
        return true;
    } else {
        constexpr ::foundation::reflect::detail::NameChars name = member_name_<T, Index>;
        bool const is_written = (is_first || detail::put_text_(out, ", "))
                             && detail::put_text_(out, mint_static_text(name.first, name.size))
                             && detail::put_text_(out, " = ")
                             && detail::put_part_(out, detail::member_at_<Index>(object));
        is_first = false;
        return is_written;
    }
}

template <std::size_t Capacity, class T, std::size_t... Bases, std::size_t... Members>
[[nodiscard]] bool put_parts_(FixedText<Capacity>& out, [[maybe_unused]] T const& object, bool& is_first,
                              std::index_sequence<Bases...> /*bases*/,
                              std::index_sequence<Members...> /*members*/) noexcept;

// The parts of a class with each base first, in one pair of braces.
template <std::size_t Capacity, class T>
[[nodiscard]] bool put_inline_parts_(FixedText<Capacity>& out, T const& object, bool& is_first) noexcept {
    constexpr std::meta::info type = std::meta::dealias(^^T);
    return detail::put_parts_(out, object, is_first, std::make_index_sequence<base_count(type)>{},
                              std::make_index_sequence<member_count(type)>{});
}

template <std::size_t Capacity, class T, std::size_t... Bases, std::size_t... Members>
[[nodiscard]] bool put_parts_(FixedText<Capacity>& out, [[maybe_unused]] T const& object, bool& is_first,
                              std::index_sequence<Bases...> /*bases*/,
                              std::index_sequence<Members...> /*members*/) noexcept {
    return (detail::put_inline_parts_(out, detail::base_at_<Bases>(object), is_first) && ...)
        && (detail::put_member_<Capacity, T, Members>(out, object, is_first) && ...);
}

// The text of a sum (the format): some(value) or none, ok(value) or
// err(error), and the index of the alternative of a OneOf before its value,
// as #1(value).
template <std::size_t Capacity, class T>
[[nodiscard]] bool put_sum_(FixedText<Capacity>& out, Option<T> const& part) noexcept {
    return part.match(
        [&out](T const& payload) noexcept {
            return detail::put_text_(out, "some(") && detail::put_part_(out, payload) && detail::put_text_(out, ")");
        },
        [&out]() noexcept { return detail::put_text_(out, "none"); });
}

template <std::size_t Capacity, class T, class E>
[[nodiscard]] bool put_sum_(FixedText<Capacity>& out, Result<T, E> const& part) noexcept {
    return part.match(
        [&out](T const& value) noexcept {
            return detail::put_text_(out, "ok(") && detail::put_part_(out, value) && detail::put_text_(out, ")");
        },
        [&out](E const& error) noexcept {
            return detail::put_text_(out, "err(") && detail::put_part_(out, error) && detail::put_text_(out, ")");
        });
}

template <std::size_t Capacity, class... Ts>
[[nodiscard]] bool put_sum_(FixedText<Capacity>& out, OneOf<Ts...> const& part) noexcept {
    auto const write = [&out](auto const& alternative) noexcept {
        return detail::put_part_(out, alternative) && detail::put_text_(out, ")");
    };
    return detail::put_text_(out, "#") && detail::put_number_(out, static_cast<std::uint64_t>(part.which().raw()))
        && detail::put_text_(out, "(") && part.match(((void)sizeof(Ts), write)...);
}

template <std::size_t Capacity, class T>
[[nodiscard]] bool put_sequence_(FixedText<Capacity>& out, T const& sequence) noexcept {
    if (!detail::put_text_(out, "[")) return false;
    bool is_first = true;
    for (auto const& element : sequence) {
        if (!is_first && !detail::put_text_(out, ", ")) return false;
        if (!detail::put_part_(out, element)) return false;
        is_first = false;
    }
    return detail::put_text_(out, "]");
}

// The characters of a text between two quotes, with a backslash before each
// quote and each backslash (the format).
template <std::size_t Capacity>
[[nodiscard]] bool put_quoted_(FixedText<Capacity>& out, TextView text) noexcept {
    if (!detail::put_text_(out, "\"")) return false;
    for (char const character : text) {
        bool const needs_escape = character == '"' || character == '\\';
        if (needs_escape && !detail::put_text_(out, "\\")) return false;
        if (out.append(character).is_err()) return false;
    }
    return detail::put_text_(out, "\"");
}

// The text of a bounded list (the format): the elements of a FixedList as
// [a, b], and the characters of a FixedText between two quotes.
template <std::size_t Capacity, class T, std::size_t N>
[[nodiscard]] bool put_list_(FixedText<Capacity>& out, FixedList<T, N> const& part) noexcept {
    return detail::put_sequence_(out, part.view());
}

template <std::size_t Capacity, std::size_t Size>
[[nodiscard]] bool put_list_(FixedText<Capacity>& out, FixedText<Size> const& part) noexcept {
    return detail::put_quoted_(out, part.view());
}

// Writes one part (the format).  Complexity: linear in the length of the
// text.
template <std::size_t Capacity, class T>
[[nodiscard]] bool put_part_(FixedText<Capacity>& out, T const& part) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (std::is_array_v<Bare>) {
        return detail::put_sequence_(out, part);
    } else if constexpr (is_fixed_array_at(^^Bare)) {
        return detail::put_sequence_(out, part.view());
    } else if constexpr (is_sum_at(^^Bare)) {
        return detail::put_sum_(out, part);
    } else if constexpr (is_bounded_list_at(^^Bare)) {
        return detail::put_list_(out, part);
    } else if constexpr (is_heap_array_at(^^Bare)) {
        return detail::put_sequence_(out, part.view());
    } else if constexpr (is_heap_text_at(^^Bare)) {
        return detail::put_quoted_(out, part.view());
    } else if constexpr (derives_protocol_at(^^Bare)) {
        bool is_first = true;
        return detail::put_text_(out, "{") && detail::put_inline_parts_(out, part, is_first)
            && detail::put_text_(out, "}");
    } else if constexpr (std::is_same_v<Bare, bool> || Float<Bare> || Integer<Bare>) {
        return detail::put_number_(out, part);
    } else if constexpr (std::is_enum_v<Bare>) {
        if constexpr (std::is_scoped_enum_v<Bare> && is_closed_enumeration_at(^^Bare)) {
            return detail::put_text_(out, ::foundation::core::enum_name_text(part));
        } else if constexpr (std::is_scoped_enum_v<Bare> && has_enumerators(std::meta::dealias(^^Bare))) {
            // A value of an open enumeration that names no enumerator is a
            // value too, so the text gives its number.
            if (detail::names_enumerator<Bare>(std::to_underlying(part))) {
                return detail::put_text_(out, ::foundation::core::enum_name_text(part));
            }
            return detail::put_part_(out, std::to_underlying(part));
        } else {
            return detail::put_part_(out, std::to_underlying(part));
        }
    } else if constexpr (is_target_signed_character_v<Bare>) {
        return detail::put_number_(out, static_cast<std::uint64_t>(detail::leaf_word_(part)));
    } else if constexpr (std::is_signed_v<Bare>) {
        return detail::put_number_(out, static_cast<std::int64_t>(part));
    } else {
        return detail::put_number_(out, static_cast<std::uint64_t>(part));
    }
}

}  // namespace detail

// A class with the annotation derive_protocol whose value has a byte image
// (the image) of at most max_image_bytes bytes.  hash, encode and format
// take it.
template <class T>
concept Encodable = detail::derives_protocol_at(^^T) && detail::has_image_at(^^T, 0)
                 && detail::image_bytes_at(^^T) <= detail::max_image_bytes;

// An Encodable class that decode builds: it has a default constructor, no
// member of its image is const, and no class of it carries the annotation
// no_start_over_bytes.
template <class T>
concept Decodable = Encodable<T> && std::is_nothrow_default_constructible_v<T> && detail::decodes_at(^^T);

// The count of bytes of the image of a T.
template <Encodable T>
inline constexpr std::size_t encoded_size_v = detail::image_bytes_at(^^T);

// A type that hash takes: an Encodable class, and a leaf of the image that
// is no class: an integer, a bool, a character, an enumeration, a float or a
// double.  The image of a leaf is its bytes, so its hash is one fold.  A
// pointer, a C array and a floating type other than float and double have
// no hash.
template <class T>
concept Hashable = Encodable<T> || (!std::is_class_v<T> && !std::is_array_v<T> && detail::has_image_at(^^T, 0));

// The hash of the value (the hash): 64 bits with no key, the same on each
// target.  Complexity: one fold for each 16 bytes of the image.
template <Hashable T>
[[nodiscard]] constexpr Digest<T> hash(T const& value) noexcept {
    detail::HashState state{};
    detail::hash_part_<0>(state, value);
    if constexpr (detail::image_bytes_at(^^T) % detail::hash_block_bytes != 0) state.fold_block();
    return Digest<T>{state.accumulator};
}

// Writes the image of the value into the run of a frame (the image).  The
// door takes the ViewMut of the frame by value, as fill does, so the encode
// is the one writer of the run.  Complexity: one pass over the parts.
template <Encodable T, class Brand>
constexpr void encode(T const& value, ViewMut<std::byte, Brand, encoded_size_v<T>> out) noexcept {
    detail::encode_part_(value, first_of_(detail::BaseLayerRead{}, out));
}

// The hash of a run of values (the hash of a run): the fold of the images of
// the elements one after the other as one image, and then of one block with
// the count of the elements.  The count keeps two runs apart when one is the
// other with zero values after it.  The order of the elements takes part.
// Complexity: one fold for each 16 bytes of the images, and one more.
template <Encodable T>
[[nodiscard]] constexpr Digest<View<T const>> hash(View<T const> run) noexcept {
    detail::HashState state{};
    detail::hash_run_(state, first_of_(detail::BaseLayerRead{}, run), run.size());
    return Digest<View<T const>>{state.accumulator};
}

// The T of an image, or the first fault of the image (the codec).
// Complexity: one pass over the parts.
template <Decodable T>
[[nodiscard]] constexpr Result<T, DecodeFault> decode(View<std::byte const, encoded_size_v<T>> bytes) noexcept {
    T value{};
    DecodeFault fault{DecodeRefusal::bool_byte, 0};
    if (!detail::decode_part_(value, first_of_(detail::BaseLayerRead{}, bytes), 0, fault)) {
        return err(fault);
    }
    return value;
}

// A class with the annotation derive_protocol whose image has a run-time
// size (the heap parts): one or more of its parts is a HeapArray or a
// HeapText, and each other part has an image.  No type is Encodable and
// HeapEncodable at the same time.
template <class T>
concept HeapEncodable = detail::derives_protocol_at(^^T) && detail::has_heap_image_at(^^T, 0);

// A HeapEncodable class that decode builds: it has a default constructor
// with no throw, an element of each HeapArray has one too and is not linear,
// and no class of it carries a const member or the annotation
// no_start_over_bytes.
template <class T>
concept HeapDecodable = HeapEncodable<T> && std::is_nothrow_default_constructible_v<T> && detail::heap_decodes_at(^^T);

// The count of bytes of the image of the value (the heap parts).
// Complexity: one step for each part, and none for each element.
template <HeapEncodable T>
[[nodiscard]] constexpr std::size_t encoded_size(T const& value) noexcept {
    return detail::heap_size_(value);
}

// Writes the image of the value into out, the run of a frame (the heap
// parts).  The door takes the ViewMut of the frame by value, as fill does,
// so the encode is the one writer of the run.  out holds exactly
// encoded_size(value) bytes, and the image holds at most max_image_bytes
// bytes, or the process ends.  Complexity: one store for each byte of the
// image.
template <HeapEncodable T, class Brand, std::size_t Extent>
constexpr void encode(T const& value, ViewMut<std::byte, Brand, Extent> out) noexcept {
    std::size_t const needed = detail::heap_size_(value);
    if (out.size() != needed || needed > detail::max_image_bytes) [[unlikely]] {
        detail::refuse_heap_encode_(out.size(), needed);
    }
    if (needed != 0) detail::encode_heap_part_(value, first_of_(detail::BaseLayerRead{}, out));
}

// The hash of the value (the heap parts): the fold of its image, and then
// one block with the count of the bytes of the image.  Complexity: one step
// for each leaf, and one fold for each 16 bytes of the image.
template <HeapEncodable T>
[[nodiscard]] constexpr Digest<T> hash(T const& value) noexcept {
    detail::HeapFold fold{};
    detail::fold_heap_part_(fold, value);
    if (fold.position != 0) fold.state.fold_block();
    fold.state.low = static_cast<std::uint64_t>(fold.total);
    fold.state.fold_block();
    return Digest<T>{fold.state.accumulator};
}

// The T of an image of run-time size, or the first fault of the bytes (the
// heap parts).  Each heap part allocates through the capability, and the
// storage of all the heap parts takes at most budget bytes.  Complexity: one
// pass over the bytes, and one allocation for each heap part.
template <HeapDecodable T, class Marker>
    requires AllocationCapability<Marker>
[[nodiscard]] Result<T, DecodeFault> decode(Marker allocation, Quantity<unit::Byte, std::size_t> budget,
                                            View<std::byte const> bytes) noexcept {
    if (bytes.size() > detail::max_image_bytes) {
        return err(detail::fault_at_(DecodeRefusal::image_size, detail::max_image_bytes));
    }
    T value{};
    detail::HeapCursor cursor{bytes.size() == 0 ? nullptr : first_of_(detail::BaseLayerRead{}, bytes), bytes.size(), 0,
                              detail::heap_budget_of_(budget)};
    DecodeFault fault{DecodeRefusal::bool_byte, 0};
    if (!detail::decode_heap_part_(value, allocation, cursor, fault)) return err(fault);
    if (cursor.remaining != 0) return err(detail::fault_at_(DecodeRefusal::trailing_byte, cursor.offset));
    return value;
}

// Writes each part of the value with its name into out, from its start
// (the format).  Gives the text, or Full<char> when the text needs more than
// the capacity of out.  On Full<char>, out holds a prefix of the text.
template <std::size_t Capacity, class T>
    requires(Encodable<T> || HeapEncodable<T>)
[[nodiscard]] Result<TextView, Full<char>> format(FixedText<Capacity>& out, T const& value) noexcept {
    detail::text_resize_(out, 0);
    if (!detail::put_part_(out, value)) return err(Full<char>{});
    return out.view();
}

template <std::size_t Capacity, class T>
    requires(Encodable<T> || HeapEncodable<T>)
void format(FixedText<Capacity>&& out, T const& value) =
    delete("format() gives a view of its FixedText, and a temporary FixedText ends with the full expression");

}  // namespace foundation::core
