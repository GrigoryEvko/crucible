// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — pins TraceRing::drain_pure rejects a
// caller declaring Row<Effect::Bg>.
//
// IsPure<Row<Bg>> = false.  The bg thread that *owns* the consumer
// side is itself in a Bg context, but each drain_pure() call is a
// pure memory-only operation and the wrapper enforces this.  A
// caller that has accumulated Bg context (e.g., from earlier in
// the bg loop body) must drop it before calling drain_pure.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Bg>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry out[1];
    (void)ring.drain_pure<eff::Row<eff::Effect::Bg>>(out, 1);
    return 0;
}
