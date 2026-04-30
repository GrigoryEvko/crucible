// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — multi-atom rejection witness.  Pins
// TraceRing::try_append_pure rejects a caller declaring Row<IO,
// Block>.  This is the EXACT row Cipher::record_event REQUIRES
// (per FOUND-I09), so it pins a non-trivial composition: a row
// that's CORRECT for one row-typed entry point (Cipher::record_event)
// is REJECTED at another (TraceRing::try_append_pure).  Without
// this fixture, a regression that accidentally widened IsPure to
// "anything that matches the record_event row" would not be
// caught — the per-axis fixtures all use single atoms.
//
// IsPure<Row<IO, Block>> = Subrow<Row<IO, Block>, Row<>> = false.
// The requires-clause rejects.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::IO, Effect::Block>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    (void)ring.try_append_pure<
        eff::Row<eff::Effect::IO, eff::Effect::Block>>(e);
    return 0;
}
