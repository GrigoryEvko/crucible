// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::dispatch_op_pure asks for the context of the Vigil's producer
// claim.  A test fixture holds the test runner context, so its call is
// refused.  A test that drives the foreground takes the context from
// Vigil::mint_producer_context instead.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta meta{};
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    (void)vigil.dispatch_op_pure(ctx, crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(),
                                 &meta, 1);
    return 0;
}
