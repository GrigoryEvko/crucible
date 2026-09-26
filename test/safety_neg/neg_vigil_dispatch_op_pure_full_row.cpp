// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::dispatch_op_pure requires a pure caller row.  This caller names
// AllRow, the row of every effect, so the gate refuses it.  With the empty
// row in its place, the call compiles.

#include <crucible/Vigil.h>
#include <fixy/Aliases.h>

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<::fixy::AllRow>(
        crucible::mint_ffi_entry(e).retag<::fixy::tags::vessel_trust::Validated>(), &m, 1);
    return 0;
}
