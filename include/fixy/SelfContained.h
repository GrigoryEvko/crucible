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

#include <foundation/core/Array.h>
#include <foundation/core/Equatable.h>
#include <foundation/reflect/Anchor.h>

// <optional> of C++26 gives the range concepts and the range access of
// std::ranges (bits/ranges_base.h of libstdc++), with no view of <ranges>.
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <optional>
#include <type_traits>
#include <utility>
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

// ── Equality from the parts ─────────────────────────────────────────
//
// select_fresher of EpochVersioned and == of EpochVersioned and of Budgeted
// take two values of one grade as one event only when their payloads hold
// the same value.  They compare the payloads with equal of the Protocol
// family (foundation/core/Equatable.h), the one equality of the base.  A
// payload whose equality comes from a hand-written operator== could call two
// values equal that a reader tells apart, because that operator can skip a
// member or hide state, and a conflict at one version would then pass as one
// event.  So a payload of such a compare is ObservablyEquatable: its equality
// comes from its parts all the way down.  A part is:
//
//   - an integer, a bool, a character or an enumeration;
//   - a float or a double, which equal compares by its bits, so +0 and -0
//     differ and two NaNs agree only with the same bits;
//   - a bounded C array or a FixedArray of such parts;
//   - a class with the annotation derive_protocol whose bases and members
//     are such parts.
//
// A pointer is refused, because the value that it reaches is outside the
// payload.  A class with no annotation is refused, also one with a defaulted
// operator==, because the walk cannot see what its operator compares.  A
// union and a named bit-field are refused, as Equatable refuses them.

namespace detail::self_contained {

// True for a part of the list above.  Linear in the number of parts that the
// walk reaches.
[[nodiscard]] consteval bool equal_by_parts_at(std::meta::info spelled, int depth) {
    if (depth > ::foundation::core::detail::max_protocol_depth) return false;
    std::meta::info const type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (type == ^^float || type == ^^double) return true;
    if (std::meta::is_floating_point_type(type)) return false;
    if (std::meta::is_integral_type(type) || std::meta::is_enum_type(type)) return true;
    if (std::meta::is_array_type(type)) {
        return std::meta::is_bounded_array_type(type)
            && detail::self_contained::equal_by_parts_at(std::meta::remove_all_extents(type), depth + 1);
    }
    if (std::meta::is_class_type(type) && std::meta::has_template_arguments(type)
        && std::meta::template_of(type) == ^^::foundation::core::FixedArray) {
        return detail::self_contained::equal_by_parts_at(std::meta::template_arguments_of(type)[0], depth + 1);
    }
    if (!::foundation::core::detail::derives_protocol_at(type)) return false;
    std::size_t const bases = ::foundation::core::detail::base_count(type);
    std::size_t const members = ::foundation::core::detail::member_count(type);
    for (std::size_t index = 0; index < bases; ++index) {
        if (!detail::self_contained::equal_by_parts_at(::foundation::core::detail::base_type_at(type, index),
                                                       depth + 1)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < members; ++index) {
        if (::foundation::core::detail::member_is_bit_field_at(type, index)
            || !detail::self_contained::equal_by_parts_at(::foundation::core::detail::member_type_at(type, index),
                                                          depth + 1)) {
            return false;
        }
    }
    return true;
}

}  // namespace detail::self_contained

// A type that equal of the Protocol family compares, and whose equality
// comes from its parts all the way down (the list above).  equal of two of
// its values tells whether a reader can tell them apart.
template <typename T>
concept ObservablyEquatable = ::foundation::core::Equatable<T> && detail::self_contained::equal_by_parts_at(^^T, 0);

}  // namespace fixy
