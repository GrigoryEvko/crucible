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
//
// The query is a concept, and the value spelling is derived from it.
// A concept cannot be specialized, and the expression inside this one
// names only reflection functions: the strip is std::meta::remove_cvref,
// the function, and not the type trait, because a trait is a class
// template and a class template can be specialized.  So nothing a
// translation unit writes can change the concept's answer for a type.
// A variable template can be explicitly specialized, which is why every
// gate reads the concept, or a per-wrapper concept built on it, and the
// value spelling is read by diagnostics alone.

#include <meta>

namespace foundation::reflect {

// True when T, stripped of cv and reference, is a specialization of the
// class template that Template reflects, written ^^Name.
template <class T, std::meta::info Template>
concept IsInstanceOf = std::meta::has_template_arguments(std::meta::dealias(std::meta::remove_cvref(^^T)))
                    && std::meta::template_of(std::meta::dealias(std::meta::remove_cvref(^^T))) == Template;

// The same answer as a value, for the places that read a bool.  Derived
// from the concept, and read by nothing that admits: an explicit
// specialization of this variable reaches no gate.
template <class T, std::meta::info Template>
inline constexpr bool is_instance_of_v = IsInstanceOf<T, Template>;

// True when T is a specialization of ANY of the templates named.  A
// recogniser for a family of wrappers is then the family written once,
// in the concept's own body, instead of one arm per member in a table
// that a port can carry with its arms removed.
template <class T, std::meta::info... Templates>
concept IsInstanceOfAny = (IsInstanceOf<T, Templates> || ...);

template <class T, std::meta::info... Templates>
inline constexpr bool is_instance_of_any_v = IsInstanceOfAny<T, Templates...>;

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

// The concept and the value agree on every shape above.
static_assert(IsInstanceOf<TypeParam<int>, ^^TypeParam>);
static_assert(IsInstanceOf<TypeParam<int> const&, ^^TypeParam>);
static_assert(IsInstanceOf<TypeAlias<int>, ^^MixedParam>);
static_assert(!IsInstanceOf<Plain, ^^TypeParam>);
static_assert(!IsInstanceOf<void, ^^TypeParam>);

// A class derived from a specialization is not that specialization.
struct DerivedFromParam : TypeParam<int> {};
static_assert(!IsInstanceOf<DerivedFromParam, ^^TypeParam>);

// The family form answers for each member and for nothing else, and an
// empty family answers false rather than admitting.
static_assert(IsInstanceOfAny<TypeParam<int>, ^^TypeParam, ^^MixedParam>);
static_assert(IsInstanceOfAny<MixedParam<3, int>, ^^TypeParam, ^^MixedParam>);
static_assert(IsInstanceOfAny<TypeAlias<int> const&, ^^TypeParam, ^^MixedParam>);
static_assert(!IsInstanceOfAny<Plain, ^^TypeParam, ^^MixedParam>);
static_assert(!IsInstanceOfAny<int, ^^TypeParam>);
static_assert(!IsInstanceOfAny<TypeParam<int>>);
static_assert(is_instance_of_any_v<TypeParam<int>, ^^TypeParam, ^^MixedParam>);

}  // namespace detail::instance_self_test

}  // namespace foundation::reflect
