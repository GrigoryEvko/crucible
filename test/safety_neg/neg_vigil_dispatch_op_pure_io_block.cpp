// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::dispatch_op_pure requires a pure caller row.  This caller names
// Row<IO, Block>, the row that Cipher::record_event requires, so the gate
// refuses it.  A row that one entry point needs is refused at another.
// With the empty row in its place, the call compiles.

#include <crucible/Vigil.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<eff::Row<eff::Effect::IO, eff::Effect::Block>>(
        crucible::mint_ffi_entry(e).retag<::fixy::tags::vessel_trust::Validated>(), &m, 1);
    return 0;
}
