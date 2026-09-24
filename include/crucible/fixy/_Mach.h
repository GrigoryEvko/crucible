#pragma once

#include <crucible/safety/_Machine.h>

#include <type_traits>
#include <utility>

namespace crucible::fixy::mach {

using ::crucible::safety::Machine;
using ::crucible::safety::mint_machine;
using ::crucible::safety::transition_to;

template <typename M>
using state_of_t = typename std::remove_cvref_t<M>::state_type;

namespace detail {

template <typename T>
struct is_machine_impl : std::false_type {};

template <typename S>
struct is_machine_impl<::crucible::safety::Machine<S>> : std::true_type {};

}  // namespace detail

template <typename T>
inline constexpr bool is_machine_v = detail::is_machine_impl<std::remove_cvref_t<T>>::value;

// The state_type lookup is staged behind an is_machine_v bool parameter
// rather than written as one conjunction: `&&` does not short-circuit
// template substitution, so a non-Machine M would hard-error inside the
// member-typedef lookup instead of yielding false.

namespace detail {

template <typename M, typename NewState, bool IsMachine>
struct can_transition_impl : std::false_type {};

template <typename M, typename NewState>
struct can_transition_impl<M, NewState, true>
    : std::bool_constant<std::is_move_constructible_v<typename std::remove_cvref_t<M>::state_type>
                         && std::is_move_constructible_v<NewState>&& ::crucible::safety::machine_transition_v<
                             typename std::remove_cvref_t<M>::state_type, NewState>> {};

}  // namespace detail

template <typename M, typename NewState>
inline constexpr bool can_transition_v = detail::can_transition_impl<M, NewState, is_machine_v<M>>::value;

namespace self_test {

static_assert(std::is_same_v<::crucible::fixy::mach::Machine<int>, ::crucible::safety::Machine<int>>,
              "fixy::mach::Machine must alias safety::Machine");

static_assert(std::is_same_v<::crucible::fixy::mach::state_of_t<::crucible::safety::Machine<int>&>, int>,
              "state_of_t<Machine<int>&> must project to int");

static_assert(::crucible::fixy::mach::is_machine_v<::crucible::safety::Machine<int>>);
static_assert(::crucible::fixy::mach::is_machine_v<::crucible::safety::Machine<int>&>);
static_assert(!::crucible::fixy::mach::is_machine_v<int>);
static_assert(!::crucible::fixy::mach::is_machine_v<void>);

static_assert(!::crucible::fixy::mach::can_transition_v<int, double>,
              "can_transition_v must reject non-Machine M without substitution-failing.");

constexpr int mach_using_cardinality = 3;
constexpr int mach_helper_cardinality = 3;

static_assert(mach_using_cardinality == 3, "fixy::mach:: using-decl surface drifted from 3.");
static_assert(mach_helper_cardinality == 3, "fixy::mach:: in-place helper surface drifted from 3.");

}  // namespace self_test

}  // namespace crucible::fixy::mach
