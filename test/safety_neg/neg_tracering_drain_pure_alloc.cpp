// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — pins TraceRing::drain_pure rejects a
// caller declaring Row<Effect::Alloc>.
//
// drain_pure carries `requires IsPure<CallerRow>`, identical to
// try_append_pure on the producer side.  This fixture witnesses
// rejection on the consumer side.  The bug class caught: a bg
// thread that performed an arena-allocate before drain_pure
// silently slipping its Alloc grade through the row contract.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Alloc>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry out[1];
    (void)ring.drain_pure<eff::Row<eff::Effect::Alloc>>(out, 1);
    return 0;
}
