// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I17-AUDIT fixture — per-axis neg-compile parity.  Pins
// MetaLog::try_append_pure rejects a caller declaring Row<Test>.
//
// IsPure<R> = Subrow<R, Row<>>.  Row<Test> contains the Test
// atom, so {Test} ⊄ ∅; the requires-clause rejects.  Test is
// reserved for test-only call sites that may exercise paths
// production code never touches (e.g., poison-byte injection,
// invariant probes); a production hot-path append should never
// run in a Test-tagged context, and a Test caller should not
// invoke the production-hot-path append.
//
// Together with the alloc/block/init fixtures (and the previously-
// shipped io/bg fixtures), this completes the per-axis rejection
// matrix: every atom in the OsUniverse independently triggers
// IsPure failure.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Test>>.

#include <crucible/MetaLog.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::MetaLog log;
    crucible::TensorMeta m{};
    (void)log.try_append_pure<eff::Row<eff::Effect::Test>>(&m, 1);
    return 0;
}
