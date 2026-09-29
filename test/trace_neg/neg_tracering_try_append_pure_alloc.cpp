// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Pins that TraceRing::try_append_pure rejects a
// caller declaring Row<Effect::Alloc>.  Sibling of MetaLog's
// try_append_pure neg-compile matrix.
//
// IsPure<R> = Subrow<R, Row<>>.  Row<Alloc> contains the Alloc
// atom, so {Alloc} ⊄ ∅ — the requires-clause rejects.  Caller
// in an allocating context (anywhere on the foreground recording
// path that does heap allocation) is rejected at compile time.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Alloc>>.

#include <crucible/TraceRing.h>
#include <fixy/Aliases.h>

namespace eff = ::fixy;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    (void)ring.try_append_pure<eff::Row<eff::Effect::Alloc>>(e);
    return 0;
}
