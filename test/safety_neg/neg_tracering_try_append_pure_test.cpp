// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — pins TraceRing::try_append_pure rejects a
// caller declaring Row<Effect::Test>.
//
// IsPure<R> = Subrow<R, Row<>>.  Test is the test-only context
// (poison-byte injection, invariant probes, mock fixtures); a
// production hot-path append must never be in a Test-tagged
// context, and a Test caller should not invoke the production-
// hot-path append.  Compile-time rejection catches cross-context
// leakage between test fixtures and production hot path.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Test>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    (void)ring.try_append_pure<eff::Row<eff::Effect::Test>>(e);
    return 0;
}
