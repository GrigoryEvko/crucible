// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::dispatch_op_pure asks for the context of the Vigil's producer
// claim.  The unbranded foreground context has the empty row too, but it
// names no claim, so the call is refused.

#include <crucible/Vigil.h>
#include <foundation/effects/Ctx.h>

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta meta{};
    (void)vigil.dispatch_op_pure(::foundation::effects::testing::foreground(),
                                 crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(), &meta,
                                 1);
    return 0;
}
