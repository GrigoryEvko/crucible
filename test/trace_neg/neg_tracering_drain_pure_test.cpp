// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Pins that TraceRing::drain_pure rejects a
// caller declaring Row<Effect::Test>.
//
// IsPure<Row<Test>> = false.  Test context is reserved for
// poison-byte injection / invariant probes / mock fixtures and
// must not cross into production drain_pure call sites.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Test>>.

#include <crucible/TraceRing.h>
#include <fixy/Aliases.h>

namespace eff = ::fixy;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry out[1];
    (void)ring.drain_pure<eff::Row<eff::Effect::Test>>(out, 1);
    return 0;
}
