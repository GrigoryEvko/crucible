// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I17-AUDIT fixture — multi-atom rejection witness.  Pins
// MetaLog::try_append_pure rejects a caller declaring Row<IO,
// Block>.  This is the EXACT row Cipher::record_event REQUIRES,
// so it pins a non-trivial composition: a row that's CORRECT for
// one row-typed entry point (Cipher::record_event) is REJECTED
// at another (MetaLog::try_append_pure).  Without this fixture,
// a regression that accidentally widened IsPure to "anything that
// matches the record_event row" would not be caught — both the
// per-axis fixtures use single atoms.
//
// IsPure<Row<IO, Block>> = Subrow<Row<IO, Block>, Row<>> = false.
// The requires-clause rejects.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::IO, Effect::Block>>.

#include <crucible/MetaLog.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::MetaLog log;
    crucible::TensorMeta m{};
    (void)log.try_append_pure<
        eff::Row<eff::Effect::IO, eff::Effect::Block>>(&m, 1);
    return 0;
}
