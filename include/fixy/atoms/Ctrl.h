#pragma once

// The control-flow atoms: the ways a binding can leave its frame other
// than by returning.  Every atom here engages Axis::ControlFlow.
//
// Old spelling: include/crucible/fixy/grant/Ctrl.h.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace fixy::atom::ctrl {

// A string literal cannot be a template argument through a pointer,
// because its address is not a constant. Copying the characters into a
// structural class type makes the literal itself part of the type, so
// two sites that state different reasons are different types.
template <std::size_t N>
struct rationale final {
    char data[N]{};

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

// The build compiles without exceptions, so no binding can hold this
// atom. It exists so a binding compiled with exceptions can name the
// family it throws.
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

template <class SuspensionPolicy>
struct coroutine final : atom_of<Axis::ControlFlow> {};

}  // namespace fixy::atom::ctrl

namespace fixy::atom::detail {

struct ctrl_sample_exception final {};

using ctrl_atom_roster =
    std::tuple<ctrl::throws<>, ctrl::throws<ctrl_sample_exception>, ctrl::abort<"oom unrecoverable">,
               ctrl::longjmp_unsafe<"setjmp island">, ctrl::exit<ctrl::at_exit>, ctrl::exit<ctrl::no_cleanup>,
               ctrl::exit<ctrl::exit_immediate>, ctrl::builtin_trap_ok, ctrl::unreachable_ok,
               ctrl::coroutine<ctrl::co_await_only>, ctrl::coroutine<ctrl::generator>,
               ctrl::coroutine<ctrl::async_task>>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::ctrl_atom_self_test {

// The roster is a hand list, so this reads the family namespace and
// asks the list about each plain atom it finds.  A parametric atom is
// not covered; fixy/Atom.h says why beside the walk.
static_assert(every_atom_in_is_rostered_<^^::fixy::atom::ctrl, ctrl_atom_roster>(),
              "fixy/atoms/Ctrl.h: an atom declared in fixy::atom::ctrl is missing from "
              "ctrl_atom_roster.");

static_assert(every_roster_member_is_atom_<ctrl_atom_roster>(),
              "fixy/atoms/Ctrl.h: a member of ctrl_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<ctrl_atom_roster, Axis::ControlFlow>(),
              "fixy/atoms/Ctrl.h: every control-flow atom engages Axis::ControlFlow.");

static_assert(!IsAtom<ctrl::any_exception>);
static_assert(!IsAtom<ctrl::at_exit>);
static_assert(!IsAtom<ctrl::co_await_only>);

// The rationale is part of the type: two reasons are two types, the
// same reason is one type, and a prefix is not the whole.
static_assert(!std::is_same_v<ctrl::abort<"reason A">, ctrl::abort<"reason B">>);
static_assert(std::is_same_v<ctrl::abort<"same">, ctrl::abort<"same">>);
static_assert(!std::is_same_v<ctrl::abort<"ab">, ctrl::abort<"abc">>);
static_assert(!std::is_same_v<ctrl::abort<"x">, ctrl::longjmp_unsafe<"x">>);
static_assert(ctrl::rationale{"oom"}.size() == 4);  // three characters plus the terminator

}  // namespace fixy::atom::detail::ctrl_atom_self_test
