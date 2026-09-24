#pragma once

// Reflection over a function's parameter list and return type.
//
// Old spelling: include/crucible/safety/_SignatureTraits.h, namespace
// crucible::safety::extract.  That namespace was not a location: 57
// headers reopened it, each adding the aliases for the thing it
// examined, so "extract" named a convention rather than a home.  These
// queries examine a function signature through reflection, which is
// what this directory is for, so they land here under their own names.
//
// Two shapes are out of scope.  A pointer to a non-static member
// function has a different invocation shape, with an implicit object
// parameter, and needs its own trait family.  A variadic ellipsis is
// not reachable: the reflection query returns only the named
// parameters.

#include <foundation/Platform.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::reflect {

// The standard library shipped here exposes no reflection query for
// noexcept-ness, so this falls back to partial specialization on the
// function type.  Strip the pointer at the call site.

namespace detail {

template <typename F>
struct is_noexcept_function : std::false_type {};

// Two specializations cover the whole matrix.  A free function type is
// either noexcept or it is not, and neither reference nor cv
// qualification applies to one.

template <typename R, typename... Args>
struct is_noexcept_function<R(Args...) noexcept> : std::true_type {};

template <typename R, typename... Args>
struct is_noexcept_function<R(Args...)> : std::false_type {};

template <typename F>
inline constexpr bool is_noexcept_function_v = is_noexcept_function<F>::value;

}  // namespace detail

template <auto FnPtr>
struct signature_traits {
    static constexpr auto function_reflection = ^^std::remove_pointer_t<decltype(FnPtr)>;

    // The query returns a vector, whose allocation is not a constant
    // expression in the context an expansion statement needs.  Landing it
    // in static storage first is what makes the elements spliceable.
    static constexpr auto params = std::define_static_array(std::meta::parameters_of(function_reflection));

    static constexpr std::size_t arity = params.size();

    // Reflecting a function type rather than a declaration yields
    // parameter reflections that are already type reflections.  They have
    // no source-level declaration, so asking for their type throws, and
    // the element has to be spliced directly.
    //
    // The bound constraint turns an out-of-range index into a clean error
    // instead of a substitution failure deep inside the array.
    template <std::size_t I>
        requires(I < arity)
    using param_type_t = typename[:params[I]:];

    using return_type = typename[:std::meta::return_type_of(function_reflection):];

    using function_type = typename[:^^std::remove_pointer_t<decltype(FnPtr)>:];

    static constexpr bool is_noexcept = detail::is_noexcept_function_v<function_type>;
};

template <auto FnPtr, std::size_t I>
using param_type_t = typename signature_traits<FnPtr>::template param_type_t<I>;

template <auto FnPtr>
using return_type_t = typename signature_traits<FnPtr>::return_type;

template <auto FnPtr>
using function_type_t = typename signature_traits<FnPtr>::function_type;

template <auto FnPtr>
inline constexpr std::size_t arity_v = signature_traits<FnPtr>::arity;

template <auto FnPtr>
inline constexpr bool is_noexcept_v = signature_traits<FnPtr>::is_noexcept;

namespace detail::signature_self_test {

inline void witness_nullary() noexcept {}
inline int witness_int_returning() noexcept { return 0; }

inline void witness_unary_int(int) noexcept {}
inline void witness_unary_int_ref(int&) noexcept {}
inline void witness_unary_int_rref(int&&) noexcept {}
inline void witness_unary_int_cref(int const&) noexcept {}
inline void witness_unary_int_ptr(int*) noexcept {}

inline void witness_binary(int, double) noexcept {}
inline void witness_ternary(int, double, char) noexcept {}

inline auto witness_returning_double(int) noexcept -> double { return 0.0; }

struct UserType {
    int v = 0;
};
inline void witness_user_cref(UserType const&) noexcept {}

static_assert(signature_traits<&witness_nullary>::arity == 0);
static_assert(signature_traits<&witness_int_returning>::arity == 0);
static_assert(signature_traits<&witness_unary_int>::arity == 1);
static_assert(signature_traits<&witness_binary>::arity == 2);
static_assert(signature_traits<&witness_ternary>::arity == 3);

static_assert(arity_v<&witness_nullary> == 0);
static_assert(arity_v<&witness_unary_int> == 1);
static_assert(arity_v<&witness_binary> == 2);

static_assert(std::is_same_v<param_type_t<&witness_unary_int, 0>, int>);

static_assert(std::is_same_v<param_type_t<&witness_unary_int_ref, 0>, int&>);

static_assert(std::is_same_v<param_type_t<&witness_unary_int_rref, 0>, int&&>);

static_assert(std::is_same_v<param_type_t<&witness_unary_int_cref, 0>, int const&>);

static_assert(std::is_same_v<param_type_t<&witness_unary_int_ptr, 0>, int*>);

static_assert(std::is_same_v<param_type_t<&witness_binary, 0>, int>);
static_assert(std::is_same_v<param_type_t<&witness_binary, 1>, double>);

static_assert(std::is_same_v<param_type_t<&witness_ternary, 0>, int>);
static_assert(std::is_same_v<param_type_t<&witness_ternary, 1>, double>);
static_assert(std::is_same_v<param_type_t<&witness_ternary, 2>, char>);

static_assert(std::is_same_v<param_type_t<&witness_user_cref, 0>, UserType const&>);

static_assert(std::is_same_v<return_type_t<&witness_nullary>, void>);

static_assert(std::is_same_v<return_type_t<&witness_int_returning>, int>);

static_assert(std::is_same_v<return_type_t<&witness_returning_double>, double>);

inline void witness_alpha(int) noexcept {}
inline void witness_beta(int) noexcept {}

static_assert(arity_v<&witness_alpha> == arity_v<&witness_beta>);
static_assert(std::is_same_v<param_type_t<&witness_alpha, 0>, param_type_t<&witness_beta, 0>>);

inline void witness_throwing(int) {}
inline void witness_nothrowing(int) noexcept {}

static_assert(is_noexcept_v<&witness_nothrowing>);
static_assert(!is_noexcept_v<&witness_throwing>);

static_assert(signature_traits<&witness_unary_int>::is_noexcept);
static_assert(is_noexcept_v<&witness_unary_int>);
static_assert(is_noexcept_v<&witness_nullary>);

static_assert(std::is_same_v<function_type_t<&witness_unary_int>, void(int) noexcept>);
static_assert(std::is_same_v<function_type_t<&witness_throwing>, void(int)>);
static_assert(std::is_same_v<function_type_t<&witness_nullary>, void() noexcept>);
static_assert(std::is_same_v<function_type_t<&witness_int_returning>, int() noexcept>);

inline void witness_quaternary(int, double, char, float) noexcept {}
inline void witness_quinary(int, double, char, float, long) noexcept {}

static_assert(arity_v<&witness_quaternary> == 4);
static_assert(arity_v<&witness_quinary> == 5);
static_assert(std::is_same_v<param_type_t<&witness_quaternary, 3>, float>);
static_assert(std::is_same_v<param_type_t<&witness_quinary, 4>, long>);

// An array parameter and a function parameter each decay under the
// adjusted-parameter-type rule, so the reflected type is a pointer.

inline void witness_array_decay(int[5]) noexcept {}

static_assert(arity_v<&witness_array_decay> == 1);
static_assert(std::is_same_v<param_type_t<&witness_array_decay, 0>, int*>);

inline void witness_function_decay(int()) noexcept {}

static_assert(arity_v<&witness_function_decay> == 1);
static_assert(std::is_same_v<param_type_t<&witness_function_decay, 0>, int (*)()>);

}  // namespace detail::signature_self_test

}  // namespace foundation::reflect
