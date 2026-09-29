// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Pins that TraceRing::try_append_pure rejects a
// caller declaring Row<Effect::IO>.  Sibling of MetaLog's
// try_append_pure neg-compile matrix.
//
// IsPure<R> = Subrow<R, Row<>>.  Row<IO> contains the IO atom,
// so {IO} ⊄ ∅ — the requires-clause rejects.  An IO-context
// caller (filesystem code, network code, syscall path) cannot
// silently invoke try_append_pure on the hot path.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::IO>>.

#include <crucible/TraceRing.h>
#include <fixy/Aliases.h>

namespace eff = ::fixy;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    (void)ring.try_append_pure<eff::Row<eff::Effect::IO>>(e);
    return 0;
}
