// Negative-compile test.  This file must fail to compile.
//
// register_external reads the flag of the slot in the plan of the active
// region.  The door that reads no plan takes an ExternalSlotId, which only the
// list of the context holds, and no SlotId converts to one.  The door is also
// private to the context.  So no code outside the context writes a slot with
// no check.

#include <crucible/CrucibleContext.h>

#include <foundation/effects/Effect.h>

int main() {
    crucible::CrucibleContext context{::foundation::effects::testing::test().alloc};
    const crucible::TensorData data{::fixy::mint_foreign_run_address(&context).expect("a context has an address"),
                                    crucible::kHostDevice};
    context.register_external_(crucible::SlotId{1}, data);
    return 0;
}
