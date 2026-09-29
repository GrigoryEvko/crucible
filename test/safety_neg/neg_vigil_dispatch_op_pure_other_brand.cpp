// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::dispatch_op_pure asks for the context of the Vigil's producer
// claim.  The context of another state's claim proves that its holder owns
// that state, not a Vigil, so the call is refused.

#include <crucible/Vigil.h>
#include <foundation/effects/Ctx.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta meta{};
    (void)vigil.dispatch_op_pure(::foundation::effects::testing::foreground<Stranger>(),
                                 crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(), &meta,
                                 1);
    return 0;
}
