// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::dispatch_op_pure asks for the context of the Vigil's producer
// claim.  A background helper holds the background load context, whose row
// admits Bg, Alloc, IO and Block, so its call is refused.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta meta{};
    const ::fixy::BgLoadCtx ctx{::foundation::effects::testing::bg()};
    (void)vigil.dispatch_op_pure(ctx, crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(),
                                 &meta, 1);
    return 0;
}
