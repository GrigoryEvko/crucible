#pragma once

// A noexcept declaration is a promise that the callable can still break.
// A throw that gets to a noexcept boundary calls std::terminate.  The
// process stops there, and a structured join does not unwind.  Nothing in
// this tree throws, and utils/scripts/check-no-throw-no-rtti.sh holds that on
// the built artifact.  The control-flow atom fixy::atom::ctrl::throws is
// the type-level record that a callable transits a throw, so a consumer
// can reject it by searching the type tree rather than trusting the
// declaration.
//
// Design notes:
//
//  1. The needle is the template, not one specialization of it.  The
//     atom is parametric on the exception family, and the default family
//     is only one of its specializations.  A search for throws<> alone
//     passes a callable carrying throws<MyException>: the walk descends
//     into the family argument without ever matching the carrier.  The
//     match here is the reflection query of foundation/reflect/Instance.h
//     asked of the template, so every family is caught.  A gate that
//     rejects a throwing callable has to reject all of them.
//
//  2. The walk takes a predicate rather than a type.  One recursion
//     serves both the exact-type search and the template search the
//     throws gate wants, so there is no second copy of the descent.
//     type_tree_contains_v is the exact-type search.
//
//  3. There is no `fixy::throws` alias.  A short name for the
//     default-family specialization invites the bug design note 1
//     closes, because that one type is not the whole answer.  Callers
//     name fixy::atom::ctrl::throws<> when they mean that one
//     specialization.
//
// The walk is foundation/reflect/TypeComponents.h.  It reads each type
// argument and the type of each value argument, so a carrier with a
// non-type parameter is descended like any other.  It also reads the
// bases and the by-value members of a class, so a plain class that holds
// a throwing wrapper in a member is found too.  The header states what
// the walk cannot see: a lambda capture, a value behind type erasure,
// and a member of a specialization that the walk reaches only through a
// pointer or a template argument.  A closure that escapes the search
// still has to satisfy the nothrow-invocable requirement at its use
// site.

#include <fixy/atoms/Ctrl.h>
#include <foundation/reflect/Instance.h>
#include <foundation/reflect/TypeComponents.h>

#include <meta>

#include <tuple>
#include <type_traits>

namespace fixy {

namespace detail {

// The answer of Match for one node, read by the walk through a
// reflection of this variable.
template <template <class> class Match, typename Node>
inline constexpr bool node_matches_v = Match<Node>::value;

// True when Match accepts Haystack or a component of it.  The walk
// strips cv, reference and alias from each node before it asks, so a
// reference to a carrier answers the same as the carrier.
template <template <class> class Match, typename Haystack>
struct type_tree_any
    : std::bool_constant<
          ::foundation::reflect::any_component_satisfies<[](::foundation::reflect::TypeNode node) consteval {
              return std::meta::extract<bool>(std::meta::substitute(^^node_matches_v, {^^Match, node.type}));
          }>(^^Haystack)> {};

// The exact-type predicate, as a member template so the needle binds
// before type_tree_any takes the predicate as a template-template
// argument.
template <typename Needle>
struct match_exactly {
    template <typename Node>
    struct pred : std::bool_constant<std::is_same_v<std::remove_cvref_t<Node>, Needle>> {};
};

// Every specialization of the atom answers true, whatever exception
// family it names.  The query is a concept over reflection functions, so
// no translation unit can specialize its answer.
template <typename Node>
struct is_throws_atom
    : std::bool_constant<::foundation::reflect::IsInstanceOf<std::remove_cvref_t<Node>, ^^::fixy::atom::ctrl::throws>> {
};

}  // namespace detail

// True when Needle occurs anywhere in Haystack's type tree, compared as
// an exact type after stripping cv and reference qualifiers.
template <typename Needle, typename Haystack>
inline constexpr bool type_tree_contains_v =
    detail::type_tree_any<detail::match_exactly<Needle>::template pred, Haystack>::value;

// True when any specialization of the throws atom occurs anywhere in T's
// type tree.
template <typename T>
inline constexpr bool type_tree_contains_throws_v = detail::type_tree_any<detail::is_throws_atom, T>::value;

}  // namespace fixy
