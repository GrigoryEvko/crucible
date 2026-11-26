// The compile-time checks of foundation/reflect/Members.h.

#include <foundation/reflect/Members.h>

namespace foundation::reflect {

namespace detail::members_checks {

struct Named {
    int count = 0;
    int other = 0;
};

// The name query compares the whole identifier, and an entity with no
// identifier, such as the global namespace, has no name.
static_assert(has_identifier_named(^^Named, "Named"));
static_assert(!has_identifier_named(^^Named, "Name") && !has_identifier_named(^^Named, "NamedMore"));
static_assert(!has_identifier_named(^^::, ""));

// The walk of a scope gives the first member of the name, or a null
// reflection when no member has it.
static_assert(member_named_in(^^Named, std::meta::access_context::unprivileged(), "other") == ^^Named::other);
static_assert(member_named_in(^^Named, std::meta::access_context::unprivileged(), "absent") == std::meta::info{});

// A local class of a function of the base is a class of the base too.
consteval std::meta::info local_class_of_the_base() {
    struct Local {};
    return ^^Local;
}

}  // namespace detail::members_checks

}  // namespace foundation::reflect

namespace {

struct Outside {};

}  // namespace

namespace foundation::reflect::detail::members_checks {

// An alias in the base that names a class outside the base gives that class.
using OutsideAlias = Outside;

// The test of the base reads the outermost namespace of the class: the
// namespace foundation, through a nested namespace and a local class, but not
// an unnamed namespace or an alias of the base that names a class outside it.
static_assert(is_class_of_the_base_(^^Named) && is_class_of_the_base_(local_class_of_the_base()));
static_assert(!is_class_of_the_base_(^^Outside) && !is_class_of_the_base_(^^OutsideAlias));

}  // namespace foundation::reflect::detail::members_checks
