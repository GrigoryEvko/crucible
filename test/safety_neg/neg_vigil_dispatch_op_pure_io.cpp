// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I19 fixture — Vigil::dispatch_op_pure rejects Row<Effect::IO>.
//
// IsPure<Row<IO>> = false.  An IO-context caller (filesystem,
// network, syscall path) cannot silently invoke dispatch_op_pure
// on the per-op recording hot path; the row mismatch rejects the
// call before SPSC ring head advances.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::IO>>.

#include <crucible/Vigil.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<eff::Row<eff::Effect::IO>>(
        crucible::vouch(e), &m, 1);
    return 0;
}
