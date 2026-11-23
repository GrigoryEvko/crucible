// The first route to the key of the run of an owner: a class outside the base
// derives from OwnerCore with a state of its own, takes the run key through
// the member run_key_() of the core, and builds a View of 64 elements over
// one int (the probe of the audit).  The core takes its Owner as a second
// argument, so the core of one argument is no class, and the derivation of
// the probe does not compile.  The fixtures neg_core_owner_core_thief_*
// refuse each form with two arguments.

#include <foundation/core/Owner.h>

namespace crucible::probe {

struct LooseState final {
    using owner_state_mark = ::foundation::core::OwnerStateMark;
    unsigned char unused = 0;
    [[nodiscard]] static LooseState emptied(LooseState const& /*state*/) noexcept { return LooseState{}; }
    static void release(LooseState const& /*state*/) noexcept {}
};

struct LooseOwner final : ::foundation::core::OwnerCore<LooseState> {};

}  // namespace crucible::probe

int main() { return static_cast<int>(sizeof(crucible::probe::LooseOwner)); }
