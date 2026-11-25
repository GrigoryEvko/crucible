// Negative-compile test.  This file must fail to compile.
//
// register_external refuses a pooled slot and an id outside the plan of the
// active region.  A caller that reads no result would take a refused slot as
// bound, so the call with no read of its result does not compile.

#include <crucible/CrucibleContext.h>

#include <foundation/effects/Effect.h>

int main() {
    crucible::CrucibleContext context{::foundation::effects::testing::test().alloc};
    auto view = context.mint_compiled_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    const crucible::TensorData data{::fixy::mint_foreign_run_address(&context).expect("a context has an address"),
                                    crucible::kHostDevice};
    context.register_external(crucible::SlotId{1}, data, view);
    return 0;
}
