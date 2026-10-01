// The compile-time checks of foundation/reflect/Instance.h.

#include <foundation/reflect/Instance.h>

namespace foundation::reflect {

namespace detail::instance_self_test {

template <class T>
struct TypeParam {};

template <auto N, class T>
struct MixedParam {};

template <class T>
using TypeAlias = MixedParam<1, T>;

struct Plain {};

static_assert(IsInstanceOf<TypeParam<int>, ^^TypeParam>);
static_assert(IsInstanceOf<TypeParam<int> const&, ^^TypeParam>);
static_assert(IsInstanceOf<TypeParam<int>&&, ^^TypeParam>);
static_assert(IsInstanceOf<MixedParam<3, int>, ^^MixedParam>);
static_assert(IsInstanceOf<TypeAlias<int>, ^^MixedParam>);
static_assert(!IsInstanceOf<TypeParam<int>, ^^MixedParam>);
static_assert(!IsInstanceOf<Plain, ^^TypeParam>);
static_assert(!IsInstanceOf<int, ^^TypeParam>);
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

// A member is found in the class and in a public base, and not when it is
// private or when the type is no class.
struct StatesAValue {
    static constexpr int stated = 1;
};
struct InheritsAValue : StatesAValue {};
class HidesAValue {
    static constexpr int stated = 2;

public:
    static constexpr int shown = stated;
};
static_assert(member_named(^^StatesAValue, "stated") == ^^StatesAValue::stated);
static_assert(member_named(^^InheritsAValue const&, "stated") == ^^StatesAValue::stated);
static_assert(member_named(^^HidesAValue, "stated") == std::meta::info{});
static_assert(member_named(^^HidesAValue, "shown") == ^^HidesAValue::shown);
static_assert(member_named(^^int, "stated") == std::meta::info{});
static_assert(member_named(^^Plain, "stated") == std::meta::info{});

}  // namespace detail::instance_self_test

}  // namespace foundation::reflect
