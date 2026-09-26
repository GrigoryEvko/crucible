// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The host probe reads procfs and sysfs, and the open and the read can
// park the caller on the kernel.  CtxFitsHostProbe admits a context only
// when it owns IO and Block.  The hot foreground context owns neither, so
// no hot path can take the probe.
//
// The companion fixture neg_host_probe_init_ctx_cannot_block.cpp passes a
// context that owns IO and lacks only Block, a distinct mismatch class.

#include <crucible/ledger/Competence.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace ledger = crucible::ledger;

int main() {
    const ::fixy::HotFgCtx foreground = ::foundation::effects::testing::foreground();
    const ledger::HostFacts facts = ledger::probe_host_facts(foreground);
    (void)facts;
    return 0;
}
