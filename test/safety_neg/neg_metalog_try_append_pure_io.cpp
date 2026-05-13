// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I17 fixture — pins MetaLog::try_append_pure rejects a
// caller that declares Row<Effect::IO>.
//
// try_append_pure carries `requires IsPure<CallerRow>`.  IsPure<R>
// = Subrow<R, Row<>>, i.e., R must be empty.  Row<IO> contains
// the IO atom, so {IO} ⊄ ∅ — the constraint fails at substitution
// time and the call site is rejected.  The bug class caught: a
// caller in an IO context (filesystem code, network code, etc.)
// inadvertently invokes try_append_pure on the hot path; the
// row mismatch prevents the silent miscategorization at compile
// time rather than letting it slip into runtime.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::IO>>.

#include <crucible/MetaLog.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::MetaLog log;
    crucible::TensorMeta m{};
    // Caller declares Row<IO> — IsPure<Row<IO>> is false (Row<IO>
    // is not a subrow of Row<>); the requires-clause rejects.
    (void)log.try_append_pure<eff::Row<eff::Effect::IO>>(&m, 1);
    return 0;
}
