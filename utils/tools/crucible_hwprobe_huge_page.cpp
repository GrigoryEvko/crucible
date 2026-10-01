// The transparent-hugepage probes of crucible-hwprobe.  Refer to
// crucible_hwprobe_probes.h for why each probe family compiles alone.

#include "crucible_hwprobe_probes.h"

#include <crucible/ledger/probes/HugePage.h>

namespace crucible::hwprobe {

ProbeOutcome probe_thp_fault_cost(ledger::LedgerIoCtx const& ctx, ledger::CompetenceReport const& competence) noexcept {
    return ledger::probes::probe_thp_fault_cost(ctx, competence);
}

ProbeOutcome probe_thp_fault_gain(ledger::LedgerIoCtx const& ctx, ledger::CompetenceReport const& competence) noexcept {
    return ledger::probes::probe_thp_fault_gain(ctx, competence);
}

ProbeOutcome probe_thp_access_gain(ledger::LedgerIoCtx const& ctx,
                                   ledger::CompetenceReport const& competence) noexcept {
    return ledger::probes::probe_thp_access_gain(ctx, competence);
}

}  // namespace crucible::hwprobe
