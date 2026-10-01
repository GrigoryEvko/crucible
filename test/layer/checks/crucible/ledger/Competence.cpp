// The compile-time checks of crucible/ledger/Competence.h.

#include <crucible/ledger/Competence.h>

namespace crucible::ledger {

static_assert(std::is_trivially_copyable_v<CompetenceReport>);

namespace competence_detail::self_test {

// A synthetic host that passes everything.
inline constexpr CompetenceReport s_ideal{
    .defects = {},
    .isolated_core_count = 8,
    .online_sibling_count = 0,
    .load_average_milli = 1000,
    .allowed_cpu_count = 384,
    .machine_cpu_count = 384,
    .perf_event_paranoid = 2,
    .scaling_min_freq_khz = 4510205,
    .scaling_max_freq_khz = 4510205,
    .governor_is_performance = false,
};
static_assert(derive_defects(s_ideal, true).raw() == 0u,
              "a host with the floor raised to the ceiling is pinned even under powersave");

// The exact configuration that produced meaningless numbers on this box:
// unisolated siblings, the clock at its floor, no binding.
inline constexpr CompetenceReport s_burned{
    .defects = {},
    .isolated_core_count = 0,
    .online_sibling_count = 4,
    .load_average_milli = 118000,
    .allowed_cpu_count = 384,
    .machine_cpu_count = 384,
    .perf_event_paranoid = 2,
    .scaling_min_freq_khz = 1220000,
    .scaling_max_freq_khz = 4510205,
    .governor_is_performance = false,
};
static_assert((derive_defects(s_burned, true).raw() & static_cast<std::uint16_t>(CompetenceDefect::NoIsolatedCores))
              != 0u);
static_assert((derive_defects(s_burned, true).raw() & static_cast<std::uint16_t>(CompetenceDefect::SmtSiblingOnline))
              != 0u);
static_assert((derive_defects(s_burned, true).raw() & static_cast<std::uint16_t>(CompetenceDefect::ClockNotPinned))
              != 0u);

// A fallback topology is never competent, because a cache-knee verdict
// measured against a guessed cache size is a verdict about the guess.
static_assert((derive_defects(s_ideal, false).raw() & static_cast<std::uint16_t>(CompetenceDefect::TopologyNotProbed))
              != 0u);

// Zero allowed CPUs is a failed probe, not an idle machine.
inline constexpr CompetenceReport s_no_cpus = [] {
    CompetenceReport report = s_ideal;
    report.allowed_cpu_count = 0;
    return report;
}();
static_assert((derive_defects(s_no_cpus, true).raw() & static_cast<std::uint16_t>(CompetenceDefect::LoadAverageHigh))
              != 0u);

// A machine reporting no hardware threads is likewise a failed probe, and
// must not divide the budget down to something every load clears.
inline constexpr CompetenceReport s_no_machine_cpus = [] {
    CompetenceReport report = s_ideal;
    report.machine_cpu_count = 0;
    return report;
}();
static_assert((derive_defects(s_no_machine_cpus, true).raw()
               & static_cast<std::uint16_t>(CompetenceDefect::LoadAverageHigh))
              != 0u);

// Regression: a process pinned to eight isolated cores on a 384-thread box
// carrying load 156. The load is real and it is on the other 376 threads.
// Dividing by the allowed set called this 1957% and flagged it; dividing by
// the machine calls it 41% and does not.
inline constexpr CompetenceReport s_pinned_on_busy_box = [] {
    CompetenceReport report = s_ideal;
    report.load_average_milli = 156640;
    report.allowed_cpu_count = 8;
    report.machine_cpu_count = 384;
    return report;
}();
static_assert(derive_defects(s_pinned_on_busy_box, true).raw() == 0u,
              "loadavg is a machine-wide number and must be divided by machine-wide capacity");

// The bar still bites when the machine really is busy.
inline constexpr CompetenceReport s_busy_machine = [] {
    CompetenceReport report = s_ideal;
    report.load_average_milli = 300000;  // 300 on 384 threads = 78%
    return report;
}();
static_assert((derive_defects(s_busy_machine, true).raw()
               & static_cast<std::uint16_t>(CompetenceDefect::LoadAverageHigh))
              != 0u);

// A locked-down counter subsystem is a defect even on an otherwise
// immaculate host.
inline constexpr CompetenceReport s_paranoid = [] {
    CompetenceReport report = s_ideal;
    report.perf_event_paranoid = 3;
    return report;
}();
static_assert(derive_defects(s_paranoid, true).raw()
              == static_cast<std::uint16_t>(CompetenceDefect::PerfEventRestricted));

}  // namespace competence_detail::self_test

}  // namespace crucible::ledger
