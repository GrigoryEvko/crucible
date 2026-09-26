// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The startup context owns IO but not Block: initialization may touch the
// kernel and may not park.  The competence probe reads procfs and sysfs,
// and a read can park, so CtxFitsHostProbe refuses the startup context.
//
// The companion fixture neg_host_probe_hot_fg_ctx.cpp passes a context
// that owns neither IO nor Block, a distinct mismatch class.

#include <crucible/ledger/Competence.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

namespace ledger = crucible::ledger;

int main() {
    const ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    const ledger::CompetenceReport report = ledger::probe_competence(init);
    (void)report;
    return 0;
}
