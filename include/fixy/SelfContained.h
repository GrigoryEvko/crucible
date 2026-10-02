#pragma once

// A value that owns everything a reader can reach through it, and that a
// const reference to it cannot change.
//
// A grade that describes a value, such as a version or a use budget,
// describes what the value holds when the grade is given.  A value that
// reaches state outside itself breaks that promise without a cast: the
// referent behind a pointer, a span or a string_view changes after the
// claim, and the value still carries the old grade.  The same holds for
// a const handle that writes through, such as a unique_ptr, and for a
// mutable member, because a wrapper hands out its payload only as a const
// reference, and those two change the payload through one.  The rule is
// the owners-as-dominators property of ownership types (Clarke, Potter
// and Noble, OOPSLA 1998): every path to the representation of the value
// goes through the value, so a claim about the value covers all of it.
//
// The rule is derived from the type, and no list of types names it:
//
//   - the value is an object, so void, a function type and a reference
//     are refused;
//   - a scalar, an enumeration, a member pointer and nullptr_t hold only
//     their own bits;
//   - an array holds its elements;
//   - a container is a range that the standard library declares, that is
//     not a view, and that is not trivially copyable.  It owns its
//     elements unless the standard marks it borrowed, and it must keep
//     them const through a const reference.  The rule reads its
//     elements and its type arguments, which name its comparator, its
//     hasher and its allocator.  It does not read its members, which
//     point at the storage it owns;
//   - any other class holds its bases and its data members, and a
//     mutable data member is refused.  A view is such a class: a ref_view
//     holds a pointer and is refused, an owning_view holds its container
//     and is admitted;
//   - a pointer and a reference reach state outside the value;
//   - a class whose storage the walk cannot account for is refused.  A
//     lambda that captures an address is that case, because GCC 16
//     reflects no capture as a member.  A capture of one byte cannot hold
//     an address, so it is admitted.
//
// The container rule is a closed world.  The standard specifies what each
// of its containers owns, and a program's specialization of a standard
// template must meet the requirements of that template ([namespace.std]).
// Nothing specifies what a program-defined range owns, so it is walked by
// its members, and one that holds its storage through a pointer is
// refused whatever it owns.  A trivially copyable range cannot own storage
// outside itself, because a copy would share that storage, so std::array
// and std::initializer_list are also walked by their members.
//
// The rule reads types.  An integer that names outside state, such as an
// index or a descriptor, is a plain value to it.  Complexity: linear in
// the number of distinct types reached, because a type reached twice is
// read once.

#include <foundation/reflect/Anchor.h>
#include <foundation/reflect/Instance.h>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fixy {

namespace detail::self_contained {

// True when a const reference to a range of R still writes its elements.
template <typename R>
inline constexpr bool writes_through_const =
    std::is_lvalue_reference_v<std::ranges::range_reference_t<R const>>
    && !std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<R const>>>;

// True when the standard library declares the class, or the template of
// which it is a specialization.  A namespace named std inside another
// namespace is not the standard's.
[[nodiscard]] consteval bool declared_by_the_standard(std::meta::info type) {
    std::meta::info scope = std::meta::has_template_arguments(type) ? std::meta::template_of(type) : type;
    while (std::meta::has_parent(scope)) {
        scope = std::meta::parent_of(scope);
        if (scope == ^^std) return true;
    }
    return false;
}

// True when U is a container in the sense of the header comment.
template <typename U>
inline constexpr bool is_standard_container =
    std::ranges::range<U const> && !std::ranges::view<U> && !std::is_trivially_copyable_v<U>
    && declared_by_the_standard(std::meta::dealias(^^U));

// The type arguments of a class template specialization.  A value
// argument, such as the capacity of an inplace_vector, is not state.
[[nodiscard]] consteval std::vector<std::meta::info> type_arguments_of(std::meta::info type) {
    std::vector<std::meta::info> types;
    if (!std::meta::has_template_arguments(type)) return types;
    for (std::meta::info const argument : std::meta::template_arguments_of(type)) {
        if (std::meta::is_type(argument)) types.push_back(argument);
    }
    return types;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The first type through which T reaches state outside itself, or a null
// reflection when T owns everything it reaches.  `visiting` holds the
// class types on the current path and the ones already read, so a type
// that contains itself through a container ends its own walk.
template <typename T>
[[nodiscard]] consteval auto first_outside_reach(std::vector<std::meta::info>& visiting) -> std::meta::info {
    // The reflection of the type itself, not of the alias that names it
    // here, so two paths to one type compare equal.
    constexpr std::meta::info self = std::meta::dealias(^^T);
    if constexpr (std::is_reference_v<T>) {
        return self;
    } else {
        using U = std::remove_cv_t<T>;
        constexpr std::meta::info bare = std::meta::dealias(^^U);
        if constexpr (std::is_pointer_v<U>) {
            return bare;
        } else if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U> || std::is_member_pointer_v<U>
                             || std::is_null_pointer_v<U>) {
            return std::meta::info{};
        } else if constexpr (std::is_array_v<U>) {
            return first_outside_reach<std::remove_all_extents_t<U>>(visiting);
        } else if constexpr (std::is_class_v<U> || std::is_union_v<U>) {
            for (std::meta::info const seen : visiting) {
                if (seen == bare) return std::meta::info{};
            }
            visiting.push_back(bare);
            if constexpr (is_standard_container<U>) {
                if constexpr (std::ranges::borrowed_range<U> || writes_through_const<U>) {
                    return bare;
                } else {
                    std::meta::info const found = first_outside_reach<std::ranges::range_value_t<U>>(visiting);
                    if (found != std::meta::info{}) return found;
                    // The comparator, the hasher and the allocator are part
                    // of the container, and a template argument names each.
                    static constexpr auto arguments = std::define_static_array(
                        static_cast<::foundation::reflect::anchored_t<bare, std::vector<std::meta::info>>>(
                            type_arguments_of(bare)));
                    template for (constexpr std::meta::info argument : arguments) {
                        std::meta::info const reached = first_outside_reach<typename[:argument:]>(visiting);
                        if (reached != std::meta::info{}) return reached;
                    }
                    return std::meta::info{};
                }
            } else {
                static constexpr auto bases = std::define_static_array(
                    static_cast<::foundation::reflect::anchored_t<bare, std::vector<std::meta::info>>>(
                        std::meta::bases_of(bare, std::meta::access_context::unchecked())));
                static constexpr auto members = std::define_static_array(
                    static_cast<::foundation::reflect::anchored_t<bare, std::vector<std::meta::info>>>(
                        std::meta::nonstatic_data_members_of(bare, std::meta::access_context::unchecked())));
                // Storage the walk cannot account for.  A member-less union
                // or a one-byte closure holds no address, so only a larger
                // one is refused.
                if constexpr (bases.size() == 0 && members.size() == 0 && !std::is_empty_v<U> && sizeof(U) > 1) {
                    return bare;
                } else {
                    template for (constexpr std::meta::info base : bases) {
                        std::meta::info const found =
                            first_outside_reach<typename[:std::meta::type_of(base):]>(visiting);
                        if (found != std::meta::info{}) return found;
                    }
                    template for (constexpr std::meta::info member : members) {
                        if constexpr (std::meta::is_mutable_member(member)) {
                            return bare;
                        } else {
                            std::meta::info const found =
                                first_outside_reach<typename[:std::meta::type_of(member):]>(visiting);
                            if (found != std::meta::info{}) return found;
                        }
                    }
                    return std::meta::info{};
                }
            }
        } else {
            // void or a function type, which only a template argument can
            // name.  Neither is state the walk can read, so it is refused.
            return bare;
        }
    }
}

#pragma GCC diagnostic pop

// The answer of a walk that found no way out.  It is a type of its own,
// so no type the walk can reach is mistaken for it.
struct nothing_reaches_out {};

// The walk for T, run one time, with the sentinel in place of a null
// reflection.
template <typename T>
[[nodiscard]] consteval auto way_out_of() -> std::meta::info {
    std::vector<std::meta::info> visiting;
    std::meta::info const found = first_outside_reach<T>(visiting);
    return found == std::meta::info{} ? ^^nothing_reaches_out : found;
}

template <typename T>
inline constexpr std::meta::info way_out_v = way_out_of<T>();

}  // namespace detail::self_contained

// The type through which T reaches state outside itself or is written
// through const, or detail::self_contained::nothing_reaches_out when there
// is none.  The walk reports the innermost way out: a struct that holds a
// string_view names the pointer in the view, and a struct with a mutable
// member names that struct.
template <typename T>
using outside_reach_t = [:detail::self_contained::way_out_v<T>:];

// The concept exists so that a refusal names the way out in its
// diagnostic: "is_same_v<WayOut, ...nothing_reaches_out> [with WayOut =
// const char*]".
template <typename WayOut>
concept NoWayOut = std::is_same_v<WayOut, detail::self_contained::nothing_reaches_out>;

// T is an object, and it owns everything a reader can reach through it.
template <typename T>
concept SelfContained = std::is_object_v<T> && NoWayOut<outside_reach_t<T>>;

// ── Equality derived from the members ───────────────────────────────
//
// Two self-contained values are the same when their members are the
// same, member by member, down to the scalars.  The comparison is derived
// from the type by reflection, the way a derived equality is in Haskell or
// in Rust, and a hand-written operator== plays no part in it.  A value
// owns everything a reader can reach through it, and a const reader sees
// nothing else, so two values with equal members give the same answer to
// every const question: the derived equality implies observational
// equality.  A hand-written operator== can hold for values that differ in
// what a reader observes, and a caller that must know whether two values
// are one event then learns nothing from it.
//
// The rules follow the ones of SelfContained:
//
//   - an integral, an enumeration, a member pointer and nullptr_t compare
//     by value;
//   - a float and a double compare by their bits, so +0 and -0 differ and
//     two equal NaNs agree.  Any other floating type is refused, because
//     its object representation has padding;
//   - an array compares element by element;
//   - a standard container compares its size and then its elements in
//     order.  Two unordered containers that hold the same elements in two
//     orders compare different, which errs toward a conflict;
//   - a std::optional compares its engagement and its value, and a
//     std::variant its index and its alternative;
//   - any other class compares its bases and its data members;
//   - a union is refused, because no rule knows which member is active;
//   - a class outside the standard library that has an operator== is
//     refused unless it declares that operator== as a defaulted member.
//     A defaulted one compares the members, as the derived equality does.
//     An operator== that a friend declaration or a namespace declares is
//     refused too, because reflection does not show whether it is
//     defaulted.

namespace detail::self_contained {

// True when the class declares a member operator==, and each one it
// declares is defaulted.
[[nodiscard]] consteval bool declares_only_defaulted_equality(std::meta::info type) {
    bool declares_one = false;
    for (std::meta::info const member : std::meta::members_of(type, std::meta::access_context::unchecked())) {
        if (!std::meta::is_function(member) || !std::meta::is_operator_function(member)) continue;
        if (std::meta::operator_of(member) != std::meta::op_equals_equals) continue;
        if (!std::meta::is_defaulted(member)) return false;
        declares_one = true;
    }
    return declares_one;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The first type that the derived equality cannot compare, or a null
// reflection when it can compare all of T.
template <typename T>
[[nodiscard]] consteval auto first_uncomparable(std::vector<std::meta::info>& visiting) -> std::meta::info {
    using U = std::remove_cv_t<T>;
    constexpr std::meta::info bare = std::meta::dealias(^^U);
    if constexpr (std::is_reference_v<T> || std::is_pointer_v<U> || std::is_union_v<U>) {
        return bare;
    } else if constexpr (std::is_floating_point_v<U>) {
        if constexpr (sizeof(U) == 4 || sizeof(U) == 8) {
            return std::meta::info{};
        } else {
            return bare;
        }
    } else if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U> || std::is_member_pointer_v<U>
                         || std::is_null_pointer_v<U>) {
        return std::meta::info{};
    } else if constexpr (std::is_array_v<U>) {
        return first_uncomparable<std::remove_all_extents_t<U>>(visiting);
    } else if constexpr (std::is_class_v<U>) {
        for (std::meta::info const seen : visiting) {
            if (seen == bare) return std::meta::info{};
        }
        visiting.push_back(bare);
        if constexpr (is_standard_container<U>) {
            return first_uncomparable<std::ranges::range_value_t<U>>(visiting);
        } else if constexpr (::foundation::reflect::IsInstanceOf<U, ^^std::optional>) {
            return first_uncomparable<typename U::value_type>(visiting);
        } else if constexpr (::foundation::reflect::IsInstanceOf<U, ^^std::variant>) {
            static constexpr auto alternatives = std::define_static_array(
                static_cast<::foundation::reflect::anchored_t<bare, std::vector<std::meta::info>>>(
                    type_arguments_of(bare)));
            template for (constexpr std::meta::info alternative : alternatives) {
                std::meta::info const found = first_uncomparable<typename[:alternative:]>(visiting);
                if (found != std::meta::info{}) return found;
            }
            return std::meta::info{};
        } else {
            if constexpr (!declared_by_the_standard(bare) && std::equality_comparable<U>) {
                if (!declares_only_defaulted_equality(bare)) return bare;
            }
            static constexpr auto bases = std::define_static_array(
                static_cast<::foundation::reflect::anchored_t<bare, std::vector<std::meta::info>>>(
                    std::meta::bases_of(bare, std::meta::access_context::unchecked())));
            static constexpr auto members = std::define_static_array(
                static_cast<::foundation::reflect::anchored_t<bare, std::vector<std::meta::info>>>(
                    std::meta::nonstatic_data_members_of(bare, std::meta::access_context::unchecked())));
            if constexpr (bases.size() == 0 && members.size() == 0 && !std::is_empty_v<U>) {
                return bare;
            } else {
                template for (constexpr std::meta::info base : bases) {
                    std::meta::info const found = first_uncomparable<typename[:std::meta::type_of(base):]>(visiting);
                    if (found != std::meta::info{}) return found;
                }
                template for (constexpr std::meta::info member : members) {
                    std::meta::info const found = first_uncomparable<typename[:std::meta::type_of(member):]>(visiting);
                    if (found != std::meta::info{}) return found;
                }
                return std::meta::info{};
            }
        }
    } else {
        return bare;
    }
}

#pragma GCC diagnostic pop

// The answer of a walk that found every part comparable.
struct every_part_compares {};

template <typename T>
[[nodiscard]] consteval auto uncomparable_part_of() -> std::meta::info {
    std::vector<std::meta::info> visiting;
    std::meta::info const found = first_uncomparable<T>(visiting);
    return found == std::meta::info{} ? ^^every_part_compares : found;
}

template <typename T>
inline constexpr std::meta::info uncomparable_part_v = uncomparable_part_of<T>();

}  // namespace detail::self_contained

// The part of T that the derived equality cannot compare, or
// detail::self_contained::every_part_compares when there is none.
template <typename T>
using uncomparable_part_t = [:detail::self_contained::uncomparable_part_v<T>:];

// The concept exists so that a refusal names the part in its diagnostic.
template <typename Part>
concept ComparablePart = std::is_same_v<Part, detail::self_contained::every_part_compares>;

// T owns what it reaches, and the members of T decide its equality.
template <typename T>
concept EqualityByMembers = SelfContained<T> && ComparablePart<uncomparable_part_t<T>>;

// The derived equality.  Linear in the size of the two values.
template <EqualityByMembers T>
[[nodiscard]] constexpr bool equal_by_members(T const& a, T const& b) noexcept {
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_floating_point_v<U>) {
        using Bits = std::conditional_t<sizeof(U) == 4, std::uint32_t, std::uint64_t>;
        return std::bit_cast<Bits>(a) == std::bit_cast<Bits>(b);
    } else if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U> || std::is_member_pointer_v<U>
                         || std::is_null_pointer_v<U>) {
        return a == b;
    } else if constexpr (std::is_array_v<U>) {
        for (std::size_t i = 0; i < std::extent_v<U>; ++i) {
            if (!equal_by_members(a[i], b[i])) return false;
        }
        return true;
    } else if constexpr (detail::self_contained::is_standard_container<U>) {
        using Element = std::ranges::range_value_t<U>;
        auto left = std::ranges::begin(a);
        auto right = std::ranges::begin(b);
        auto const left_end = std::ranges::end(a);
        auto const right_end = std::ranges::end(b);
        for (; left != left_end && right != right_end; ++left, ++right) {
            Element const& left_element = *left;
            Element const& right_element = *right;
            if (!equal_by_members(left_element, right_element)) return false;
        }
        return left == left_end && right == right_end;
    } else if constexpr (::foundation::reflect::IsInstanceOf<U, ^^std::optional>) {
        if (a.has_value() != b.has_value()) return false;
        return !a.has_value() || equal_by_members(*a, *b);
    } else if constexpr (::foundation::reflect::IsInstanceOf<U, ^^std::variant>) {
        if (a.index() != b.index()) return false;
        if (a.valueless_by_exception()) return true;
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return ((a.index() == I && equal_by_members(std::get<I>(a), std::get<I>(b))) || ...);
        }(std::make_index_sequence<std::variant_size_v<U>>{});
    } else {
        constexpr std::meta::info bare = std::meta::dealias(^^U);
        static constexpr auto bases = std::define_static_array(
            static_cast<::foundation::reflect::anchored_t<bare, std::vector<std::meta::info>>>(
                std::meta::bases_of(bare, std::meta::access_context::unchecked())));
        static constexpr auto members = std::define_static_array(
            static_cast<::foundation::reflect::anchored_t<bare, std::vector<std::meta::info>>>(
                std::meta::nonstatic_data_members_of(bare, std::meta::access_context::unchecked())));
        bool same = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
        template for (constexpr std::meta::info base : bases) {
            using Base = typename[:std::meta::type_of(base):];
            same = same && equal_by_members(static_cast<Base const&>(a), static_cast<Base const&>(b));
        }
        template for (constexpr std::meta::info member : members) {
            same = same && equal_by_members(a.[:member:], b.[:member:]);
        }
#pragma GCC diagnostic pop
        return same;
    }
}

}  // namespace fixy
