// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — pins TraceRing::try_append_pure rejects a
// caller declaring Row<Effect::IO>.  Sibling of MetaLog's
// try_append_pure neg-compile matrix (FOUND-I17-AUDIT).
//
// IsPure<R> = Subrow<R, Row<>>.  Row<IO> contains the IO atom,
// so {IO} ⊄ ∅ — the requires-clause rejects.  An IO-context
// caller (filesystem code, network code, syscall path) cannot
// silently invoke try_append_pure on the hot path.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::IO>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    (void)ring.try_append_pure<eff::Row<eff::Effect::IO>>(e);
    return 0;
}
