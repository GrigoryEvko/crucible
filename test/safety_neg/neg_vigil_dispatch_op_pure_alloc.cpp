// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I19 fixture — pins Vigil::dispatch_op_pure rejects a
// caller declaring Row<Effect::Alloc>.  Sibling of TraceRing's
// I16 matrix and MetaLog's I17 matrix.
//
// dispatch_op_pure carries `requires IsPure<CallerRow>`.  A caller
// in an allocating context (anywhere on the foreground recording
// path that does heap allocation) is rejected at compile time.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Alloc>>.

#include <crucible/Vigil.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry e{};
    crucible::TensorMeta m{};
    (void)vigil.dispatch_op_pure<eff::Row<eff::Effect::Alloc>>(
        crucible::vouch(e), &m, 1);
    return 0;
}
