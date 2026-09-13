#pragma once

// A noexcept declaration is a promise the callable can still break. The
// throw is rewritten to an abort under -fno-exceptions and terminates at
// the noexcept boundary under -fexceptions, and either outcome tears
// through a structured join instead of unwinding it. This tag is the
// type-level record that a callable transits a throw, so a consumer can
// reject it by searching the type tree rather than trusting the
// declaration.

#include <crucible/fixy/Grant.h>
#include <crucible/fixy/grant/Ctrl.h>

#include <tuple>
#include <type_traits>

namespace crucible::fixy::ctrl {

using throws = ::crucible::fixy::grant::ctrl::throws<>;

// The recursion descends only through templates whose parameters are all
// types. A carrier with a non-type parameter falls through to the primary
// template and never matches. That false negative is accepted: the search
// targets named callable wrappers that declare the tag in their template
// arguments, and a closure that escapes it still has to satisfy the
// nothrow-invocable requirement at its use site.

namespace detail {

template <typename Needle, typename Haystack>
inline constexpr bool type_tree_match_v = std::is_same_v<std::remove_cvref_t<Haystack>, Needle>;

template <typename Needle, typename Haystack>
struct type_tree_contains : std::bool_constant<type_tree_match_v<Needle, Haystack>> {};

template <typename Needle, template <typename...> class Tmpl, typename... Args>
struct type_tree_contains<Needle, Tmpl<Args...>>
    : std::bool_constant<type_tree_match_v<Needle, Tmpl<Args...>> || (type_tree_contains<Needle, Args>::value || ...)> {
};

}  // namespace detail

template <typename Needle, typename Haystack>
inline constexpr bool type_tree_contains_v = detail::type_tree_contains<Needle, Haystack>::value;

template <typename T>
inline constexpr bool type_tree_contains_throws_v = type_tree_contains_v<throws, T>;

namespace detail::ctrl_self_test {

static_assert(::crucible::fixy::grant::IsGrantTag<throws>);
static_assert(std::is_empty_v<throws>);
static_assert(std::is_final_v<throws>);
static_assert(std::is_base_of_v<::crucible::fixy::grant::grant_base, throws>);
static_assert(sizeof(throws) == 1);

struct unrelated_tag {};
static_assert(!::crucible::fixy::grant::IsGrantTag<int>);
static_assert(!::crucible::fixy::grant::IsGrantTag<unrelated_tag>);

static_assert(type_tree_contains_v<throws, throws>);
static_assert(type_tree_contains_v<throws, throws const>);
static_assert(type_tree_contains_v<throws, throws&>);
static_assert(type_tree_contains_v<throws, throws const&>);
static_assert(type_tree_contains_v<throws, throws volatile>);
static_assert(type_tree_contains_v<throws, std::tuple<throws>>);
static_assert(type_tree_contains_v<throws, std::tuple<int, throws>>);
static_assert(type_tree_contains_v<throws, std::tuple<int, throws const&>>);
static_assert(type_tree_contains_v<throws, std::tuple<int, std::tuple<throws, double>>>);
static_assert(type_tree_contains_v<throws, std::tuple<int, std::tuple<unrelated_tag, std::tuple<throws>>>>);

static_assert(!type_tree_contains_v<throws, int>);
static_assert(!type_tree_contains_v<throws, int const&>);
static_assert(!type_tree_contains_v<throws, unrelated_tag>);
static_assert(!type_tree_contains_v<throws, std::tuple<int, double>>);
static_assert(!type_tree_contains_v<throws, std::tuple<int, std::tuple<unrelated_tag, double>>>);

static_assert(type_tree_contains_throws_v<throws>);
static_assert(type_tree_contains_throws_v<std::tuple<int, throws>>);
static_assert(!type_tree_contains_throws_v<int>);
static_assert(!type_tree_contains_throws_v<std::tuple<int, double>>);

}  // namespace detail::ctrl_self_test

}  // namespace crucible::fixy::ctrl
