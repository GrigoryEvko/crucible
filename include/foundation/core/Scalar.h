#pragma once

// The Scalar family: the copy of a value from the bits of another value,
// and the checked enumerator of an underlying value.
//
//   enum_from<E>(raw)    the enumerator of E whose value is raw, or no value
//   decode<To>(source)   a To with the bits of source, or no value when a
//                        bool of To would hold a byte other than 0 or 1, an
//                        enumeration of To would hold a value that names no
//                        enumerator, or a part of To with a niche would hold
//                        its empty value
//   bit_cast<To>(source) a To with the bits of source, with no check: each
//                        bit pattern is a value of a BitValid To
//
// A part that declares a niche (Choice.h) has an empty value that no valid
// value holds, such as the reserved value of an id.  decode gives no value
// for those bytes, so the bytes of a file or of the wire never make such a
// value, and Option::some never sees one.  The test reads only a bool, and
// no empty value and no key of the niche.
//
// std::bit_cast copies the bits into each type.  A bool from a byte other
// than 0 or 1 is undefined behavior, and the result then changes with the
// optimization level.  An enumeration with a fixed underlying type holds
// each value of that type, so a value that names no enumerator is not
// undefined behavior.  But a switch over the enumerators does not handle
// that value.  decode does a check of each such byte before it makes a To.
// So a To that decode gives holds only values that the program can name,
// and no bool with a byte other than 0 or 1 ever exists.
//
// An enumeration with no enumerator, such as std::byte, is a strong
// integer.  Each value of its underlying type is valid, and decode reads it
// as an integer.  enum_from refuses such an enumeration, because no
// enumerator can be its result.
//
// The enumerator set of each enumeration is a variable template, so a unit
// evaluates the set of an enumeration only when it decodes one.  When the
// enumerators are one range with no gap, the check is one compare.  When
// they fit in 64 consecutive values, it is one compare and one bit test.
// Each other set is a binary search in a sorted table.
//
// decode walks the two types by reflection: their bases, their non-static
// data members and the elements of their arrays.  The walk refuses each
// part whose bytes are not its whole value: a pointer, a member pointer, a
// reference, a union, a bit-field, an empty class and a class whose values
// only its own constructors make (detail::is_plain_at).  So decode makes no
// token, no key and no address.  A part of To with no bool, no enumeration
// and no niche gets no check, so a decode into such a type is the bit copy
// alone, and bit_cast is that copy for a To with no checked part.  The
// source has no padding bit: float and double are sources, and long double
// is not.

#include <foundation/ByteSeal.h>
#include <foundation/core/Choice.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail {

// The values of the enumerators of E, in ascending order, each value one
// time.  Two enumerators can share one value.
template <class E>
struct EnumeratorSet final {
    using Underlying = std::underlying_type_t<E>;
    static constexpr std::size_t declared = std::meta::enumerators_of(^^E).size();

    Underlying sorted[declared == 0 ? 1 : declared] = {};
    std::size_t count = 0;
};

// Collects the enumerators of E with an insertion sort.  Quadratic in the
// number of enumerators, at compile time only.
template <class E>
[[nodiscard]] consteval EnumeratorSet<E> make_enumerator_set() {
    using Underlying = std::underlying_type_t<E>;
    EnumeratorSet<E> set{};
    for (std::size_t index = 0; index < EnumeratorSet<E>::declared; ++index) {
        Underlying const value = static_cast<Underlying>(std::meta::extract<E>(std::meta::enumerators_of(^^E)[index]));
        std::size_t slot = 0;
        while (slot < set.count && set.sorted[slot] < value)
            ++slot;
        if (slot < set.count && set.sorted[slot] == value) continue;
        for (std::size_t later = set.count; later > slot; --later)
            set.sorted[later] = set.sorted[later - 1];
        set.sorted[slot] = value;
        ++set.count;
    }
    return set;
}

template <class E>
inline constexpr EnumeratorSet<E> enumerator_set = make_enumerator_set<E>();

// The bit of each enumerator in a mask of the 64 values from the lowest
// enumerator.  The caller makes sure that the highest enumerator is less
// than 64 values above the lowest.
template <class E>
[[nodiscard]] consteval std::uint64_t make_enumerator_mask() {
    using Unsigned = std::make_unsigned_t<std::underlying_type_t<E>>;
    EnumeratorSet<E> const& set = enumerator_set<E>;
    std::uint64_t mask = 0;
    for (std::size_t index = 0; index < set.count; ++index) {
        auto const offset = static_cast<unsigned>(
            static_cast<Unsigned>(static_cast<Unsigned>(set.sorted[index]) - static_cast<Unsigned>(set.sorted[0])));
        mask |= std::uint64_t{1} << offset;
    }
    return mask;
}

template <class E>
inline constexpr std::uint64_t enumerator_mask = make_enumerator_mask<E>();

// True when raw is the value of an enumerator of E.  Constant time for a
// set with no gap and for a set within 64 values.  Logarithmic in the
// number of enumerators for each other set.
template <class E>
[[nodiscard]] constexpr bool names_enumerator(std::underlying_type_t<E> raw) noexcept {
    using Underlying = std::underlying_type_t<E>;
    EnumeratorSet<E> const& set = enumerator_set<E>;
    if constexpr (EnumeratorSet<E>::declared == 0) {
        return false;
    } else if constexpr (std::same_as<Underlying, bool>) {
        return raw ? set.sorted[set.count - 1] : !set.sorted[0];
    } else {
        using Unsigned = std::make_unsigned_t<Underlying>;
        constexpr Unsigned lowest = static_cast<Unsigned>(enumerator_set<E>.sorted[0]);
        constexpr Unsigned span = static_cast<Unsigned>(
            static_cast<Unsigned>(enumerator_set<E>.sorted[enumerator_set<E>.count - 1]) - lowest);
        auto const offset = static_cast<Unsigned>(static_cast<Unsigned>(raw) - lowest);
        if constexpr (static_cast<std::size_t>(span) == enumerator_set<E>.count - 1) {
            return offset <= span;
        } else if constexpr (span < 64) {
            return offset <= span && ((enumerator_mask<E> >> offset) & std::uint64_t{1}) != 0;
        } else {
            std::size_t first = 0;
            std::size_t last = set.count;
            while (first < last) {
                std::size_t const middle = first + (last - first) / 2;
                if (set.sorted[middle] < raw) {
                    first = middle + 1;
                } else {
                    last = middle;
                }
            }
            return first < set.count && set.sorted[first] == raw;
        }
    }
}

// True for an enumeration with at least one enumerator.
[[nodiscard]] consteval bool has_enumerators(std::meta::info type) {
    return std::meta::is_enum_type(type) && !std::meta::enumerators_of(type).empty();
}

// The type and the byte offset of each base subobject and of each
// non-static data member of a class, by index.  The walks see each private
// part too, because a private bool or pointer is as much a part of the
// bytes as a public one.  No function gives its caller the reflection of a
// member, only its type, its offset and its kind.  The walks index the
// lists, so they make no static list.
[[nodiscard]] consteval std::size_t base_count(std::meta::info type) {
    return std::meta::bases_of(type, std::meta::access_context::unchecked()).size();
}

[[nodiscard]] consteval std::meta::info base_type_at(std::meta::info type, std::size_t index) {
    return std::meta::type_of(std::meta::bases_of(type, std::meta::access_context::unchecked())[index]);
}

// An offset is never negative.
[[nodiscard]] consteval std::size_t base_offset_at(std::meta::info type, std::size_t index) {
    return static_cast<std::size_t>(
        std::meta::offset_of(std::meta::bases_of(type, std::meta::access_context::unchecked())[index]).bytes);
}

[[nodiscard]] consteval std::size_t member_count(std::meta::info type) {
    return std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked()).size();
}

[[nodiscard]] consteval std::meta::info member_type_at(std::meta::info type, std::size_t index) {
    return std::meta::type_of(
        std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())[index]);
}

[[nodiscard]] consteval std::size_t member_offset_at(std::meta::info type, std::size_t index) {
    return static_cast<std::size_t>(
        std::meta::offset_of(std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())[index])
            .bytes);
}

[[nodiscard]] consteval bool member_is_bit_field_at(std::meta::info type, std::size_t index) {
    return std::meta::is_bit_field(
        std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())[index]);
}

// A class that nests deeper than this is refused, not walked.
inline constexpr int max_plain_depth = 64;

// True when the bytes of a value of the type are the whole value: each leaf
// is an integer, a floating value, a bool or an enumeration, in an array or
// a class at each depth.  The walk refuses a pointer, a member pointer and
// a reference, whose bits are an address.  It refuses a union, whose active
// member the bytes do not tell, and a named bit-field, which has no byte
// offset.  It refuses an empty class, whose value is its type alone, such
// as a token or a key.  It refuses a class that carries the annotation
// no_start_over_bytes (foundation/ByteSeal.h), whose values only its own
// constructors make, and a class that carries the mark closed_tag
// (Choice.h), whose bytes hold the tag of a sum.  It refuses a class whose
// state the walk cannot read:
// not empty, with no reflected base and no reflected member.  A lambda with
// captures has that shape, because GCC 16 reflects no capture
// (foundation/reflect/TypeComponents.h).  Linear in the number of subobject
// declarations that the walk reaches.
[[nodiscard]] consteval bool is_plain_at(std::meta::info spelled, int depth) {
    if (depth > max_plain_depth) return false;
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (std::meta::is_array_type(type)) return is_plain_at(std::meta::remove_all_extents(type), depth + 1);
    if (std::meta::is_arithmetic_type(type) || std::meta::is_enum_type(type)) return true;
    if (!std::meta::is_class_type(type) || !std::meta::is_complete_type(type) || std::meta::is_empty_type(type)) {
        return false;
    }
    if (!std::meta::annotations_of_with_type(type, ^^::foundation::lifetime::no_start_over_bytes).empty()
        || !std::meta::annotations_of_with_type(type, ^^::foundation::core::closed_tag).empty()) {
        return false;
    }
    std::size_t const bases = base_count(type);
    std::size_t const members = member_count(type);
    if (bases == 0 && members == 0) return false;
    for (std::size_t index = 0; index < bases; ++index) {
        if (!is_plain_at(base_type_at(type, index), depth + 1)) return false;
    }
    for (std::size_t index = 0; index < members; ++index) {
        if (member_is_bit_field_at(type, index) || !is_plain_at(member_type_at(type, index), depth + 1)) return false;
    }
    return true;
}

// True when the class or enumeration declares a niche: niche<T> is a
// specialization, which carries no annotation UndeclaredNiche.  The rule
// specialization-home admits a specialization of niche only in the file that
// defines the type, so each unit that names the type sees the same answer.
[[nodiscard]] consteval bool declares_niche_at(std::meta::info type) {
    if (!std::meta::is_class_type(type) && !std::meta::is_enum_type(type)) return false;
    return !::foundation::linear::carries_annotation(std::meta::substitute(^^niche, {type}), ^^UndeclaredNiche);
}

// True when each bit of a value of the type is a value bit.  float and
// double have no padding bit, although std::has_unique_object_representations
// is false for them: -0 and +0 compare equal.  long double has 80 value bits
// in 16 bytes on x86_64.  A class has no padding bit when its bases and its
// members have none and fill its whole size.  The caller makes sure that the
// type is plain, so no part is empty and none is a bit-field.  Linear in the
// number of subobject declarations that the walk reaches.
[[nodiscard]] consteval bool has_no_padding_bit_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (std::meta::is_array_type(type)) return has_no_padding_bit_at(std::meta::remove_all_extents(type));
    if (type == ^^float || type == ^^double) return true;
    if (!std::meta::is_class_type(type)) return std::meta::has_unique_object_representations(type);
    std::size_t covered = 0;
    for (std::size_t index = 0; index < base_count(type); ++index) {
        std::meta::info const part = base_type_at(type, index);
        if (!has_no_padding_bit_at(part)) return false;
        covered += std::meta::size_of(part);
    }
    for (std::size_t index = 0; index < member_count(type); ++index) {
        std::meta::info const part = member_type_at(type, index);
        if (!has_no_padding_bit_at(part)) return false;
        covered += std::meta::size_of(part);
    }
    return covered == std::meta::size_of(type);
}

// True when a value of the type can hold a byte that names no value: the
// type is or holds a bool, an enumeration with at least one enumerator, or a
// class or an enumeration that declares a niche.  A part with no such leaf
// gets no check.  The caller makes sure that the type is plain.
[[nodiscard]] consteval bool has_checked_leaf_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (std::meta::is_array_type(type)) return has_checked_leaf_at(std::meta::remove_all_extents(type));
    if (type == ^^bool) return true;
    if (declares_niche_at(type)) return true;
    if (std::meta::is_enum_type(type)) return has_enumerators(type);
    if (!std::meta::is_class_type(type)) return false;
    for (std::size_t index = 0; index < base_count(type); ++index) {
        if (has_checked_leaf_at(base_type_at(type, index))) return true;
    }
    for (std::size_t index = 0; index < member_count(type); ++index) {
        if (has_checked_leaf_at(member_type_at(type, index))) return true;
    }
    return false;
}

// The bytes of a value of Size bytes.
template <std::size_t Size>
struct ByteImage final {
    unsigned char bytes[Size];
};

// The part of type T at offset, made from its bytes.  The caller makes sure
// that each bool and each enumeration of T holds a value, so the bit copy
// makes no bool with a byte other than 0 or 1.
template <class T, std::size_t Size>
[[nodiscard]] constexpr T read_part(ByteImage<Size> const& image, std::size_t offset) noexcept {
    ByteImage<sizeof(T)> part{};
    for (std::size_t index = 0; index < sizeof(T); ++index)
        part.bytes[index] = image.bytes[offset + index];
    return __builtin_bit_cast(T, part);
}

template <class T, std::size_t Size>
[[nodiscard]] constexpr bool holds_value_of(ByteImage<Size> const& image, std::size_t offset) noexcept;

template <class T, std::size_t Size, std::size_t... Base, std::size_t... Member>
[[nodiscard]] constexpr bool parts_hold_values(ByteImage<Size> const& image, std::size_t offset,
                                               std::index_sequence<Base...>, std::index_sequence<Member...>) noexcept {
    return (holds_value_of<typename[:base_type_at(^^T, Base):]>(image, offset + base_offset_at(^^T, Base)) && ...)
        && (holds_value_of<typename[:member_type_at(^^T, Member):]>(image, offset + member_offset_at(^^T, Member))
            && ...);
}

// True when the bytes at offset hold a value of each bool and each
// enumeration of T: each bool byte is 0 or 1, and each enumeration value
// names an enumerator.  An enumeration with no enumerator takes each value.
// The niche of T itself is the test of holds_value_of.
template <class T, std::size_t Size>
[[nodiscard]] constexpr bool holds_leaf_values_of(ByteImage<Size> const& image, std::size_t offset) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (std::same_as<Bare, bool>) {
        return image.bytes[offset] <= 1;
    } else if constexpr (std::is_enum_v<Bare>) {
        if constexpr (!has_enumerators(^^Bare)) {
            return true;
        } else if constexpr (std::same_as<std::underlying_type_t<Bare>, bool>) {
            return image.bytes[offset] <= 1 && names_enumerator<Bare>(image.bytes[offset] == 1);
        } else {
            return names_enumerator<Bare>(read_part<std::underlying_type_t<Bare>>(image, offset));
        }
    } else if constexpr (std::is_array_v<Bare>) {
        using Element = std::remove_extent_t<Bare>;
        for (std::size_t index = 0; index < std::extent_v<Bare>; ++index) {
            if (!holds_value_of<Element>(image, offset + index * sizeof(Element))) return false;
        }
        return true;
    } else {
        return parts_hold_values<Bare>(image, offset, std::make_index_sequence<base_count(^^Bare)>{},
                                       std::make_index_sequence<member_count(^^Bare)>{});
    }
}

// True when the bytes at offset hold a value of T: each bool and each
// enumeration holds a value, and a part that declares a niche does not hold
// its empty value, which no valid value holds.  The parts come first, so the
// bit copy of a part with a niche makes no bool with a byte other than 0 or
// 1.  Linear in the number of checked leaves of T.
template <class T, std::size_t Size>
[[nodiscard]] constexpr bool holds_value_of(ByteImage<Size> const& image, std::size_t offset) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (!has_checked_leaf_at(^^Bare)) {
        return true;
    } else if constexpr (declares_niche_at(^^Bare)) {
        return holds_leaf_values_of<Bare>(image, offset)
            && !NicheRead<Bare>::holds_empty(read_part<Bare>(image, offset));
    } else {
        return holds_leaf_values_of<Bare>(image, offset);
    }
}

}  // namespace detail

// An enumeration that enum_from can decode: it has at least one enumerator.
template <class E>
concept Enumerated = std::is_enum_v<E> && detail::has_enumerators(^^E);

// A type To that decode can make from the bits of a From.  The two have one
// size, they are trivially copyable, and each is plain: the walk of
// detail::is_plain_at accepts it.  Each bit of From is a value bit, so no
// padding bit of the source becomes a value bit of To.  An array of bytes,
// a float and a double have this property.  To is not an array and not
// cv-qualified, because a function cannot return such a type.
template <class To, class From>
concept DecodableFrom =
    std::is_object_v<To> && !std::is_array_v<To> && std::same_as<To, std::remove_cv_t<To>>
    && std::is_trivially_copyable_v<To> && std::is_trivially_copyable_v<From> && sizeof(To) == sizeof(From)
    && detail::is_plain_at(^^To, 0) && detail::is_plain_at(^^From, 0) && detail::has_no_padding_bit_at(^^From);

// A type of which each bit pattern of its bytes is a value: plain, with no
// bool, no enumeration with an enumerator and no part that declares a niche.
// An integer, a float, an enumeration with no enumerator such as std::byte,
// and a class or an array of such parts are bit valid.
template <class T>
concept BitValid = std::is_object_v<T> && detail::is_plain_at(^^T, 0) && !detail::has_checked_leaf_at(^^T);

// The enumerator of E whose value is raw, or no value when no enumerator of
// E has the value raw.
template <Enumerated E>
[[nodiscard]] constexpr Option<E> enum_from(std::underlying_type_t<E> raw) noexcept {
    if (!detail::names_enumerator<E>(raw)) return none;
    return Option<E>::some(static_cast<E>(raw));
}

// A To with the bits of source, or no value when a bool of To would hold a
// byte other than 0 or 1, an enumeration of To would hold a value that names
// no enumerator, or a part of To that declares a niche would hold its empty
// value.  The check of each bool and each enumeration reads the bytes before
// a To exists.  Linear in the number of checked leaves of To.
template <class To, class From>
    requires DecodableFrom<To, From>
[[nodiscard]] constexpr Option<To> decode(From const& source) noexcept {
    auto const image = __builtin_bit_cast(detail::ByteImage<sizeof(To)>, source);
    if constexpr (detail::has_checked_leaf_at(^^To)) {
        if (!detail::holds_leaf_values_of<To>(image, 0)) return none;
    }
    // Each leaf holds a value, so the copy makes a To.  The test of the niche
    // of To reads the value that Option::some gets, so the compiler removes
    // the test in some().
    To const value = __builtin_bit_cast(To, image);
    if constexpr (detail::declares_niche_at(^^To)) {
        if (detail::NicheRead<To>::holds_empty(value)) return none;
    }
    return Option<To>::some(value);
}

// A To with the bits of source.  Each bit pattern is a value of a bit valid
// To, so the copy has no check and no failure.
template <class To, class From>
    requires BitValid<To> && DecodableFrom<To, From>
[[nodiscard]] constexpr To bit_cast(From const& source) noexcept {
    return __builtin_bit_cast(To, source);
}

}  // namespace foundation::core
