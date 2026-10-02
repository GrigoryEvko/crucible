// SPDX-License-Identifier: Apache-2.0
//
// A type that depends on a template parameter, for a list or a text that a
// header gives to std::define_static_array or std::define_static_string.
//
// GCC resolves a call where its template stands when no argument has a type
// that depends on a template parameter.  At that point it instantiates the
// function template that the call names, in each translation unit that
// includes the header, also in a unit that never instantiates the enclosing
// template.  The first instantiation of std::define_static_array costs about
// 68 M instructions in a unit, because GCC deduces its return type.  The
// first instantiation of std::define_static_string costs about 19 M.  A reflection
// query returns a std::vector<std::meta::info>, and a text builder holds a
// std::string, so their types never depend on a template parameter.
//
// anchored_t<Owner, Value> is Value.  Its spelling depends on the reflection
// Owner, so a list or a text of this type makes the call depend on Owner.
// Then only a translation unit that instantiates the enclosing template
// instantiates the function.  Owner is the reflection of what the value
// belongs to: ^^T for a template type parameter T, or a template parameter
// of the type std::meta::info.
//
// A function that is not a template gets the parameter
// `template <class Anchor = void>`, and its text gets the type
// anchored_t<^^Anchor, std::string>.  Do this only for a function whose
// result no check reads, because a translation unit can specialize a
// function template.  A call from a function that is not a template
// instantiates the template where that function stands, so each caller must
// be a template too.
//
// An alias template has no specialization, and std::conditional_t reads no
// specialization of std::conditional.  So the anchor adds no point where a
// translation unit can change a value that a check reads.

#pragma once

#include <meta>
#include <type_traits>

namespace foundation::reflect {

template <std::meta::info Owner, class Value>
using anchored_t = std::conditional_t<Owner == ^^void, Value, Value>;

}  // namespace foundation::reflect
