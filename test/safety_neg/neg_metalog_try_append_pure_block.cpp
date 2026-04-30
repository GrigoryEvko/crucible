// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I17-AUDIT fixture — per-axis neg-compile parity.  Pins
// MetaLog::try_append_pure rejects a caller declaring Row<Block>.
//
// IsPure<R> = Subrow<R, Row<>>.  Row<Block> contains the Block
// atom, so {Block} ⊄ ∅; the requires-clause rejects.  Block
// (the F* `Div` axis — non-termination / blocking) is critical
// to reject because try_append is a non-blocking primitive: a
// caller in a Block-context invoking the hot-path append path
// would be a category error (the implementation never blocks,
// but the caller's row claims it might).
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Block>>.

#include <crucible/MetaLog.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::MetaLog log;
    crucible::TensorMeta m{};
    (void)log.try_append_pure<eff::Row<eff::Effect::Block>>(&m, 1);
    return 0;
}
