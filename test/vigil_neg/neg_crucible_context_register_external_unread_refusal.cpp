// Negative-compile test.  This file must fail to compile.
//
// register_external refuses a pooled slot and an id outside the plan of the
// active region.  A caller that reads no result would take a refused slot as
// bound, so the call with no read of its result does not compile.

#include <crucible/CrucibleContext.h>

#include <foundation/effects/Effect.h>

int main() {
    crucible::CrucibleContext context{::foundation::effects::testing::test().alloc};
    auto context_key = crucible::mint_context_key(::foundation::effects::testing::test());
    const crucible::TensorData data{::fixy::mint_foreign_run_address(&context).expect("a context has an address"),
                                    crucible::kHostDevice};
    auto given = context.with_compiled(context_key, [data](crucible::CompiledScope& scope) noexcept {
        scope.register_external(crucible::SlotId{1}, data);
    });
    static_cast<void>(given);
    return 0;
}
