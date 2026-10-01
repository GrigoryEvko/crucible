// The vector-width probes of crucible-hwprobe.  Refer to
// crucible_hwprobe_probes.h for why each probe family compiles alone.

#include "crucible_hwprobe_probes.h"

#include <crucible/ledger/probes/VectorWidth.h>

namespace crucible::hwprobe {

ProbeOutcome probe_vector_width_preferred_bits(ledger::LedgerIoCtx const& ctx,
                                               ledger::CompetenceReport const& competence) noexcept {
    return ledger::probes::probe_vector_width_preferred_bits(ctx, competence);
}

ProbeOutcome probe_vector_width_compute_gain(ledger::LedgerIoCtx const& ctx,
                                             ledger::CompetenceReport const& competence) noexcept {
    return ledger::probes::probe_vector_width_compute_gain(ctx, competence);
}

ProbeOutcome probe_vector_width_memory_gain(ledger::LedgerIoCtx const& ctx,
                                            ledger::CompetenceReport const& competence) noexcept {
    return ledger::probes::probe_vector_width_memory_gain(ctx, competence);
}

}  // namespace crucible::hwprobe
