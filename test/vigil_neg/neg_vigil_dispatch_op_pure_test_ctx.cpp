// Negative-compile test.  This file must fail to compile.
//
// Vigil::dispatch_op_pure asks for the context of the Vigil's producer
// claim.  A test fixture holds the test runner context, so its call is
// refused.  A test that drives the foreground takes the context from
// Vigil::mint_producer_context instead.  The op is well formed, so the
// context is the only fault.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry entry{};
    const crucible::ValidatedOp op =
        crucible::mint_validated_op(crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>())
            .expect("an entry with no tensors takes no records");
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    (void)vigil.dispatch_op_pure(ctx, op);
    return 0;
}
