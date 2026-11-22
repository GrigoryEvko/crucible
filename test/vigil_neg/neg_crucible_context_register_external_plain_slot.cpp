// Negative-compile test.  This file must fail to compile.
//
// The address form of register_external takes an ExternalSlotId, which only
// mint_external_slot gives, for a slot that the plan marks external.  A plain
// SlotId does not convert to it, so no call of the address form writes a
// pooled slot.

#include <crucible/CrucibleContext.h>

namespace {
[[maybe_unused]] void register_plain(crucible::CrucibleContext& ctx, crucible::MemoryPlan const& plan,
                                     crucible::TensorData data, crucible::CrucibleContext::CompiledView const& view) {
    static_cast<void>(crucible::mint_external_slot(plan.slots, crucible::SlotId{1}).is_some());
    ctx.register_external(crucible::SlotId{1}, data, view);
}
}  // namespace

int main() { return 0; }
