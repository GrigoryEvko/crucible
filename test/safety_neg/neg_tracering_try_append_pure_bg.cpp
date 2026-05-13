// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — pins TraceRing::try_append_pure rejects a
// caller declaring Row<Effect::Bg>.
//
// IsPure<R> = Subrow<R, Row<>>.  Row<Bg> contains the Bg atom
// (background-thread context); the foreground per-op recording
// path must NEVER be invoked from a Bg context — that would mean
// the bg consumer thread is somehow producing entries, breaking
// the SPSC discipline.  Compile-time rejection prevents the
// regression.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Bg>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    (void)ring.try_append_pure<eff::Row<eff::Effect::Bg>>(e);
    return 0;
}
