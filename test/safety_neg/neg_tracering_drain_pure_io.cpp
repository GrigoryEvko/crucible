// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — pins TraceRing::drain_pure rejects a
// caller declaring Row<Effect::IO>.
//
// drain_pure carries `requires IsPure<CallerRow>`.  IsPure<Row<IO>>
// is false; the requires-clause rejects.  An IO-context bg
// consumer (e.g., a path that fseek()s a checkpoint between
// drain calls) cannot silently invoke drain_pure.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::IO>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry out[1];
    (void)ring.drain_pure<eff::Row<eff::Effect::IO>>(out, 1);
    return 0;
}
