// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I17 fixture — pins MetaLog::try_append_pure rejects a
// caller that declares Row<Effect::Bg>.
//
// MetaLog::try_append is the FOREGROUND producer-side write
// (per the SPSC protocol — fg writes head, bg reads tail).  A
// caller declaring Row<Bg> is structurally incorrect: bg threads
// are the consumer side of MetaLog, not the producer.  IsPure
// <Row<Bg>> = Subrow<Row<Bg>, Row<>> is false (Bg not in ∅), so
// the requires-clause rejects.
//
// This is the second per-axis rejection leg (companion to the
// Row<IO> fixture).  Together they pin TWO independent atoms
// causing rejection, preventing a regression that accidentally
// widened the constraint to accept either.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Bg>>.

#include <crucible/MetaLog.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::MetaLog log;
    crucible::TensorMeta m{};
    // Caller declares Row<Bg> — bg consumer-side context cannot
    // call the producer-side try_append_pure path.  IsPure<Row<Bg>>
    // is false; the requires-clause rejects.
    (void)log.try_append_pure<eff::Row<eff::Effect::Bg>>(&m, 1);
    return 0;
}
