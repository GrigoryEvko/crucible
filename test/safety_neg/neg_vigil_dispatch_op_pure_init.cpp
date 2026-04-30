// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I19 fixture — Vigil::dispatch_op_pure rejects Row<Effect::Init>.
//
// IsPure<Row<Init>> = false.  Init context is the one-shot startup
// phase (Vigil construction, Cipher tier wiring, KernelCache prime);
// the per-op dispatch path is reachable only AFTER Init completes,
// so an Init-tagged caller invoking dispatch_op_pure indicates a
// cross-tier wiring bug.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Init>>.

#include <crucible/Vigil.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<eff::Row<eff::Effect::Init>>(
        crucible::vouch(e), &m, 1);
    return 0;
}
