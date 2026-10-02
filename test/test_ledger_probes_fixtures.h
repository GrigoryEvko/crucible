// The fixtures that the probe tests of the hardware-capability ledger share:
// a host that is fit to measure, a host that is not, and the context under
// which a probe maps and pins.

#pragma once

#include <crucible/ledger/Ledger.h>

#include <fixy/Ctx.h>

namespace ledger_probe_fixtures {

[[nodiscard]] inline crucible::ledger::CompetenceReport fit_host() noexcept {
    crucible::ledger::CompetenceReport report{};
    report.isolated_core_count = 8;
    report.online_sibling_count = 0;
    report.load_average_milli = 1000;
    report.allowed_cpu_count = 384;
    report.machine_cpu_count = 384;
    report.perf_event_paranoid = 2;
    report.scaling_min_freq_khz = 4510205;
    report.scaling_max_freq_khz = 4510205;
    report.governor_is_performance = false;
    report.defects = crucible::ledger::derive_defects(report, true);
    return report;
}

[[nodiscard]] inline crucible::ledger::CompetenceReport unfit_host() noexcept {
    crucible::ledger::CompetenceReport report = fit_host();
    report.online_sibling_count = 4;  // an isolated core shares its pipeline
    report.defects = crucible::ledger::derive_defects(report, true);
    return report;
}

// A probe maps and pins under the context of the store.
inline constexpr crucible::ledger::LedgerIoCtx probe_ctx{::foundation::effects::testing::bg()};

}  // namespace ledger_probe_fixtures
