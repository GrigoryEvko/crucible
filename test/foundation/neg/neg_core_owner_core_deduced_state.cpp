// The second route to the key of the run of an owner: a class template
// outside the base derives from the OwnerCore of the state of a Box, which it
// takes by deduction from the public base of the Box (the probe of the
// audit).  A gate on the namespace of the state alone does not close this
// route.  The core takes its Owner as a second argument, so the core of one
// argument is no class, and the deduction of the probe does not compile.
// The fixtures neg_core_owner_core_thief_* refuse each form with two
// arguments.

#include <foundation/core/Owner.h>

namespace crucible::probe {

// The state of the core of one argument, by deduction from its public base.
// The fixture never links, so the function has no body.
template <class State>
State state_of(::foundation::core::OwnerCore<State> const& core) noexcept;

}  // namespace crucible::probe

int main() { return 0; }
