// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — saturation-row rejection.  Pins
// TraceRing::try_append_pure rejects a caller declaring the full
// 6-atom OsUniverse row (AllRow = Row<Alloc, IO, Block, Bg, Init,
// Test>).
//
// AllRow is the TOP of the F* effect lattice — a context that
// admits every observable effect.  IsPure<AllRow> = Subrow<AllRow,
// Row<>> = false (the top is NOT a subset of the bottom).  This
// is the maximum-cardinality rejection witness.
//
// Together with the per-axis fixtures (alloc/io/block/bg/init/test)
// and the multi-atom fixture (io_block), this fixture pins that the
// constraint scales to the saturation-row case (a refactor that
// special-cased "below 6 atoms" would slip).
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<AllRow>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    // AllRow contains every Effect atom — the F* lattice top.
    (void)ring.try_append_pure<eff::AllRow>(e);
    return 0;
}
