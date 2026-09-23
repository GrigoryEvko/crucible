#pragma once

// The types a value holds, read by reflection.
//
// Three gates ask one question of a type: does it hold something of a
// given kind anywhere inside it.  The throws gate asks it of a callable,
// the extract gate asks it of a payload that could convey authority, and
// the payload row asks it of a channel payload that could carry an effect
// row.  A walk over the template arguments alone answers for a wrapper
// that names what it holds.  It does not answer for a plain class that
// holds it in a member, or for a template whose parameters are not all
// types, and those shapes slipped past each gate.  This header gives the
// one step of the walk that all three read.
//
// One step lists the components of a type:
//
//   * the element of a pointer, a reference or an array;
//   * each type argument of a class template specialization, and the
//     type of each value argument;
//   * each base and each non-static data member of a class, when the
//     walk may read its members.
//
// A template template argument is not a type, and the walk skips it.  A
// member alias is a declaration, not a component, and the walk does not
// read it.
//
// When the walk may read members.  A read of the members of a class
// template specialization instantiates it, and an instantiation can stop
// the build with a static assertion that belongs to that template.  So
// the walk reads members only where the type is already complete:
//
//   * the root, which a caller holds by value;
//   * each base and each by-value member of a type whose members the walk
//     read, because a complete class has complete by-value members;
//   * the element of an array whose members the walk may read;
//   * a class that is not a specialization and that is complete, because
//     the completeness query instantiates nothing for such a class.
//
// Elsewhere the walk reads the template arguments and stops.  That is a
// type argument, a value argument, or the element of a pointer or a
// reference, when that type is a specialization.
//
// What the walk cannot see, stated rather than implied:
//
//   * a member of a specialization that the walk reaches through a
//     pointer or a template argument, when the arguments do not also name
//     that member;
//   * a value behind type erasure, as in std::function or std::any, where
//     the type of the held value is not part of the static type;
//   * a lambda capture.  GCC 16 reflects no data member of a closure
//     type, so a capture is not a component.  The self-test pins this, so
//     the day the compiler starts to reflect captures, the pin fails and
//     the gap closes by itself.

#include <cstddef>
#include <meta>
#include <vector>

namespace foundation::reflect {

// One node of the walk: a type, and whether the walk may read its
// members without an instantiation.
struct TypeNode {
    std::meta::info type{};
    bool may_read_members = false;
};

// The type with each alias, cv qualifier and reference removed.
[[nodiscard]] consteval std::meta::info bare_type(std::meta::info type) {
    return std::meta::dealias(std::meta::remove_cvref(std::meta::dealias(type)));
}

// True when a read of the members of `type` needs no instantiation: a
// class or union that is not a specialization and that is complete.
[[nodiscard]] consteval bool members_readable_without_instantiation(std::meta::info type) {
    const std::meta::info bare = bare_type(type);
    if (!std::meta::is_class_type(bare) && !std::meta::is_union_type(bare)) return false;
    if (std::meta::has_template_arguments(bare)) return false;
    return std::meta::is_complete_type(bare);
}

// The node for a type that the walk reaches other than by value.
[[nodiscard]] consteval TypeNode node_reached_indirectly(std::meta::info type) {
    const std::meta::info bare = bare_type(type);
    return TypeNode{bare, members_readable_without_instantiation(bare)};
}

// The components one step below `node`.  Complexity: linear in the
// number of template arguments, bases and members of the one type.
[[nodiscard]] consteval std::vector<TypeNode> components_of(TypeNode node) {
    std::vector<TypeNode> components;
    const std::meta::info type = bare_type(node.type);

    if (std::meta::is_pointer_type(type)) {
        components.push_back(node_reached_indirectly(std::meta::remove_pointer(type)));
        return components;
    }
    if (std::meta::is_array_type(type)) {
        components.push_back(TypeNode{bare_type(std::meta::remove_all_extents(type)), node.may_read_members});
        return components;
    }
    if (!std::meta::is_class_type(type) && !std::meta::is_union_type(type)) return components;

    if (std::meta::has_template_arguments(type)) {
        for (const std::meta::info argument : std::meta::template_arguments_of(type)) {
            if (std::meta::is_type(argument)) {
                components.push_back(node_reached_indirectly(argument));
            } else if (std::meta::is_value(argument) || std::meta::is_object(argument)) {
                components.push_back(node_reached_indirectly(std::meta::type_of(argument)));
            }
        }
    }

    if (node.may_read_members) {
        for (const std::meta::info base : std::meta::bases_of(type, std::meta::access_context::unchecked())) {
            components.push_back(TypeNode{bare_type(std::meta::type_of(base)), true});
        }
        for (const std::meta::info member :
             std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
            const std::meta::info member_type = std::meta::type_of(member);
            if (std::meta::is_reference_type(member_type)) {
                components.push_back(node_reached_indirectly(member_type));
            } else {
                components.push_back(TypeNode{bare_type(member_type), true});
            }
        }
    }
    return components;
}

// True when `predicate` accepts the root or a type the walk reaches from
// it.  The walk visits each node once, so a type that reaches itself
// through a pointer ends the walk.  Complexity: linear in the number of
// distinct nodes, times the cost of the visited-list scan.
//
// The root is read with its members, because the caller holds a value
// of it.  `predicate` is a consteval callable that takes a reflected
// type and returns bool.
template <auto Predicate>
[[nodiscard]] consteval bool any_component_satisfies(std::meta::info root) {
    std::vector<TypeNode> pending{TypeNode{bare_type(root), true}};
    std::vector<TypeNode> visited;
    while (!pending.empty()) {
        const TypeNode node = pending.back();
        pending.pop_back();
        bool was_visited = false;
        for (const TypeNode& seen : visited) {
            if (seen.type == node.type && (seen.may_read_members || !node.may_read_members)) {
                was_visited = true;
                break;
            }
        }
        if (was_visited) continue;
        visited.push_back(node);
        if (Predicate(node.type)) return true;
        for (const TypeNode& component : components_of(node)) pending.push_back(component);
    }
    return false;
}

namespace detail::type_components_self_test {

struct Needle {};
struct HoldsNeedle {
    Needle held;
};
struct DerivesNeedle : Needle {};
struct PointsAtNeedle {
    Needle* held = nullptr;
};
struct Unrelated {
    int value = 0;
};
struct SelfReferential {
    SelfReferential* next = nullptr;
    int value = 0;
};
template <class T>
struct Wrap {};
template <int N, class T>
struct MixedWrap {};
template <auto V>
struct ValueWrap {};

struct NeedleValue {
    Needle held{};
};

// The walk stops the build if it instantiates this template.
template <class T>
struct Detonates {
    static_assert(sizeof(T) == 0, "the walk instantiated a specialization that it reached indirectly");
};

inline constexpr auto is_needle = [](std::meta::info type) consteval { return type == ^^Needle; };

static_assert(any_component_satisfies<is_needle>(^^Needle));
static_assert(any_component_satisfies<is_needle>(^^Needle const&));
static_assert(any_component_satisfies<is_needle>(^^HoldsNeedle), "a member is a component");
static_assert(any_component_satisfies<is_needle>(^^DerivesNeedle), "a base is a component");
static_assert(any_component_satisfies<is_needle>(^^PointsAtNeedle), "a pointee is a component");
static_assert(any_component_satisfies<is_needle>(^^Needle[4]));
static_assert(any_component_satisfies<is_needle>(^^Wrap<Needle>));
static_assert(any_component_satisfies<is_needle>(^^MixedWrap<3, Needle>),
              "a template with a value parameter is still read for its type arguments");
static_assert(any_component_satisfies<is_needle>(^^ValueWrap<NeedleValue{}>),
              "the type of a value argument is a component");
static_assert(any_component_satisfies<is_needle>(^^Wrap<HoldsNeedle>),
              "a class that is not a specialization is read for members wherever the walk reaches it");

// The capture gap, pinned.  The closure holds a Needle and the walk does
// not see it, because GCC 16 reflects no data member of a closure type.
inline constexpr auto captures_needle = [held = NeedleValue{}] { return sizeof(held); };
static_assert(!any_component_satisfies<is_needle>(^^decltype(captures_needle)),
              "the walk now sees a lambda capture.  The compiler reflects captures, which closes a gap: delete "
              "this cell and the capture paragraph at the head of this header.");

static_assert(!any_component_satisfies<is_needle>(^^int));
static_assert(!any_component_satisfies<is_needle>(^^Unrelated));
static_assert(!any_component_satisfies<is_needle>(^^SelfReferential), "a cycle through a pointer ends the walk");
static_assert(!any_component_satisfies<is_needle>(^^Wrap<Unrelated>));

// A specialization reached through a template argument is read for its
// arguments and not instantiated.
static_assert(!any_component_satisfies<is_needle>(^^Wrap<Detonates<int>>));
static_assert(any_component_satisfies<is_needle>(^^Wrap<Detonates<Needle>>));

}  // namespace detail::type_components_self_test

}  // namespace foundation::reflect
