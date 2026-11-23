// A class outside the base derives from the core of a Box: the state of the
// Box by deduction, and the Box as the Owner.  The gate on the Owner passes,
// because the Box is a class of the base.  The class takes the key of a run
// through a static member, with no object of its own.  run_key_() is a
// private member of the core, and its one friend is the Box, so the class is
// refused.  The positive control is CheckOwner of the check file of
// foundation/core/Owner.h, which takes the key of its own run.

#include <fixy/Core.h>

#include <foundation/core/Owner.h>

namespace crucible::probe {

// The state of the core of an owner, by deduction from its public base.  The
// fixture never links, so the functions have no body.
template <class State, class Owner>
State state_of(::foundation::core::OwnerCore<State, Owner> const& core) noexcept;

::fixy::Box<int> const& some_box() noexcept;

using BoxState = decltype(state_of(some_box()));

struct Thief final : ::foundation::core::OwnerCore<BoxState, ::fixy::Box<int>> {
    [[nodiscard]] static ::fixy::View<int> forge_over(int& first, ::fixy::ElementCount count) noexcept {
        return ::fixy::View<int>{Thief::run_key_(), &first, count.raw()};
    }
};

}  // namespace crucible::probe

int main() {
    int one = 1;
    return static_cast<int>(crucible::probe::Thief::forge_over(one, ::fixy::ElementCount{64}).size());
}
