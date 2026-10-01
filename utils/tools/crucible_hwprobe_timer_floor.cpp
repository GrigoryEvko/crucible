// The timer-floor probe of crucible-hwprobe.
//
// It measures the floor of the timing rig itself: the cost of touching a
// register and reading the cycle counter around it. That number is real, is
// stable, and decides nothing. It proves that the loop closes — measure,
// judge, store, read back, serve.
//
// Timing goes through bench/bench_harness.h rather than a rig written here.
// The harness already reports the quantiles, the within-run coefficient of
// variation and the first-half-versus-second-half drift that the evidence
// record wants, and it already knows how to pin, warm up and cap wall time.
// A second rig would be a second set of bugs.

#include "crucible_hwprobe_probes.h"

#include <crucible/ledger/ProbeSupport.h>

#include "bench_harness.h"

namespace crucible::hwprobe {

// Two runs, not one. The within-run coefficient of variation says how
// steady the samples were inside a single burst; it says nothing about
// whether the burst itself was representative. A part that throttles
// between runs, a scheduler that places the second run on a colder cache,
// an operator who changes the governor mid-flight — all of those show up as
// run-to-run spread and none of them show up within a run. The evidence
// record has a field for each because they fail differently, and filling
// the second one from the first would be a lie the reader cannot detect.
ProbeOutcome probe_timer_floor(ledger::LedgerIoCtx const&, ledger::CompetenceReport const&) noexcept {
    int sink = 0;

    auto one_run = [&](const char* name) {
        auto run = bench::Run{name};
        auto& configured = run.samples(ledger::probe_settings().sample_count).warmup(2000).max_wall_ms(4000);
        const int core = ledger::probe_settings().pin_core;
        if (core >= 0) {
            (void)configured.core(core);
        } else {
            (void)configured.no_pin();
        }
        return run.measure([&] {
            sink += 1;
            bench::do_not_optimize(sink);
        });
    };

    const bench::Report first = one_run("ledger.timer_floor.run1");
    const bench::Report second = one_run("ledger.timer_floor.run2");

    if (first.pct.n == 0u || second.pct.n == 0u) {
        return std::unexpected(ledger::LedgerError::ConfidenceBelowBar);
    }

    // The two-run fold lives in ProbeSupport.h, because the probes of
    // ledger/probes/ use it too. This probe reads the shared one rather than keeping its
    // own copy, so a change to how evidence is built cannot apply to three
    // probes and miss the fourth.
    const ledger::VerdictEvidence evidence = ledger::evidence_from_two_runs(first, second);

    return ledger::VerdictMeasurement{.value = ledger::VerdictValue{evidence.quantiles.p50_ns}, .evidence = evidence};
}

}  // namespace crucible::hwprobe
