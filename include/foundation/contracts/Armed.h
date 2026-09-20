#pragma once

// A predicate that answers "no" for every type is indistinguishable, at
// every call site, from a predicate nobody armed.  Both let the caller
// through.  These two helpers are the witness that a predicate still has
// arms: the author names, beside the predicate, the types it must accept
// and the types it must refuse, and the build fails on the day an arm is
// deleted rather than on the day someone notices a gate stopped gating.
//
// The shape this exists for was measured, not imagined.  fixy/Qtt.h
// carried two gates whose tables the port left with a false primary and
// no specialization at all, where the old tree had four.  Both gates
// read their table, the table answered "no" to every question, and both
// static assertions became tautologies: Affine<Linear<int>> compiled and
// an exactly-once obligation silently became at-most-once.  Nothing in
// the tree could say so, because a table with no arms and a table whose
// arms all refuse are the same text.
//
// Pred is a class trait, which is the form a template template argument
// can name.  A predicate that exists only as a concept or as a variable
// template gets a one-line trait beside it, and that trait is then what
// these helpers read.
//
// Both directions are needed.  A predicate that answers "yes" for
// everything is as unarmed as one that answers "no", and only one of the
// two is caught by acceptance alone.

#include <type_traits>

namespace foundation::contracts {

// True when Pred accepts every witness, and there is at least one
// witness.  An empty pack proves nothing and answers false.
template <template <class> class Pred, class... Witnesses>
[[nodiscard]] consteval bool predicate_accepts() noexcept {
    return sizeof...(Witnesses) > 0 && (static_cast<bool>(Pred<Witnesses>::value) && ...);
}

// True when Pred refuses every witness, and there is at least one
// witness.  An empty pack proves nothing and answers false.
template <template <class> class Pred, class... Witnesses>
[[nodiscard]] consteval bool predicate_refuses() noexcept {
    return sizeof...(Witnesses) > 0 && (!static_cast<bool>(Pred<Witnesses>::value) && ...);
}

namespace detail::armed_self_test {

template <class T>
struct always_true : std::true_type {};

template <class T>
struct always_false : std::false_type {};

template <class T>
struct is_int : std::bool_constant<std::is_same_v<T, int>> {};

static_assert(predicate_accepts<is_int, int>());
static_assert(predicate_refuses<is_int, char, void>());
static_assert(!predicate_accepts<is_int, char>());
static_assert(!predicate_refuses<is_int, int>());

// An empty witness pack proves nothing, so neither helper answers yes.
static_assert(!predicate_accepts<always_true>());
static_assert(!predicate_refuses<always_false>());

// The two unarmed shapes, each caught by exactly one of the pair.
static_assert(!predicate_accepts<always_false, int>());
static_assert(!predicate_refuses<always_true, int>());

}  // namespace detail::armed_self_test

}  // namespace foundation::contracts
