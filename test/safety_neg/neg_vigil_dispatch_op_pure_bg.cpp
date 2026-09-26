// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::dispatch_op_pure requires a pure caller row.  This caller names
// Row<Bg>, the row of the background thread, so the gate refuses it.  With
// the empty row in its place, the call compiles.

#include <crucible/Vigil.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<eff::Row<eff::Effect::Bg>>(
        crucible::mint_ffi_entry(e).retag<::fixy::tags::vessel_trust::Validated>(), &m, 1);
    return 0;
}
