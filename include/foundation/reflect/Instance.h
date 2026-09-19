#pragma once

// One reflection query answers "is T a specialization of this class
// template" for every wrapper, in place of a primary-plus-partial-
// specialization detector pair written once per wrapper.  It answers
// for a template with a non-type parameter as readily as for one with
// type parameters only, which the `template <class...> class` form
// cannot.
//
// The strip is what closes the slip-through: `Wrapper<int> const&`
// answers the same as `Wrapper<int>`.  The dealias is what lets an
// alias such as `Linear<int>` answer for the class template it names,
// and what keeps the strip itself, an alias specialization, from being
// mistaken for the type it strips.

#include <meta>
#include <type_traits>

namespace foundation::reflect {

// True when T, stripped of cv and reference, is a specialization of the
// class template that Template reflects, written ^^Name.
template <class T, std::meta::info Template>
inline constexpr bool is_instance_of_v =
    std::meta::has_template_arguments(std::meta::dealias(^^std::remove_cvref_t<T>))
    && std::meta::template_of(std::meta::dealias(^^std::remove_cvref_t<T>)) == Template;

namespace detail::instance_self_test {

template <class T>
struct TypeParam {};

template <auto N, class T>
struct MixedParam {};

template <class T>
using TypeAlias = MixedParam<1, T>;

struct Plain {};

static_assert(is_instance_of_v<TypeParam<int>, ^^TypeParam>);
static_assert(is_instance_of_v<TypeParam<int> const&, ^^TypeParam>);
static_assert(is_instance_of_v<TypeParam<int>&&, ^^TypeParam>);
static_assert(is_instance_of_v<MixedParam<3, int>, ^^MixedParam>);
static_assert(is_instance_of_v<TypeAlias<int>, ^^MixedParam>);
static_assert(!is_instance_of_v<TypeParam<int>, ^^MixedParam>);
static_assert(!is_instance_of_v<Plain, ^^TypeParam>);
static_assert(!is_instance_of_v<int, ^^TypeParam>);
static_assert(!is_instance_of_v<void, ^^TypeParam>);

}  // namespace detail::instance_self_test

}  // namespace foundation::reflect
