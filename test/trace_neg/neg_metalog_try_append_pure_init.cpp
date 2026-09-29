// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Per-axis neg-compile parity.  Pins that
// MetaLog::try_append_pure rejects a caller declaring Row<Init>.
//
// IsPure<R> = Subrow<R, Row<>>.  Row<Init> contains the Init
// atom, so {Init} ⊄ ∅; the requires-clause rejects.  Init is
// reserved for one-shot startup-time code (Crucible bootstrap,
// Vessel adapter init); the hot-path try_append is structurally
// incompatible with the Init context-tag because by the time
// try_append is called the runtime is already in steady state.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Init>>.

#include <crucible/MetaLog.h>
#include <fixy/Aliases.h>

namespace eff = ::fixy;

int main() {
    crucible::MetaLog log;
    crucible::TensorMeta m{};
    (void)log.try_append_pure<eff::Row<eff::Effect::Init>>(&m, 1);
    return 0;
}
