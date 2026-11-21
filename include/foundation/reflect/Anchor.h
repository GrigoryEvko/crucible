// SPDX-License-Identifier: Apache-2.0
//
// A type that depends on a template parameter, for a list or a text that a
// header gives to std::define_static_array or std::define_static_string.
//
// GCC evaluates a call where its template stands when no argument depends on
// a template parameter, by its type or by its value.  Then it instantiates
// the function template that the call names in each translation unit that
// includes the header.  The first instantiation of std::define_static_array
// costs about 68 M instructions in a unit, and of std::define_static_string
// about 19 M.  A query of ^^T depends on T by its value, so its call needs no
// anchor.  A query of a fixed entity and a text builder depend on nothing.
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
