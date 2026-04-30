// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — pins TraceRing::try_append_pure rejects a
// caller declaring Row<Effect::Block>.  Sibling of MetaLog's
// neg-compile matrix.
//
// IsPure<R> = Subrow<R, Row<>>.  Row<Block> contains the Block
// atom, so {Block} ⊄ ∅ — the requires-clause rejects.  A blocking
// caller (futex, condvar, syscall-blocking path) cannot run on
// the hot per-op recording path; the row mismatch catches the
// miscategorization at compile time.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Block>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    (void)ring.try_append_pure<eff::Row<eff::Effect::Block>>(e);
    return 0;
}
