// The failure rules read the failure that a ControlFlow grade throws.  The
// relation was once the class template thrown_failure_.  This file tries to
// hide the throw of ctrl::throws<>, so that a binding that throws passes as a
// binding that cannot fail: it specializes that class template.  The
// relation is a function, and no class template gives it, so the
// specialization does not compile.

#include <fixy/Collision.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
struct fixy::collision::detail::thrown_failure_<fixy::atom::ctrl::throws<>> {
    static constexpr bool fails = false;
    static constexpr bool carries_error = false;
    static constexpr bool error_is_secret = false;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
