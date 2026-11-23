// A class outside the base derives from OwnerCore with the state of a Box,
// which it takes by deduction from the public base of the Box, and names
// itself as the Owner of the core.  A gate on the namespace of the state
// passes this class, because the state is a state of the base.  The core asks
// that its Owner is a class of the namespace foundation or fixy
// (OwnerOfTheBase), so the class is refused.  The positive control is the
// deduction of the core of a Box in test_core_owner_probe, which compiles.

#include <fixy/Core.h>

#include <foundation/core/Owner.h>

namespace crucible::probe {

// The state of the core of an owner, by deduction from its public base.  The
// fixture never links, so the functions have no body.
template <class State, class Owner>
State state_of(::foundation::core::OwnerCore<State, Owner> const& core) noexcept;

::fixy::Box<int> const& some_box() noexcept;

using BoxState = decltype(state_of(some_box()));

struct Thief final : ::foundation::core::OwnerCore<BoxState, Thief> {};

}  // namespace crucible::probe

int main() { return static_cast<int>(sizeof(crucible::probe::Thief)); }
