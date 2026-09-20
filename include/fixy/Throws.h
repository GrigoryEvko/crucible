#pragma once

// A noexcept declaration is a promise the callable can still break.  The
// throw is rewritten to an abort under -fno-exceptions and terminates at
// the noexcept boundary under -fexceptions, and either outcome tears
// through a structured join instead of unwinding it.  The control-flow
// atom fixy::atom::ctrl::throws is the type-level record that a callable
// transits a throw, so a consumer can reject it by searching the type
// tree rather than trusting the declaration.
//
// Old spelling: include/crucible/fixy/ctrl/Throws.h.
//
// Deviations, each deliberate:
//
//  1. The needle is the template, not one specialization of it.  The old
//     header aliased `throws = grant::ctrl::throws<>` and searched for
//     that one type, so a callable carrying throws<MyException> passed
//     the gate: the atom is parametric on the exception family, the
//     default family is only one of its specializations, and the walk
//     descended into the family argument without ever matching the
//     carrier.  Both trees had that hole and neither disclosed it.  The
//     match here is the reflection query of foundation/reflect/Instance.h
//     asked of the template, so every family is caught.  A gate that
//     rejects a throwing callable has to reject all of them.
//
//  2. The walk takes a predicate rather than a type.  One recursion now
//     serves both the exact-type search the old header exported and the
//     template search the throws gate wants, instead of a second copy of
//     the descent.  type_tree_contains_v keeps the old exact-match
//     meaning for every other caller.
//
//  3. There is no `fixy::throws` alias.  The old one named the
//     default-family specialization, which is exactly the type this
//     header no longer treats as the whole answer, so a short name for
//     it would invite the bug deviation 1 closes.  Callers name
//     fixy::atom::ctrl::throws<> when they mean that one specialization.
//
// The disclosed hole, unchanged: the recursion descends only through
// templates whose parameters are all types.  A carrier with a non-type
// template parameter falls through to the primary template and is never
// descended, so an atom nested inside one is not found.  That false
// negative is accepted for the same reason the old header accepted it:
// the search targets named callable wrappers that declare the atom in
// their template arguments, and a closure that escapes the search still
// has to satisfy the nothrow-invocable requirement at its use site.

#include <fixy/atoms/Ctrl.h>
#include <foundation/reflect/Instance.h>

#include <tuple>
#include <type_traits>

namespace fixy {

namespace detail {

// The primary strips cv and reference qualifiers before asking, so a
// reference to a carrier answers the same as the carrier.  The partial
// specialization fires only for a class template specialization whose
// arguments are all types, which is the disclosed hole above: anything
// else reaches the primary and is tested without being descended.
template <template <class> class Match, typename Haystack>
struct type_tree_any : std::bool_constant<Match<std::remove_cvref_t<Haystack>>::value> {};

template <template <class> class Match, template <typename...> class Tmpl, typename... Args>
struct type_tree_any<Match, Tmpl<Args...>>
    : std::bool_constant<Match<Tmpl<Args...>>::value || (type_tree_any<Match, Args>::value || ...)> {};

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
    : std::bool_constant<
          ::foundation::reflect::IsInstanceOf<std::remove_cvref_t<Node>, ^^::fixy::atom::ctrl::throws>> {};

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

namespace fixy::detail::throws_self_test {

namespace ctrl = ::fixy::atom::ctrl;

struct unrelated_tag {};
struct sample_exception {};

// The atom itself, at the default family and at a named one.
static_assert(type_tree_contains_throws_v<ctrl::throws<>>);
static_assert(type_tree_contains_throws_v<ctrl::throws<sample_exception>>);

// Qualifiers do not hide the carrier.
static_assert(type_tree_contains_throws_v<ctrl::throws<> const>);
static_assert(type_tree_contains_throws_v<ctrl::throws<>&>);
static_assert(type_tree_contains_throws_v<ctrl::throws<> const&>);
static_assert(type_tree_contains_throws_v<ctrl::throws<> volatile>);

// Nested at every depth, and beside unrelated members.
static_assert(type_tree_contains_throws_v<std::tuple<ctrl::throws<>>>);
static_assert(type_tree_contains_throws_v<std::tuple<int, ctrl::throws<>>>);
static_assert(type_tree_contains_throws_v<std::tuple<int, ctrl::throws<> const&>>);
static_assert(type_tree_contains_throws_v<std::tuple<int, std::tuple<ctrl::throws<>, double>>>);
static_assert(type_tree_contains_throws_v<std::tuple<int, std::tuple<unrelated_tag, std::tuple<ctrl::throws<>>>>>);

// A named family nested, which is the case the old needle missed.
static_assert(type_tree_contains_throws_v<std::tuple<int, ctrl::throws<sample_exception>>>);
static_assert(type_tree_contains_throws_v<std::tuple<std::tuple<ctrl::throws<sample_exception>>, double>>);

// Nothing else answers true.
static_assert(!type_tree_contains_throws_v<int>);
static_assert(!type_tree_contains_throws_v<int const&>);
static_assert(!type_tree_contains_throws_v<void>);
static_assert(!type_tree_contains_throws_v<unrelated_tag>);
static_assert(!type_tree_contains_throws_v<std::tuple<int, double>>);
static_assert(!type_tree_contains_throws_v<std::tuple<int, std::tuple<unrelated_tag, double>>>);

// The exception family is a plain tag, not an atom, so naming it alone
// is not naming a throw.
static_assert(!type_tree_contains_throws_v<ctrl::any_exception>);
static_assert(!type_tree_contains_throws_v<std::tuple<ctrl::any_exception>>);

// Deriving from the atom to reach the match is not merely unmatched, it
// does not compile: the atom is final.  That is what closes the cheat,
// so it is what this cell asserts.  The reflection query would refuse a
// derived type anyway, because it reports the type it is asked about and
// never its bases.
static_assert(std::is_final_v<ctrl::throws<>>);
static_assert(std::is_final_v<ctrl::throws<sample_exception>>);

// The exact-type search keeps its old meaning, including for a needle
// that has nothing to do with control flow.
static_assert(type_tree_contains_v<unrelated_tag, unrelated_tag>);
static_assert(type_tree_contains_v<unrelated_tag, std::tuple<int, unrelated_tag>>);
static_assert(type_tree_contains_v<unrelated_tag, std::tuple<int, unrelated_tag const&>>);
static_assert(!type_tree_contains_v<unrelated_tag, std::tuple<int, double>>);
static_assert(type_tree_contains_v<ctrl::throws<>, std::tuple<int, ctrl::throws<>>>);

// And it is exact: the default family is not the named one.
static_assert(!type_tree_contains_v<ctrl::throws<>, ctrl::throws<sample_exception>>);
static_assert(type_tree_contains_throws_v<ctrl::throws<sample_exception>>);

// The disclosed hole, pinned as a cell rather than left to be
// rediscovered: a carrier with a non-type template parameter is not
// descended, so the atom inside it is not found.
template <int N>
struct nttp_carrier {
    using hidden = ctrl::throws<>;
};
static_assert(!type_tree_contains_throws_v<nttp_carrier<1>>,
              "the disclosed hole: a template with a non-type parameter is not descended. If this "
              "assertion starts failing the walk has been generalized, which is an improvement: "
              "delete the cell and the paragraph in the header that discloses it.");

}  // namespace fixy::detail::throws_self_test
