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

}  // namespace detail::members_checks

}  // namespace foundation::reflect
