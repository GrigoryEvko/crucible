#pragma once

// Reflection over a function's parameter list and return type.
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

template <auto FnPtr>
struct signature_traits {
    using function_type = std::remove_pointer_t<decltype(FnPtr)>;

    static constexpr auto function_reflection = ^^function_type;

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

    // The query reads the exception specification of the function type,
    // so a variadic function type answers as a named one does.
    static constexpr bool is_noexcept = std::meta::is_noexcept(function_reflection);
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

}  // namespace foundation::reflect
