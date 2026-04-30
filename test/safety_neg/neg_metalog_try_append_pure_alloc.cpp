// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I17-AUDIT fixture — per-axis neg-compile parity.  Pins
// MetaLog::try_append_pure rejects a caller declaring Row<Alloc>.
//
// IsPure<R> = Subrow<R, Row<>>.  Row<Alloc> contains the Alloc
// atom, so {Alloc} ⊄ ∅; the requires-clause rejects.  This is
// the third per-axis rejection leg (companion to the IO and Bg
// fixtures shipped with FOUND-I17).
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Alloc>>.

#include <crucible/MetaLog.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::MetaLog log;
    crucible::TensorMeta m{};
    (void)log.try_append_pure<eff::Row<eff::Effect::Alloc>>(&m, 1);
    return 0;
}
