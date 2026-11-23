// Negative-compile test.  This file must fail to compile.
//
// mint_external_slot is the one door of an ExternalSlotId: it reads the flag
// of the slot in the plan.  The constructor from a SlotId is private, so no
// code outside the mint makes an ExternalSlotId of a pooled slot.

#include <crucible/CrucibleContext.h>

namespace {
[[maybe_unused]] bool forge(crucible::MemoryPlan const& plan) {
    const bool is_minted = crucible::mint_external_slot(plan.slots.view(), crucible::SlotId{1}).is_some();
    const crucible::ExternalSlotId forged{crucible::SlotId{1}};
    return is_minted && forged.slot() == crucible::SlotId{1};
}
}  // namespace

int main() { return 0; }
