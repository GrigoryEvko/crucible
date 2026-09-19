#pragma once

#include <crucible/fixy/_Grant.h>
#include <crucible/fixy/Dim.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::grant::ctrl {

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

// The policy tags below are template arguments to the grants, never
// grants themselves, so they do not inherit the grant marker.
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
// grant. It exists so a binding compiled with exceptions can name the
// family it throws.
template <class ExceptionFamily = any_exception>
struct throws final : grant_base {};

template <rationale Reason>
struct abort final : grant_base {};

// A long jump leaves the intervening scopes without running any
// destructor, so a resource whose release depends on one is lost.
template <rationale Reason>
struct longjmp_unsafe final : grant_base {};

template <class CleanupPolicy>
struct exit final : grant_base {};

// Spelled without the leading underscores of the builtin, which are
// reserved to the implementation.
struct builtin_trap_ok final : grant_base {};
struct unreachable_ok final : grant_base {};

template <class SuspensionPolicy>
struct coroutine final : grant_base {};

}  // namespace crucible::fixy::grant::ctrl

namespace crucible::fixy::grant {

template <class ExceptionFamily>
struct which_dim<ctrl::throws<ExceptionFamily>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <ctrl::rationale Reason>
struct which_dim<ctrl::abort<Reason>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <ctrl::rationale Reason>
struct which_dim<ctrl::longjmp_unsafe<Reason>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <class CleanupPolicy>
struct which_dim<ctrl::exit<CleanupPolicy>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <>
struct which_dim<ctrl::builtin_trap_ok> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {
};

template <>
struct which_dim<ctrl::unreachable_ok> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <class SuspensionPolicy>
struct which_dim<ctrl::coroutine<SuspensionPolicy>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

using accept_default_strict_for_ControlFlow = accept_default_strict_for<dim::DimensionAxis::ControlFlow>;

}  // namespace crucible::fixy::grant

namespace crucible::fixy::grant::detail::ctrl_grant_self_test {

using D = dim::DimensionAxis;

struct sample_exception final {};

static_assert(IsGrantTag<ctrl::throws<>>);
static_assert(IsGrantTag<ctrl::throws<sample_exception>>);
static_assert(IsGrantTag<ctrl::abort<"oom unrecoverable">>);
static_assert(IsGrantTag<ctrl::longjmp_unsafe<"setjmp island">>);
static_assert(IsGrantTag<ctrl::exit<ctrl::at_exit>>);
static_assert(IsGrantTag<ctrl::exit<ctrl::exit_immediate>>);
static_assert(IsGrantTag<ctrl::builtin_trap_ok>);
static_assert(IsGrantTag<ctrl::unreachable_ok>);
static_assert(IsGrantTag<ctrl::coroutine<ctrl::generator>>);

static_assert(!IsGrantTag<ctrl::any_exception>);
static_assert(!IsGrantTag<ctrl::at_exit>);
static_assert(!IsGrantTag<ctrl::co_await_only>);

static_assert(sizeof(ctrl::throws<>) == 1);
static_assert(sizeof(ctrl::abort<"x">) == 1);
static_assert(sizeof(ctrl::longjmp_unsafe<"x">) == 1);
static_assert(sizeof(ctrl::exit<ctrl::at_exit>) == 1);
static_assert(sizeof(ctrl::builtin_trap_ok) == 1);
static_assert(sizeof(ctrl::unreachable_ok) == 1);
static_assert(sizeof(ctrl::coroutine<ctrl::generator>) == 1);

static_assert(which_dim_v<ctrl::throws<>> == D::ControlFlow);
static_assert(which_dim_v<ctrl::throws<sample_exception>> == D::ControlFlow);
static_assert(which_dim_v<ctrl::abort<"oom">> == D::ControlFlow);
static_assert(which_dim_v<ctrl::longjmp_unsafe<"jmp">> == D::ControlFlow);
static_assert(which_dim_v<ctrl::exit<ctrl::at_exit>> == D::ControlFlow);
static_assert(which_dim_v<ctrl::exit<ctrl::no_cleanup>> == D::ControlFlow);
static_assert(which_dim_v<ctrl::exit<ctrl::exit_immediate>> == D::ControlFlow);
static_assert(which_dim_v<ctrl::builtin_trap_ok> == D::ControlFlow);

// fix-36: ControlFlow ships grant tags, so it is not a grantless axis.
static_assert(audit::grant_family_witnessed_v<ctrl::builtin_trap_ok>,
              "grant/Ctrl.h ships a grant family for ControlFlow, so ControlFlow must "
              "not appear in grant::kAxesWithoutNonDefaultGrants.");
static_assert(which_dim_v<ctrl::unreachable_ok> == D::ControlFlow);
static_assert(which_dim_v<ctrl::coroutine<ctrl::co_await_only>> == D::ControlFlow);
static_assert(which_dim_v<ctrl::coroutine<ctrl::generator>> == D::ControlFlow);
static_assert(which_dim_v<ctrl::coroutine<ctrl::async_task>> == D::ControlFlow);
static_assert(which_dim_v<accept_default_strict_for_ControlFlow> == D::ControlFlow);

static_assert(!std::is_same_v<ctrl::throws<>, ctrl::abort<"x">>);
static_assert(!std::is_same_v<ctrl::abort<"x">, ctrl::longjmp_unsafe<"x">>);
static_assert(!std::is_same_v<ctrl::builtin_trap_ok, ctrl::unreachable_ok>);
static_assert(!std::is_same_v<ctrl::exit<ctrl::at_exit>, ctrl::exit<ctrl::no_cleanup>>);
static_assert(!std::is_same_v<ctrl::coroutine<ctrl::generator>, ctrl::coroutine<ctrl::async_task>>);
static_assert(!std::is_same_v<ctrl::throws<>, ctrl::throws<sample_exception>>);

static_assert(!std::is_same_v<ctrl::abort<"reason A">, ctrl::abort<"reason B">>);
static_assert(std::is_same_v<ctrl::abort<"same">, ctrl::abort<"same">>);
static_assert(!std::is_same_v<ctrl::abort<"ab">, ctrl::abort<"abc">>);
static_assert(ctrl::rationale{"oom"}.size() == 4);  // three characters plus the terminator

}  // namespace crucible::fixy::grant::detail::ctrl_grant_self_test
