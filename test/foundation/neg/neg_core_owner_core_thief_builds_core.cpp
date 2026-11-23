// A class outside the base derives from the core of a Box, as the fixture
// neg_core_owner_core_thief_names_base_owner does, and builds an object.  An
// object would let the class write the state of its core, the address of a
// Box, and its end would then give back storage that no Box owns.  The
// constructors and the end of the core are private, and its one friend is
// the Box, so the class builds no object.  The positive control is
// SecondCheckOwner of the check file of foundation/core/Owner.h, an Owner of
// the base whose core builds.

#include <fixy/Core.h>

#include <foundation/core/Owner.h>

namespace crucible::probe {

// The state of the core of an owner, by deduction from its public base.  The
// fixture never links, so the functions have no body.
template <class State, class Owner>
State state_of(::foundation::core::OwnerCore<State, Owner> const& core) noexcept;

::fixy::Box<int> const& some_box() noexcept;

using BoxState = decltype(state_of(some_box()));

// The constructor of the class builds its core, which only the Box may build.
// The private end of the core refuses the end of such an object too.
struct Thief final : ::foundation::core::OwnerCore<BoxState, ::fixy::Box<int>> {
    Thief() noexcept {}
};

}  // namespace crucible::probe

int main() { return static_cast<int>(sizeof(crucible::probe::Thief)); }
