// The failure rules read the failure that a payload returns.  The relation
// was once the class template returned_failure_.  This file tries to hide
// the failure of an Option, so that a binding that returns one passes as a
// binding that cannot fail: it specializes that class template.  The
// relation is a function, and no class template gives it, so the
// specialization does not compile.

#include <fixy/Collision.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
struct fixy::collision::detail::returned_failure_<foundation::core::Option<int>> {
    static constexpr bool fails = false;
    static constexpr bool carries_error = false;
    static constexpr bool error_is_secret = false;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
