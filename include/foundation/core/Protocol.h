#pragma once

// The Protocol family, its upper part: the hash, the byte codec and the
// format of a value, derived from the parts of its type.
//
//   Encodable<T>        a class with the annotation derive_protocol whose
//                       value has a byte image
//   Decodable<T>        an Encodable class that decode can build
//   encoded_size_v<T>   the count of bytes of the image of a T
//   hash(value)         the Digest<T> of the value: 64 bits, with no key
//   encode(value, out)  writes the image of the value into out
//   decode<T>(bytes)    the T of an image, or the DecodeFault of the first
//                       part that holds no value
//   format(out, value)  writes each part of the value with its name into a
//                       FixedText, and gives the text or Truncated
//
// foundation/core/Equatable.h gives equal and order, the annotations
// derive_protocol, local_part and pad_part, and the rules of the walk: the
// parts of a class are its bases and then its members, in declaration order.
//
// THE IMAGE
//   A value has a byte image when each part has one: an integer, a bool, a
//   character, an enumeration, a float or a double, a bounded C array or a
//   FixedArray of such parts, and a class with the annotation
//   derive_protocol.  The walk refuses a pointer, a reference, a union, a
//   named bit-field and each other class, as decode of the Scalar family
//   does.  A local part has no place in the image.  The image holds no
//   padding byte: each part takes the bytes of its value and no more, in
//   declaration order.  A leaf takes its size in bytes, in little-endian
//   order: an integer its bits, a bool one byte of 0 or 1, an enumeration
//   its underlying integer, and a float or a double its IEEE 754 bits.  So
//   one value gives the same image on each target.
//
// THE HASH
//   hash gives 64 bits with no key.  It is an id inside one process, and a
//   hash of the store that the format version of the store pins, such as
//   the hash of a guard in the merkle hash of a branch.  The fold reads
//   each part of the image, a pad part too, and no local part:
//
//     a leaf     fmix64 of its value widened to 64 bits: a signed integer
//                with its sign, a float or a double as its bits, and a plain
//                char or a wchar_t as its bits with no sign
//     an array   h = 0, then h = h * 0x100000001b3 ^ hash(element) for each
//                element, then fmix64(h).  A C array and a FixedArray of one
//                element type give one hash
//     a class    h = 0x9E3779B97F4A7C15, then h = h * 0x9E3779B97F4A7C15 ^
//                hash(part) for each part, then fmix64(h)
//
//   fmix64 is the one mixer of foundation/reflect/Hash.h.  The fold reads
//   values and not bytes, so it is the same on each target.  A peer that
//   chooses the input can find two values of one hash, so a key that
//   crosses a trust boundary is a keyed hash of the bytes of encode, with
//   its width and its key in its type, and never this hash.
//
// THE CODEC
//   encode writes the image into a View of exactly encoded_size_v<T> bytes,
//   so it cannot fail.  decode reads such a View into a T.  It refuses a
//   bool byte other than 0 and 1, an enumeration value that names no
//   enumerator, a byte of a pad part that is not zero, and a part that
//   declares a niche and holds its empty value.  The DecodeFault names the
//   reason and the byte offset of the part in the image.  The first fault in
//   the order of the parts wins.  A local part of the result keeps the value
//   of the default constructor of T.  Decodable refuses a class with a const
//   member and a class that carries the annotation no_start_over_bytes,
//   whose values only its own constructors make.
//
// THE FORMAT
//   format writes `{name = value, name = value}`, with the parts of each base
//   first.  An integer is written in decimal, a bool as true or false, a
//   float as the text of report(), an enumerator of a scoped enumeration as
//   its name, and an array as `[a, b, c]`.  A local part and a pad part are
//   not written.  format is no part of a hot path.

#include <foundation/ByteSeal.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Equatable.h>
#include <foundation/core/Format.h>
#include <foundation/core/Id.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Text.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Hash.h>

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
    // An enumeration value that names no enumerator.
    enumerator,
    // A byte of a pad part that is not zero.
    pad_byte,
    // A part that declares a niche holds its empty value.
    empty_niche,
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

// True when a value of the type has a byte image (THE IMAGE).  Linear in the
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

// The count of bytes of the image of a type with an image.
[[nodiscard]] consteval std::size_t image_bytes_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (std::meta::is_array_type(type)) {
        std::meta::info const element = std::meta::remove_extent(type);
        return (std::meta::size_of(type) / std::meta::size_of(element)) * image_bytes_at(element);
    }
    if (is_fixed_array_at(type)) return fixed_array_count_at(type) * image_bytes_at(fixed_array_element_at(type));
    if (!derives_protocol_at(type)) return std::meta::size_of(type);
    std::size_t total = 0;
    std::size_t const bases = base_count(type);
    std::size_t const members = member_count(type);
    for (std::size_t index = 0; index < bases; ++index) {
        total += image_bytes_at(base_type_at(type, index));
    }
    for (std::size_t index = 0; index < members; ++index) {
        if (!member_carries_at(type, index, ^^local_part)) total += image_bytes_at(member_type_at(type, index));
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

// ── The hash ────────────────────────────────────────────────────────────

inline constexpr std::uint64_t hash_class_seed = 0x9E3779B97F4A7C15ULL;
inline constexpr std::uint64_t hash_array_prime = 0x100000001b3ULL;

template <class T>
[[nodiscard]] constexpr std::uint64_t hash_part_(T const& part) noexcept;

// The fold of a class over its bases and the members of its image, one
// statement for each part.
template <class T, std::size_t... Bases, std::size_t... Members>
[[nodiscard]] constexpr std::uint64_t hash_parts_([[maybe_unused]] T const& object,
                                                  std::index_sequence<Bases...> /*bases*/,
                                                  std::index_sequence<Members...> /*members*/) noexcept {
    std::uint64_t state = hash_class_seed;
    ((state = state * hash_class_seed ^ detail::hash_part_(detail::base_at_<Bases>(object))), ...);
    ((state = state * hash_class_seed ^ detail::hash_part_(detail::member_at_<Members>(object))), ...);
    return ::foundation::reflect::fmix64(state);
}

// The hash of one part (THE HASH).  Complexity: one fmix64 for each leaf,
// array and class.
template <class T>
[[nodiscard]] constexpr std::uint64_t hash_part_(T const& part) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (std::is_array_v<Bare>) {
        std::uint64_t state = 0;
        for (std::size_t index = 0; index < std::extent_v<Bare>; ++index) {
            state = state * hash_array_prime ^ detail::hash_part_(part[index]);
        }
        return ::foundation::reflect::fmix64(state);
    } else if constexpr (is_fixed_array_at(^^Bare)) {
        std::uint64_t state = 0;
        for (auto const& element : part.view()) {
            state = state * hash_array_prime ^ detail::hash_part_(element);
        }
        return ::foundation::reflect::fmix64(state);
    } else if constexpr (derives_protocol_at(^^Bare)) {
        constexpr std::meta::info type = std::meta::dealias(^^Bare);
        return detail::hash_parts_(part, std::make_index_sequence<base_count(type)>{}, image_member_sequence_<Bare>{});
    } else if constexpr (Float<Bare> || std::is_same_v<Bare, bool> || is_target_signed_character_v<Bare>) {
        return ::foundation::reflect::fmix64(static_cast<std::uint64_t>(detail::leaf_word_(part)));
    } else if constexpr (std::is_enum_v<Bare>) {
        return ::foundation::reflect::fmix64(static_cast<std::uint64_t>(std::to_underlying(part)));
    } else {
        return ::foundation::reflect::fmix64(static_cast<std::uint64_t>(part));
    }
}

// ── The codec ───────────────────────────────────────────────────────────

template <class T>
constexpr void encode_part_(T const& part, std::byte* first) noexcept;

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
            if (!detail::decode_part_(part.at(index), first + at, offset + at, fault)) return false;
        }
        return true;
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
        if constexpr (has_enumerators(std::meta::dealias(^^Bare))) {
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
                             && detail::put_text_(out, detail::text_over_(name.first, name.size))
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

// Writes one part (THE FORMAT).  Complexity: linear in the length of the
// text.
template <std::size_t Capacity, class T>
[[nodiscard]] bool put_part_(FixedText<Capacity>& out, T const& part) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (std::is_array_v<Bare>) {
        return detail::put_sequence_(out, part);
    } else if constexpr (is_fixed_array_at(^^Bare)) {
        return detail::put_sequence_(out, part.view());
    } else if constexpr (derives_protocol_at(^^Bare)) {
        bool is_first = true;
        return detail::put_text_(out, "{") && detail::put_inline_parts_(out, part, is_first)
            && detail::put_text_(out, "}");
    } else if constexpr (std::is_same_v<Bare, bool> || Float<Bare> || Integer<Bare>) {
        return detail::put_number_(out, part);
    } else if constexpr (std::is_enum_v<Bare>) {
        if constexpr (std::is_scoped_enum_v<Bare> && has_enumerators(std::meta::dealias(^^Bare))) {
            return detail::put_text_(out, ::foundation::core::enum_name_text(part));
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
// (THE IMAGE).  hash, encode and format take it.
template <class T>
concept Encodable = detail::derives_protocol_at(^^T) && detail::has_image_at(^^T, 0);

// An Encodable class that decode builds: it has a default constructor, no
// member of its image is const, and no class of it carries the annotation
// no_start_over_bytes.
template <class T>
concept Decodable = Encodable<T> && std::is_nothrow_default_constructible_v<T> && detail::decodes_at(^^T);

// The count of bytes of the image of a T.
template <Encodable T>
inline constexpr std::size_t encoded_size_v = detail::image_bytes_at(^^T);

// The hash of the value (THE HASH): 64 bits with no key, the same on each
// target.  Complexity: one pass over the parts.
template <Encodable T>
[[nodiscard]] constexpr Digest<T> hash(T const& value) noexcept {
    return Digest<T>{detail::hash_part_(value)};
}

// Writes the image of the value into out (THE IMAGE).  Complexity: one pass
// over the parts.
template <Encodable T>
constexpr void encode(T const& value, View<std::byte, encoded_size_v<T>> out) noexcept {
    detail::encode_part_(value, first_of_(detail::ViewDoor::key(), out));
}

// The T of an image, or the first fault of the image (THE CODEC).
// Complexity: one pass over the parts.
template <Decodable T>
[[nodiscard]] constexpr Result<T, DecodeFault> decode(View<std::byte const, encoded_size_v<T>> bytes) noexcept {
    T value{};
    DecodeFault fault{DecodeRefusal::bool_byte, 0};
    if (!detail::decode_part_(value, first_of_(detail::ViewDoor::key(), bytes), 0, fault)) {
        return err(fault);
    }
    return value;
}

// Writes each part of the value with its name into out, from its start
// (THE FORMAT).  Gives the text, or Truncated when the text needs more than
// the capacity of out.  On Truncated, out holds a prefix of the text.
template <std::size_t Capacity, Encodable T>
[[nodiscard]] Result<TextView, Truncated> format(FixedText<Capacity>& out, T const& value) noexcept {
    detail::text_resize_(out, 0);
    if (!detail::put_part_(out, value)) return err(Truncated{});
    return out.view();
}

template <std::size_t Capacity, Encodable T>
void format(FixedText<Capacity>&& out, T const& value) =
    delete("format() gives a view of its FixedText, and a temporary FixedText ends with the full expression");

}  // namespace foundation::core
