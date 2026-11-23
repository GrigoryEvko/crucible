// A class outside the base derives from OwnerCore with a state of its own and
// names itself as the Owner of the core.  The Owner is the one friend of the
// core, so the class would take the key of a run and build a View of any
// count over one int.  The core asks that its Owner is a class of the
// namespace foundation or fixy (OwnerOfTheBase), and the rule base-namespace
// refuses such a class in a file outside the base.  The positive control is
// CheckOwner of the check file of foundation/core/Owner.h, an Owner of the
// base that takes the key of its run.

#include <foundation/core/Owner.h>

namespace crucible::probe {

struct LooseState final {
    using owner_state_mark = ::foundation::core::OwnerStateMark;
    unsigned char unused = 0;
    [[nodiscard]] static LooseState emptied(LooseState const& /*state*/) noexcept { return LooseState{}; }
    static void release(LooseState const& /*state*/) noexcept {}
};

struct Thief final : ::foundation::core::OwnerCore<LooseState, Thief> {};

}  // namespace crucible::probe

int main() { return static_cast<int>(sizeof(crucible::probe::Thief)); }
