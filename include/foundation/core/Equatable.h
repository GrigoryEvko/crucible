#pragma once

// The Protocol family, its lower part: the equality and the order of a
// value, derived from the parts of its type.
//
//   Equatable<T>      a type that equal compares
//   PartialEquatable<T>
//                     an Equatable type, or a class whose own == compares a
//                     float by IEEE 754 and states it with ieee_equality
//   Orderable<T>      a type that order orders
//   equal(a, b)       true when a and b hold the same value
//   order(a, b)       the std::strong_ordering of a and b: lexicographic over
//                     the parts, in declaration order
//   derive_protocol   the annotation of a class that takes the operations of
//                     the family from its parts
//   equal_parts       the annotation of a class of the base whose own ==
//                     compares each of its parts with equal, such as Option
//                     and FixedArray
//   ieee_equality     the annotation of a class whose own == compares a float
//                     with the == of IEEE 754
//   local_part        the annotation of a member whose value is local to one
//                     process, such as an address or a count of an allocator
//   pad_part          the annotation of a member of explicit padding
//
// foundation/core/Protocol.h gives the other operations of the family: hash,
// encode, decode and format.  This header is below the sums and the records,
// so Option, Result, OneOf and Record compare their parts with equal.
//
// Parts
//   A class that carries the annotation derive_protocol takes the operations
//   from its parts: each direct base in declaration order, and then each
//   non-static data member in declaration order.  The walk reads the private
//   parts too, because the class consents to the walk with the annotation.
//   The walk reads each part and no padding byte.  It refuses a named
//   bit-field, which has no byte offset.  A C array part compares element by
//   element in a loop, so no defaulted == of a class with a C array runs in a
//   constant evaluation (GCC 16 gives a wrong result there).
//
//   Each other class gives its own operator: its == gives bool and does not
//   throw, and its <=> gives std::strong_ordering and does not throw.  Option,
//   Result, OneOf, Record, FixedArray and an id are such classes.  A <=> that
//   gives a weak or a partial order does not agree with ==, so it is no order
//   of this family.
//
// Leaves
//   An integer, a bool, a character and an enumeration compare with == and
//   order with <=>.  A plain char and a wchar_t have a sign on x86_64 and no
//   sign on aarch64, so they order, hash and format by their bits as a value
//   with no sign, the same on each target.  A pointer compares as an address,
//   which is the identity of an object in one process.  A pointer has no
//   order, because the order of two addresses changes from run to run.  A
//   reference is no part.
//
// Float rule
//   equal of a float or a double is the identity of its bits: +0 and -0
//   differ, and two NaNs are equal only when they have the same bits.  The
//   base gives a canonical NaN at the output of its arithmetic, so a NaN of
//   the base equals itself.  order of a float or a double is the totalOrder
//   of IEEE 754: the negative NaNs, -inf, the negative values, -0, +0, the
//   positive values, +inf and the positive NaNs.  The order agrees with
//   equal: order(a, b) is equal exactly when equal(a, b) is true.  hash and
//   encode read the same bits.  A numeric compare of two floats, in which -0
//   equals +0 and a NaN equals nothing, goes through the fp functions of the
//   base, never through this family.  A floating type other than float and
//   double is refused: long double holds padding bits on x86_64.
//
//   A class with its own == that holds a float or a double at a depth of its
//   parts, a member of a union part too, compares that float by the == of
//   IEEE 754, as a defaulted == does: +0 equals -0, and a NaN equals nothing.
//   A union with its own == is such a class too.  Such a class is no Equatable
//   type, with or without derive_protocol, so equal, find and the sums of the
//   base refuse it.  A class with derive_protocol and no own == compares its
//   parts by the float rule.  The mark ieee_equality keeps an own == and
//   makes the class a PartialEquatable type only.  The walk of the parts
//   stops at a class of the base with the mark equal_parts, whose own ==
//   compares each part with equal.  The walk trusts the mark only in the
//   base, as the walks of foundation/core/TypeParts.h trust refers_deep{}.
//   For each such class, a check file or a test pins that its == tells +0
//   from -0 and that the canonical NaN equals itself.
//   The walk reads the parts of a class outside the base with the mark, as
//   the parts of a class with no mark.
//
// Marks of a member
//   equal and order read each part, a local part and a pad part too.  hash,
//   encode and format skip a local part, because its value has no meaning
//   outside the process.  encode writes a pad part, and decode refuses a pad
//   byte that is not zero.
//
// The gates are concepts over consteval functions that are not templates,
// so no unit can specialize a template and change what a gate admits.

#include <foundation/Platform.h>
#include <foundation/core/Number.h>
#include <foundation/reflect/Members.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation::core {

// The annotation of a class whose operations of the Protocol family come from
// its parts: [[= ::foundation::core::derive_protocol{}]].
struct derive_protocol final {};

// The annotation of a member whose value is local to one process.
struct local_part final {};

// The annotation of a member of explicit padding.  Each byte of it is zero.
struct pad_part final {};

// The annotation of a class whose own == compares each of its parts with
// equal, so a float part compares by the float rule: Option, Result, OneOf,
// Record, FixedArray, HeapArray, FixedList and the wrappers of fixy that
// compare their value with equal.  Its == asks for Equatable parts.  The
// walk of the float rule trusts the mark only on a class of the base
// (is_class_of_the_base_ of foundation/reflect/Members.h).
struct equal_parts final {};

// The annotation of a class whose own == compares a float or a double with
// the == of IEEE 754, on purpose.  The class is PartialEquatable, and no
// Equatable type.
struct ieee_equality final {};

namespace detail {

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

// True when the member at index carries an annotation of the type mark.
[[nodiscard]] consteval bool member_carries_at(std::meta::info type, std::size_t index, std::meta::info mark) {
    return !std::meta::annotations_of_with_type(
                std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())[index], mark)
                .empty();
}

// True for a complete class that carries the annotation derive_protocol.  A
// union is no such class.  An alias reads as the type that it names, because
// an alias carries no annotation of its own.
[[nodiscard]] consteval bool derives_protocol_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    return std::meta::is_class_type(type) && std::meta::is_complete_type(type)
        && !std::meta::annotations_of_with_type(type, ^^derive_protocol).empty();
}

// A type that nests deeper than this is refused, not walked.
inline constexpr int max_protocol_depth = 64;

// A class whose own == gives bool and does not throw.
template <class T>
concept HasOwnEquality = requires(T const& left, T const& right) {
    { left == right } noexcept -> std::same_as<bool>;
};

// A class whose own <=> gives a strong order and does not throw.
template <class T>
concept HasOwnStrongOrder = requires(T const& left, T const& right) {
    { left <=> right } noexcept -> std::same_as<std::strong_ordering>;
};

// The two questions of a walk: equal or order.
enum class CompareKind : std::uint8_t {
    equality,
    order,
};

// True when the class carries an annotation of the type mark.
[[nodiscard]] consteval bool carries_mark_at(std::meta::info type, std::meta::info mark) {
    return !std::meta::annotations_of_with_type(type, mark).empty();
}

// True when an own == of a class that holds the type compares a float by
// the == of IEEE 754: the type is a floating type (float, double, long
// double or an extended type such as std::float16_t), or it holds one by
// value at a depth of its parts.  The walk stops at a class of the base with
// the mark equal_parts, whose == compares its parts by the float rule, and it
// reads a class with the mark ieee_equality as such a compare.  A class
// outside the base can state equal_parts with an == that compares a float by
// IEEE 754, so the walk reads its parts as if it had no mark.  The walk reads
// each member of a union too, because an own == can read a float member of a
// union part.  Option, Result and OneOf keep their unions inside a class of
// the base with the mark, so the walk stops before them.  A class cannot hold
// itself by value, so the walk ends.  The walk reads the list of parts of
// each class one time, so it is linear in the number of parts that it
// reaches.
[[nodiscard]] consteval bool compares_float_by_ieee_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (std::meta::is_floating_point_type(type)) return true;
    if (std::meta::is_array_type(type)) return compares_float_by_ieee_at(std::meta::remove_all_extents(type));
    bool const is_union = std::meta::is_union_type(type);
    if ((!std::meta::is_class_type(type) && !is_union) || !std::meta::is_complete_type(type)) return false;
    if (carries_mark_at(type, ^^equal_parts) && ::foundation::reflect::is_class_of_the_base_(type)) return false;
    if (carries_mark_at(type, ^^ieee_equality)) return true;
    if (!is_union) {
        auto const bases = std::meta::bases_of(type, std::meta::access_context::unchecked());
        for (std::size_t index = 0; index < bases.size(); ++index) {
            if (compares_float_by_ieee_at(std::meta::type_of(bases[index]))) return true;
        }
    }
    auto const members = std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked());
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (!std::meta::is_bit_field(members[index]) && compares_float_by_ieee_at(std::meta::type_of(members[index]))) {
            return true;
        }
    }
    return false;
}

// True for a class or a union with the mark ieee_equality whose own == gives
// bool and does not throw: a PartialEquatable type that is no Equatable type.
[[nodiscard]] consteval bool states_ieee_equality_at(std::meta::info spelled) {
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    return (std::meta::is_class_type(type) || std::meta::is_union_type(type)) && std::meta::is_complete_type(type)
        && carries_mark_at(type, ^^ieee_equality)
        && std::meta::extract<bool>(std::meta::substitute(^^HasOwnEquality, {type}));
}

// True when the type is a leaf, an array or a class that the compare of the
// kind takes.  The concept of an own operator is read only for a class with
// no annotation.  A class or a union whose own == compares a float by IEEE
// 754 is no type of the equality (the float rule).  Linear in the number of
// parts that the walk reaches.
[[nodiscard]] consteval bool compares_at(std::meta::info spelled, CompareKind kind, int depth) {
    if (depth > max_protocol_depth) return false;
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (!std::meta::is_object_type(type)) return false;
    if (type == ^^float || type == ^^double) return true;
    if (std::meta::is_floating_point_type(type)) return false;
    if (std::meta::is_integral_type(type) || std::meta::is_enum_type(type)) return true;
    if (std::meta::is_pointer_type(type) || std::meta::is_member_pointer_type(type)
        || std::meta::is_null_pointer_type(type)) {
        return kind == CompareKind::equality;
    }
    if (std::meta::is_array_type(type)) {
        return std::meta::is_bounded_array_type(type)
            && compares_at(std::meta::remove_all_extents(type), kind, depth + 1);
    }
    if (kind == CompareKind::equality && (std::meta::is_class_type(type) || std::meta::is_union_type(type))
        && std::meta::extract<bool>(std::meta::substitute(^^HasOwnEquality, {type}))
        && compares_float_by_ieee_at(type)) {
        return false;
    }
    if (derives_protocol_at(type)) {
        std::size_t const bases = base_count(type);
        std::size_t const members = member_count(type);
        for (std::size_t index = 0; index < bases; ++index) {
            if (!compares_at(base_type_at(type, index), kind, depth + 1)) return false;
        }
        for (std::size_t index = 0; index < members; ++index) {
            if (member_is_bit_field_at(type, index) || !compares_at(member_type_at(type, index), kind, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (!std::meta::is_class_type(type) && !std::meta::is_union_type(type)) return false;
    std::meta::info const own = kind == CompareKind::equality ? ^^HasOwnEquality : ^^HasOwnStrongOrder;
    return std::meta::extract<bool>(std::meta::substitute(own, {type}));
}

// True for plain char and wchar_t, whose sign changes with the target.
template <class T>
inline constexpr bool is_target_signed_character_v = std::is_same_v<T, char> || std::is_same_v<T, wchar_t>;

// The word of the bits of a float or a double.
template <Float F>
using FloatWord = typename FloatBits<F>::Word;

// The key of the totalOrder of IEEE 754: the bits as a signed integer, with
// each bit but the sign flipped in a negative value.  The keys of two values
// then order as the totalOrder orders the values.
template <Float F>
[[nodiscard]] constexpr auto total_order_key_(F value) noexcept {
    using Signed = std::make_signed_t<FloatWord<F>>;
    using Unsigned = FloatWord<F>;
    Signed const bits = __builtin_bit_cast(Signed, value);
    constexpr int sign_shift = static_cast<int>(sizeof(F)) * __CHAR_BIT__ - 1;
    return static_cast<Signed>(bits ^ static_cast<Signed>(static_cast<Unsigned>(bits >> sign_shift) >> 1));
}

// The direct base at Index and the member at Index of an object, the private
// ones too.
template <std::size_t Index, class T>
[[nodiscard]] constexpr auto const& base_at_(T const& object CRUCIBLE_LIFETIMEBOUND) noexcept {
    return object.[:std::meta::bases_of(^^T, std::meta::access_context::unchecked())[Index]:];
}
template <std::size_t Index, class T>
void base_at_(T const&&) = delete("the reference names a part of the object, and a temporary ends with the full "
                                  "expression");

template <std::size_t Index, class T>
[[nodiscard]] constexpr auto const& member_at_(T const& object CRUCIBLE_LIFETIMEBOUND) noexcept {
    return object.[:std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())[Index]:];
}
template <std::size_t Index, class T>
void member_at_(T const&&) = delete("the reference names a part of the object, and a temporary ends with the full "
                                    "expression");

}  // namespace detail

// A type that equal compares: an integer, a bool, a character, an
// enumeration, a float or a double, a pointer, a bounded C array of such
// elements, a class with the annotation derive_protocol whose parts are
// Equatable, and a class whose own == gives bool and does not throw.  A class
// whose own == compares a float by IEEE 754 is none of them (the float rule).
template <class T>
concept Equatable = detail::compares_at(^^T, detail::CompareKind::equality, 0);

// A type with an == that a caller can read: an Equatable type, or a class
// with the mark ieee_equality, whose own == compares a float by IEEE 754.  A
// numeric compare of such a value goes through its own ==, and no family of
// the base takes it as an identity.
template <class T>
concept PartialEquatable = Equatable<T> || detail::states_ieee_equality_at(^^T);

// A type that order orders: an integer, a bool, a character, an
// enumeration, a float or a double, a bounded C array of such elements, a
// class with the annotation derive_protocol whose parts are Orderable, and a
// class whose own <=> gives std::strong_ordering and does not throw.  A
// pointer is not Orderable.
template <class T>
concept Orderable = detail::compares_at(^^T, detail::CompareKind::order, 0);

template <class T>
    requires Equatable<T>
[[nodiscard]] constexpr bool equal(T const& left, T const& right) noexcept;

template <class T>
    requires Orderable<T>
[[nodiscard]] constexpr std::strong_ordering order(T const& left, T const& right) noexcept;

namespace detail {

// The parts of two objects of a class with the annotation, in order.  The
// walk stops at the first part that differs.  A class with no part reads no
// parameter.
template <class T, std::size_t... Bases, std::size_t... Members>
[[nodiscard]] constexpr bool parts_equal_([[maybe_unused]] T const& left, [[maybe_unused]] T const& right,
                                          std::index_sequence<Bases...> /*bases*/,
                                          std::index_sequence<Members...> /*members*/) noexcept {
    return (::foundation::core::equal(detail::base_at_<Bases>(left), detail::base_at_<Bases>(right)) && ...)
        && (::foundation::core::equal(detail::member_at_<Members>(left), detail::member_at_<Members>(right)) && ...);
}

// The part at Part of an object: a base below the count of bases, and a
// member after them.
template <std::size_t Part, class T>
[[nodiscard]] constexpr auto const& part_at_(T const& object CRUCIBLE_LIFETIMEBOUND) noexcept {
    if constexpr (Part < base_count(^^T)) {
        return detail::base_at_<Part>(object);
    } else {
        return detail::member_at_<Part - base_count(^^T)>(object);
    }
}
template <std::size_t Part, class T>
void part_at_(T const&&) = delete("the reference names a part of the object, and a temporary ends with the full "
                                  "expression");

// The order of the parts from Part to Count: the order of the first part
// that differs, and the order of the last part when each other part is
// equal, as a defaulted <=> writes it.
template <std::size_t Part, std::size_t Count, class T>
[[nodiscard]] constexpr std::strong_ordering parts_order_([[maybe_unused]] T const& left,
                                                          [[maybe_unused]] T const& right) noexcept {
    if constexpr (Part == Count) {
        return std::strong_ordering::equal;
    } else if constexpr (Part + 1 == Count) {
        return ::foundation::core::order(detail::part_at_<Part>(left), detail::part_at_<Part>(right));
    } else {
        std::strong_ordering const step =
            ::foundation::core::order(detail::part_at_<Part>(left), detail::part_at_<Part>(right));
        if (std::is_neq(step)) return step;
        return detail::parts_order_<Part + 1, Count>(left, right);
    }
}

}  // namespace detail

// True when left and right hold the same value: each part, in declaration
// order, equals the part of the other.  A float compares by its bits (the
// float rule).  Complexity: one compare for each leaf.
template <class T>
    requires Equatable<T>
[[nodiscard]] constexpr bool equal(T const& left, T const& right) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (Float<Bare>) {
        return __builtin_bit_cast(detail::FloatWord<Bare>, left) == __builtin_bit_cast(detail::FloatWord<Bare>, right);
    } else if constexpr (std::is_array_v<Bare>) {
        for (std::size_t index = 0; index < std::extent_v<Bare>; ++index) {
            if (!::foundation::core::equal(left[index], right[index])) return false;
        }
        return true;
    } else if constexpr (detail::derives_protocol_at(^^Bare)) {
        constexpr std::meta::info type = std::meta::dealias(^^Bare);
        return detail::parts_equal_(left, right, std::make_index_sequence<detail::base_count(type)>{},
                                    std::make_index_sequence<detail::member_count(type)>{});
    } else {
        return left == right;
    }
}

// The order of left and right: the order of the first part that differs, in
// declaration order, or equal.  A float orders by the totalOrder of IEEE 754
// (the float rule).  Complexity: one compare for each leaf up to the first
// part that differs.
template <class T>
    requires Orderable<T>
[[nodiscard]] constexpr std::strong_ordering order(T const& left, T const& right) noexcept {
    using Bare = std::remove_cv_t<T>;
    if constexpr (Float<Bare>) {
        return detail::total_order_key_(left) <=> detail::total_order_key_(right);
    } else if constexpr (std::is_array_v<Bare>) {
        for (std::size_t index = 0; index < std::extent_v<Bare>; ++index) {
            std::strong_ordering const step = ::foundation::core::order(left[index], right[index]);
            if (std::is_neq(step)) return step;
        }
        return std::strong_ordering::equal;
    } else if constexpr (detail::derives_protocol_at(^^Bare)) {
        constexpr std::meta::info type = std::meta::dealias(^^Bare);
        return detail::parts_order_<0, detail::base_count(type) + detail::member_count(type)>(left, right);
    } else if constexpr (detail::is_target_signed_character_v<Bare>) {
        using Unsigned = std::make_unsigned_t<Bare>;
        return __builtin_bit_cast(Unsigned, left) <=> __builtin_bit_cast(Unsigned, right);
    } else {
        return left <=> right;
    }
}

}  // namespace foundation::core
