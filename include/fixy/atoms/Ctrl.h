#pragma once

// The control-flow atoms: the ways a binding can leave its frame other
// than by returning.  Every atom here engages Axis::ControlFlow.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>

namespace fixy::atom::ctrl {

inline constexpr atom_seal atom_namespace_seal{};

// A string literal cannot be a template argument through a pointer,
// because its address is not a constant. Copying the characters into a
// structural class type makes the literal itself part of the type, so
// two sites that state different reasons are different types.
template <std::size_t N>
struct rationale final {
    std::array<char, N> data{};

    consteval rationale(const char (&literal)[N]) noexcept {
        for (std::size_t index = 0; index < N; ++index) {
            data[index] = literal[index];
        }
    }

    [[nodiscard]] static constexpr std::size_t size() noexcept { return N; }
};

// The policy tags below are template arguments to the atoms, never
// atoms themselves, so they do not inherit the atom marker.
struct any_exception final {};

// The three cleanup policies name distinct exit paths. at_exit runs the
// registered exit handlers and the destructors of objects with static
// storage duration. no_cleanup takes the same path with nothing left to
// run. exit_immediate skips both.
struct at_exit final {};
struct no_cleanup final {};
struct exit_immediate final {};

struct co_await_only final {};
struct generator final {};
struct async_task final {};

// Nothing in this tree throws, and utils/scripts/check-no-throw-no-rtti.sh
// fails the build when __cxa_throw reaches an artifact, so no binding
// here holds this atom. It exists so a binding outside the tree can name
// the family it throws.
template <class ExceptionFamily = any_exception>
struct throws final : atom_of<Axis::ControlFlow> {};

template <rationale Reason>
struct abort final : atom_of<Axis::ControlFlow> {};

// A long jump leaves the intervening scopes without running any
// destructor, so a resource whose release depends on one is lost.
template <rationale Reason>
struct longjmp_unsafe final : atom_of<Axis::ControlFlow> {};

template <class CleanupPolicy>
struct exit final : atom_of<Axis::ControlFlow> {};

// Spelled without the leading underscores of the builtin, which are
// reserved to the implementation.
struct builtin_trap_ok final : atom_of<Axis::ControlFlow> {};
struct unreachable_ok final : atom_of<Axis::ControlFlow> {};

// The binding raises a signal, or it runs code that a signal can
// interrupt at any instruction boundary.  A rule on async-signal safety
// reads this atom.
struct raises_signal final : atom_of<Axis::ControlFlow> {};

template <class SuspensionPolicy>
struct coroutine final : atom_of<Axis::ControlFlow> {};

}  // namespace fixy::atom::ctrl

namespace fixy::atom::detail {

struct ctrl_sample_exception final {};

using ctrl_atom_roster =
    std::tuple<ctrl::throws<>, ctrl::throws<ctrl_sample_exception>, ctrl::abort<"oom unrecoverable">,
               ctrl::longjmp_unsafe<"setjmp island">, ctrl::exit<ctrl::at_exit>, ctrl::exit<ctrl::no_cleanup>,
               ctrl::exit<ctrl::exit_immediate>, ctrl::builtin_trap_ok, ctrl::unreachable_ok, ctrl::raises_signal,
               ctrl::coroutine<ctrl::co_await_only>, ctrl::coroutine<ctrl::generator>,
               ctrl::coroutine<ctrl::async_task>>;

}  // namespace fixy::atom::detail
