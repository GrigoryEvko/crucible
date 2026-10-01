#pragma once

// The probe table of crucible-hwprobe, one translation unit for each probe
// family.
//
// Each probe of include/crucible/ledger/probes/ is an inline function in a
// header, and one call instantiates its whole measurement: the bench harness
// and the scratch regions.  So each family compiles in a translation unit of
// its own, which keeps each compile job short, and each function below is
// the one door from main to that unit.  Each one only calls the inline probe
// of the same name, so the table of the tool and the table of the refresh
// daemon run the same code.

#include <crucible/ledger/Ledger.h>

#include <expected>

namespace crucible::hwprobe {

using ProbeOutcome = std::expected<ledger::VerdictMeasurement, ledger::LedgerError>;

// The floor of the timing rig itself.  crucible_hwprobe_timer_floor.cpp.
[[nodiscard]] ProbeOutcome probe_timer_floor(ledger::LedgerIoCtx const& ctx,
                                             ledger::CompetenceReport const& competence) noexcept;

// ledger/probes/VectorWidth.h.  crucible_hwprobe_vector_width.cpp.
[[nodiscard]] ProbeOutcome probe_vector_width_preferred_bits(ledger::LedgerIoCtx const& ctx,
                                                             ledger::CompetenceReport const& competence) noexcept;
[[nodiscard]] ProbeOutcome probe_vector_width_compute_gain(ledger::LedgerIoCtx const& ctx,
                                                           ledger::CompetenceReport const& competence) noexcept;
[[nodiscard]] ProbeOutcome probe_vector_width_memory_gain(ledger::LedgerIoCtx const& ctx,
                                                          ledger::CompetenceReport const& competence) noexcept;

// ledger/probes/CacheTier.h.  crucible_hwprobe_cache_tier.cpp.
[[nodiscard]] ProbeOutcome probe_parallel_knee_bytes(ledger::LedgerIoCtx const& ctx,
                                                     ledger::CompetenceReport const& competence) noexcept;
[[nodiscard]] ProbeOutcome probe_parallel_ceiling_bytes(ledger::LedgerIoCtx const& ctx,
                                                        ledger::CompetenceReport const& competence) noexcept;
[[nodiscard]] ProbeOutcome probe_numa_remote_cost(ledger::LedgerIoCtx const& ctx,
                                                  ledger::CompetenceReport const& competence) noexcept;

// ledger/probes/HugePage.h.  crucible_hwprobe_huge_page.cpp.
[[nodiscard]] ProbeOutcome probe_thp_fault_cost(ledger::LedgerIoCtx const& ctx,
                                                ledger::CompetenceReport const& competence) noexcept;
[[nodiscard]] ProbeOutcome probe_thp_fault_gain(ledger::LedgerIoCtx const& ctx,
                                                ledger::CompetenceReport const& competence) noexcept;
[[nodiscard]] ProbeOutcome probe_thp_access_gain(ledger::LedgerIoCtx const& ctx,
                                                 ledger::CompetenceReport const& competence) noexcept;

}  // namespace crucible::hwprobe
