// The cache-tier and NUMA probes of crucible-hwprobe.  Refer to
// crucible_hwprobe_probes.h for why each probe family compiles alone.

#include "crucible_hwprobe_probes.h"

#include <crucible/ledger/probes/CacheTier.h>

namespace crucible::hwprobe {

ProbeOutcome probe_parallel_knee_bytes(ledger::LedgerIoCtx const& ctx,
                                       ledger::CompetenceReport const& competence) noexcept {
    return ledger::probes::probe_parallel_knee_bytes(ctx, competence);
}

ProbeOutcome probe_parallel_ceiling_bytes(ledger::LedgerIoCtx const& ctx,
                                          ledger::CompetenceReport const& competence) noexcept {
    return ledger::probes::probe_parallel_ceiling_bytes(ctx, competence);
}

ProbeOutcome probe_numa_remote_cost(ledger::LedgerIoCtx const& ctx,
                                    ledger::CompetenceReport const& competence) noexcept {
    return ledger::probes::probe_numa_remote_cost(ctx, competence);
}

}  // namespace crucible::hwprobe
