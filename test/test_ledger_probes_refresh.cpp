// The policy of the background refresh of the hardware-capability ledger:
// whether an unfit host goes unprobed, whether sibling verdicts share one
// measurement, when the wait backs off, and whether the daemon publishes a
// view that a reader can use.  test_ledger_probes tests the contract that
// every probe family shares.
//
// Each group states the claim and then breaks it. A test that only ever
// sees the passing case cannot tell a working gate from an absent one.

#include <crucible/ledger/ProbeSupport.h>
#include <crucible/ledger/RefreshDaemon.h>

#include "test_assert.h"
#include "test_ledger_probes_fixtures.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include <unistd.h>

using namespace crucible;
using crucible::ledger::CompetenceReport;
using crucible::ledger::CycleResult;
using crucible::ledger::LedgerError;
using crucible::ledger::VerdictEvidence;
using crucible::ledger::VerdictId;
using ledger_probe_fixtures::fit_host;
using ledger_probe_fixtures::probe_ctx;
using ledger_probe_fixtures::unfit_host;

namespace {

int g_stub_probe_calls = 0;

[[nodiscard]] std::expected<ledger::VerdictMeasurement, LedgerError> stub_probe(ledger::LedgerIoCtx const&,
                                                                                CompetenceReport const&) noexcept {
    ++g_stub_probe_calls;
    VerdictEvidence evidence{};
    evidence.quantiles = cog::LatencyQuantiles{20u, 24u, 40u};
    evidence.sample_count = 100000;
    evidence.within_run_cv_ppm = 12000;
    evidence.run_to_run_spread_ppm = 4000;
    return ledger::VerdictMeasurement{.value = ledger::VerdictValue{20u}, .evidence = evidence};
}

constexpr ledger::ProbeRegistration kStubRegistry[] = {
    {.id = VerdictId::TimerFloorNanos, .run = &stub_probe},
};
constexpr VerdictId kStubWanted[] = {VerdictId::TimerFloorNanos};

void test_an_unfit_host_is_never_probed() {
    // The claim: an unfit host is a reason to skip, not a reason to
    // measure and grade low. Measuring costs the host load it cannot
    // afford, in exchange for a number the default reader refuses anyway.
    ledger::Ledger ledger{};
    g_stub_probe_calls = 0;
    const ledger::CycleReport skipped =
        ledger::refresh_when_fit(probe_ctx, ledger, kStubWanted, kStubRegistry, unfit_host(), 10000u);
    assert(skipped.result == CycleResult::SkippedUnfitHost);
    assert(skipped.queued_count == 1u);
    assert(g_stub_probe_calls == 0);
    assert(ledger.entries.empty());

    // Unbreak it: the very same call on a fit host does run the probe and
    // does admit the result, so the gate is the competence and not an
    // unconditional refusal.
    g_stub_probe_calls = 0;
    const ledger::CycleReport ran =
        ledger::refresh_when_fit(probe_ctx, ledger, kStubWanted, kStubRegistry, fit_host(), 10000u);
    assert(ran.result == CycleResult::Admitted);
    assert(g_stub_probe_calls == 1);
    assert(ledger.entries.size() == 1u);
    assert(ledger.entries.front().confidence == ledger::Confidence::High);

    // And a second cycle on the same fresh ledger has nothing to do, so a
    // served verdict is not re-measured every period.
    g_stub_probe_calls = 0;
    const ledger::CycleReport idle =
        ledger::refresh_when_fit(probe_ctx, ledger, kStubWanted, kStubRegistry, fit_host(), 10001u);
    assert(idle.result == CycleResult::NothingToDo);
    assert(g_stub_probe_calls == 0);

    std::printf("  test_an_unfit_host_is_never_probed:        PASSED\n");
}

// A measurement shared by three sibling verdicts, in the shape the real
// probes use: one memo, three probe functions that all read it.
ledger::MeasurementMemo<int> g_sibling_memo{};
int g_sibling_measure_count = 0;

[[nodiscard]] int shared_sibling_measurement() noexcept {
    return g_sibling_memo.get_or_measure([] { return ++g_sibling_measure_count; });
}

[[nodiscard]] std::expected<ledger::VerdictMeasurement, LedgerError> sibling_probe(ledger::LedgerIoCtx const&,
                                                                                   CompetenceReport const&) noexcept {
    const int shared = shared_sibling_measurement();
    VerdictEvidence evidence{};
    evidence.quantiles = cog::LatencyQuantiles{20u, 24u, 40u};
    evidence.sample_count = 100000;
    evidence.within_run_cv_ppm = 12000;
    evidence.run_to_run_spread_ppm = 4000;
    return ledger::VerdictMeasurement{.value = ledger::VerdictValue{static_cast<std::uint64_t>(shared)},
                                      .evidence = evidence};
}

constexpr ledger::ProbeRegistration kSiblingRegistry[] = {
    {.id = VerdictId::TimerFloorNanos, .run = &sibling_probe},
    {.id = VerdictId::VectorWidthPreferredBits, .run = &sibling_probe},
    {.id = VerdictId::VectorWidthComputeGainPercent, .run = &sibling_probe},
};
constexpr VerdictId kSiblingWanted[] = {
    VerdictId::TimerFloorNanos,
    VerdictId::VectorWidthPreferredBits,
    VerdictId::VectorWidthComputeGainPercent,
};

void test_sibling_verdicts_share_one_measurement() {
    // The claim, and the regression it pins.
    //
    // Two of the three real probes answer several verdicts from one
    // measurement. The memo is what shares it, and the memo has a
    // lifetime. That lifetime was five seconds first, and the hugepage
    // measurement takes about twenty, so each sibling expired the memo and
    // measured again — under the load the previous measurement had just
    // created. The two hugepage fault verdicts came out at 40.3 and 43.7
    // microseconds per mebibyte in one run of crucible-hwprobe, describing
    // a machine that had not changed.
    //
    // What must hold is that one refresh cycle takes one measurement,
    // whatever the siblings are. That is asserted by the count, and the
    // values agreeing is the same fact seen from the other side.
    ledger::Ledger ledger{};
    g_sibling_memo.forget();
    g_sibling_measure_count = 0;

    const ledger::CycleReport report =
        ledger::refresh_when_fit(probe_ctx, ledger, kSiblingWanted, kSiblingRegistry, fit_host(), 10000u);
    assert(report.result == CycleResult::Admitted);
    assert(report.outcome.measured_count == 3u);
    assert(report.outcome.admitted_count == 3u);
    assert(g_sibling_measure_count == 1);

    assert(ledger.entries.size() == 3u);
    for (ledger::LedgerEntry const& entry : ledger.entries) {
        assert(entry.value.value().raw() == 1u);
    }

    // Break it: a memo that expires between siblings measures once per
    // sibling and the three verdicts stop agreeing. Forcing the expiry by
    // hand is what the five-second lifetime did by itself.
    ledger::Ledger second{};
    g_sibling_memo.forget();
    g_sibling_measure_count = 0;
    for (const VerdictId id : kSiblingWanted) {
        g_sibling_memo.forget();  // stands in for the lifetime running out
        const VerdictId one[] = {id};
        (void)ledger::refresh_when_fit(probe_ctx, second, one, kSiblingRegistry, fit_host(), 10000u);
    }
    assert(g_sibling_measure_count == 3);
    assert(second.entries.size() == 3u);
    assert(second.entries[0].value.value().raw() != second.entries[2].value.value().raw());

    // And the bound that keeps the fix from overshooting: a memo may not
    // outlive the shortest time to live in the ledger, or a cycle could
    // serve a verdict the ledger has already called stale.
    static_assert(ledger::kMemoLifetimeSeconds < ledger::kFragmentationTtlSeconds);

    std::printf("  test_sibling_verdicts_share_one_measurement: PASSED\n");
}

void test_backoff_grows_only_when_nothing_was_admitted() {
    const ledger::RefreshSchedule schedule{
        .idle_period_seconds = 3600,
        .initial_backoff_seconds = 60,
        .max_backoff_seconds = 480,
        .backoff_multiplier = 2,
    };
    assert(schedule.is_well_formed());
    assert(schedule.next_backoff(0) == 60u);
    assert(schedule.next_backoff(60) == 120u);
    assert(schedule.next_backoff(480) == 480u);

    // Break it: a schedule that does not back off is not well formed, and
    // the mint's precondition is what refuses it.
    assert(!ledger::RefreshSchedule{.backoff_multiplier = 1}.is_well_formed());

    // Only an outcome that got somewhere resets the wait. A skipped cycle
    // on an unfit host must back off, or an unfit host is looked at every
    // idle period forever.
    assert(ledger::outcome_resets_backoff(CycleResult::Admitted));
    assert(ledger::outcome_resets_backoff(CycleResult::NothingToDo));
    assert(!ledger::outcome_resets_backoff(CycleResult::SkippedUnfitHost));
    assert(!ledger::outcome_resets_backoff(CycleResult::AllRefused));
    assert(!ledger::outcome_resets_backoff(CycleResult::CommitFailed));

    std::printf("  test_backoff_grows_only_when_nothing_was_admitted: PASSED\n");
}

void test_daemon_publishes_a_view_a_reader_can_use() {
    constexpr ledger::LedgerIoCtx ctx{::foundation::effects::testing::bg()};
    ledger::RefreshDaemonConfig config{};
    config.wanted = kStubWanted;
    config.registry = kStubRegistry;
    config.probe_settings = ledger::ProbeSettings{.sample_count = 64, .pin_core = -1};

    auto daemon = ledger::mint_refresh_daemon(ctx, config);
    assert(daemon != nullptr);

    // A reader before any cycle gets a view, not a null pointer. Every
    // lookup on it misses, so every consumer takes its conservative path.
    const std::shared_ptr<const ledger::LedgerView> before = daemon->current_view();
    assert(before != nullptr);
    const auto miss = before->lookup(VerdictId::ParallelKneeBytes);
    assert(!miss.is_known());
    assert(miss.value_or_conservative(ledger::VerdictValue{4242u}) == ledger::VerdictValue{4242u});

    // One cycle on the calling thread, which is the same code the loop
    // runs, so the test exercises the real thing.
    g_stub_probe_calls = 0;
    const ledger::CycleReport report = daemon->run_one_cycle();
    assert(daemon->completed_cycle_count() == 1u);
    assert(daemon->last_result() == report.result);

    // What the outcome is depends on whether the machine running the test
    // is fit to measure, and both answers are correct. What must hold
    // either way is that a published view is always readable and that a
    // skipped cycle ran no probe.
    if (report.result == CycleResult::SkippedUnfitHost) {
        assert(g_stub_probe_calls == 0);
    } else {
        assert(report.result == CycleResult::Admitted || report.result == CycleResult::CommitFailed
               || report.result == CycleResult::NothingToDo);
    }
    const std::shared_ptr<const ledger::LedgerView> after = daemon->current_view();
    assert(after != nullptr);

    std::printf("  test_daemon_publishes_a_view_a_reader_can_use: PASSED (%.*s)\n",
                static_cast<int>(ledger::cycle_result_name(report.result).size()),
                ledger::cycle_result_name(report.result).data());
}

void test_daemon_thread_starts_and_stops() {
    constexpr ledger::LedgerIoCtx ctx{::foundation::effects::testing::bg()};
    ledger::RefreshDaemonConfig config{};
    config.wanted = kStubWanted;
    config.registry = kStubRegistry;
    // A long idle period on purpose: stop() must not wait it out, and a
    // test that passed only because the period was short would prove
    // nothing about the wake-up.
    config.schedule.idle_period_seconds = 6u * 3600u;
    config.probe_settings = ledger::ProbeSettings{.sample_count = 64, .pin_core = -1};

    auto daemon = ledger::mint_refresh_daemon(ctx, config);
    daemon->start();
    // The first cycle happens before the first sleep, so waiting for one
    // completed cycle needs no timer.
    while (daemon->completed_cycle_count() == 0u) {
        CRUCIBLE_SPIN_PAUSE;
    }
    daemon->stop();
    assert(daemon->completed_cycle_count() >= 1u);
    // Idempotent: stopping twice, and destroying after a stop, must not
    // join a thread that is already gone.
    daemon->stop();

    std::printf("  test_daemon_thread_starts_and_stops:       PASSED\n");
}

}  // namespace

int main() {
    // The daemon commits to the store, and the store's root comes from the
    // environment. Without this the test would write a stub verdict into
    // whatever real ledger the machine is using and the next process to
    // read it would be served a fabricated twenty nanoseconds. An
    // environment variable rather than a parameter, so the path sanitizer
    // the discovery runs through is still exercised.
    //
    // A run that aborts runs no destructor and leaves its directory.  The
    // planted directory stands for one: no process can have the id
    // 999999999, because it is above the largest process id of Linux.  The
    // last six characters come from the id of this process, so two runs at
    // the same time plant two different directories.
    std::error_code error;
    std::string planted = std::to_string(1000000 + ::getpid() % 1000000);
    planted = "/tmp/crucible-probe-test-999999999-" + planted.substr(1);
    (void)std::filesystem::create_directory(planted, error);
    assert(std::filesystem::is_directory(planted, error));
    std::string directory;
    {
        const ledger_probe_fixtures::ScopedCacheHome cache_home;
        directory = cache_home.path();
        assert(!std::filesystem::exists(planted, error));

        std::printf("test_ledger_probes_refresh:\n");
        test_an_unfit_host_is_never_probed();
        test_sibling_verdicts_share_one_measurement();
        test_backoff_grows_only_when_nothing_was_admitted();
        test_daemon_publishes_a_view_a_reader_can_use();
        test_daemon_thread_starts_and_stops();
    }

    // The cache root is gone when the guard ends, so a run leaves no
    // directory behind.
    assert(!std::filesystem::exists(directory, error));
    std::printf("test_ledger_probes_refresh: 5 groups, all passed\n");
    return 0;
}
