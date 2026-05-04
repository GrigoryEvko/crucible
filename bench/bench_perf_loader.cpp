// Worked example demonstrating the GAPS-004 BPF observability surface
// through the `crucible::perf::Senses` aggregator (#1285).
//
// What this bench measures, end-to-end:
//
//   (A) ONE-SHOT load cost: `Senses::load_all()` and `load_subset()`.  Both
//       are libbpf-bound; expected wall-clock is ~50-200 ms total for 5
//       programs (verifier + attach + mmap).  Printed as a banner — NOT
//       a bench-iteration body.  Putting libbpf load into a `.measure(...)`
//       loop would be a textbook misuse: each iteration would re-attach and
//       blow past the 1-second total budget on the first call.
//
//   (B) Per-call accessor cost: `s.sense_hub()`, `s.sched_switch()`, etc.
//       Each is a single load from the optional's bit (held in cache),
//       sub-ns in steady state — the cost a production caller pays per
//       facade access.  Demonstrates that the aggregator adds zero overhead
//       on the read path vs hand-holding 5 std::optional<Facade>.
//
//   (C) Coverage diagnostics: `s.coverage()` plus `attached_count()`.  Used
//       by Augur for drift attribution ("can't read CPU stalls — PmuSample
//       is missing") and by bench banners.  Both sub-ns; we bench them
//       separately because attached_count() does 5 conditional adds and
//       could in principle be slower than the bare struct read.
//
//   (D) The "real" hot read: `SenseHub::read()` over 96 counters = 768 B
//       = 12 cache lines.  Target ~50 ns on warm L1; this is the cost a
//       caller pays per snapshot.  Skipped (with a banner explaining how
//       to fix it) if sense_hub didn't attach — typical when the binary
//       lacks CAP_BPF + CAP_PERFMON (run `make bench-caps` to grant them
//       once, see bench/CMakeLists.txt).
//
//   (E) Snapshot diff: `Snapshot::operator-()` runs 96 sub_sat — exercised
//       at every Augur sample (paired snapshot before/after a workload).
//       Target ~100 ns; a reasonable upper bound on the steady-state cost
//       of windowed counter deltas.
//
// Reading the output:
//   • [batch-avg] flag → auto-batched percentiles are over batch means.
//     Sub-5-ns ops can't be timed individually; the harness amortizes
//     rdtsc cost over a batch and reports per-op means.
//   • Variance > 5 % → throttling or migration; results invalid (CLAUDE.md
//     §VIII "Measurement discipline").
//   • Cold-start tail in `max` → first call paged in libbpf's text + the
//       BPF programs' .rodata; matches CLAUDE.md's "max from cold-start
//       cache miss" note in §VIII.
//
// What this bench does NOT measure:
//   • End-to-end "from event to userspace counter" latency — that's
//     dominated by tracepoint dispatch and BPF program execution, not
//     by the userspace facade.
//   • Per-facade .read_recent() / timeline-ring drain costs — those have
//     their own bench targets where the BTF-typed event shape matters.
//
// To run with capabilities (recommended):
//   cmake --build --preset bench --target bench-caps
//   ./build-bench/bench/bench_perf_loader
//
// To run as root (alternative):
//   sudo ./build-bench/bench/bench_perf_loader

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <crucible/effects/Capabilities.h>
#include <crucible/perf/Senses.h>
#include <crucible/perf/SenseHub.h>

#include "bench_harness.h"

namespace {

using steady = std::chrono::steady_clock;

// Wall-clock millisecond delta — used to time one-shot libbpf loads
// without a bench loop.  load() takes 50-200 ms; bench loops would
// never finish.  We measure once with steady_clock + format as a banner.
[[nodiscard]] long long elapsed_ms(steady::time_point t0,
                                   steady::time_point t1) noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

void print_coverage(const crucible::perf::CoverageReport& cov) {
    std::printf("  attached: %zu/5", cov.attached_count());
    if (cov.attached_count() == 0) {
        // Coverage 0/5 is the most common surprise.  Tell the user how to
        // fix it instead of leaving them to grep.  Two recovery paths:
        //   • make bench-caps  (one-time, sticky until rebuild)
        //   • sudo ./bench_perf_loader  (per-invocation)
        std::printf(" — run `cmake --build --preset bench --target bench-caps`"
                    " or invoke under sudo");
    }
    std::printf("\n  sense_hub=%d sched_switch=%d pmu_sample=%d "
                "lock_contention=%d syscall_latency=%d\n",
                cov.sense_hub_attached, cov.sched_switch_attached,
                cov.pmu_sample_attached, cov.lock_contention_attached,
                cov.syscall_latency_attached);
}

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();
    const bool json = bench::env_json();

    std::printf("=== senses ===\n");

    // ── (A) ONE-SHOT loads ─────────────────────────────────────────────
    //
    // Senses::load_all loads every available subprogram independently;
    // partial loads are tolerated and recorded in coverage().  We time
    // THE load we actually keep — measuring a temporary load() and then
    // doing it again would (a) double the libbpf attach cost (~130 ms on
    // this hardware) and (b) double the partial-attach noise on stderr.
    const auto t0 = steady::now();
    auto s = crucible::perf::Senses::load_all(crucible::effects::Init{});
    const auto t1 = steady::now();
    const auto cov = s.coverage();
    std::printf("load_all: %lld ms\n", elapsed_ms(t0, t1));
    print_coverage(cov);

    // load_subset cost-discipline path: callers that want predictable
    // overhead pick a low-rate pair (sense_hub @ ~0.5 % + pmu_sample @
    // ~0.05 %) and skip the timeline rings.  The result is discarded
    // immediately — we're showing the load cost shape, not retaining
    // a second handle on top of `s` above.
    const auto t2 = steady::now();
    {
        auto sub = crucible::perf::Senses::load_subset(
            crucible::effects::Init{},
            crucible::perf::SensesMask{
                .sense_hub  = true,
                .pmu_sample = true,
            });
        bench::do_not_optimize(sub);
    }
    const auto t3 = steady::now();
    std::printf("load_subset(sense_hub|pmu_sample): %lld ms\n",
                elapsed_ms(t2, t3));

    // ── (B), (C), (D), (E) — per-call benches ──────────────────────────
    //
    // Each report is a single bench::run() against a closure that touches
    // exactly one Senses surface.  Bodies guard the .read() / diff calls
    // on `if (const auto* h = ...)` so the bench is meaningful even when
    // sense_hub didn't attach: in that case the read body costs only one
    // load + one predicted-not-taken branch (~1-2 ns), which is itself a
    // useful measurement of "the safety check is ~free".

    // Pre-read a baseline snapshot OUTSIDE the bench body — operator- is
    // what we're benching, not the read.  A null-attached sense_hub leaves
    // baseline default-constructed (zeroed); the diff body skips on null
    // anyway so this is moot in that branch.
    crucible::perf::Snapshot baseline{};
    if (const auto* h = s.sense_hub()) {
        baseline = h->read();
    }

    bench::Report reports[] = {
        // (B) Per-facade accessor — sub-ns single optional-bit load.
        [&]{
            return bench::run("senses.sense_hub()", [&]{
                const auto* p = s.sense_hub();
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            return bench::run("senses.sched_switch()", [&]{
                const auto* p = s.sched_switch();
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            return bench::run("senses.pmu_sample()", [&]{
                const auto* p = s.pmu_sample();
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            return bench::run("senses.lock_contention()", [&]{
                const auto* p = s.lock_contention();
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            return bench::run("senses.syscall_latency()", [&]{
                const auto* p = s.syscall_latency();
                bench::do_not_optimize(p);
            });
        }(),
        // (C) Coverage diagnostic — 5 bools by value (≤ 4 B).  Augur reads
        // this once per drift-detection sample.
        [&]{
            return bench::run("senses.coverage()", [&]{
                const auto c = s.coverage();
                bench::do_not_optimize(c);
            });
        }(),
        [&]{
            return bench::run("coverage.attached_count() [5 cond. adds]", [&]{
                const std::size_t n = s.coverage().attached_count();
                bench::do_not_optimize(n);
            });
        }(),
        // (D) The actual hot read — 12 cache lines mmap'd from BPF.  Body
        // guards on s.sense_hub() so a null-attached sense_hub measures
        // the bare safety branch (~1-2 ns) instead of segfaulting.
        [&]{
            return bench::run("sense_hub->read() [12 cache lines]", [&]{
                if (const auto* h = s.sense_hub()) {
                    const auto snap = h->read();
                    bench::do_not_optimize(snap);
                } else {
                    bench::do_not_optimize(h);
                }
            });
        }(),
        // (E) Snapshot diff — 96 × sub_sat over a 768 B baseline.  Two
        // snapshots are diffed at every Augur sample to get windowed
        // counters; this report is the steady-state cost of that diff.
        [&]{
            return bench::run("snapshot - snapshot [96 sub_sat]", [&]{
                if (const auto* h = s.sense_hub()) {
                    const auto current = h->read();
                    const auto delta   = current - baseline;
                    bench::do_not_optimize(delta);
                } else {
                    bench::do_not_optimize(h);
                }
            });
        }(),
    };

    bench::emit_reports_text(reports);

    // Diagnostic banner for the read-skipped case so a user staring at
    // "1.5 ns" for sense_hub->read() knows it didn't actually attach.
    if (!cov.sense_hub_attached) {
        std::printf("\n[note] sense_hub not attached; the last two reports "
                    "measure only the safety branch (~1-2 ns), NOT the\n"
                    "       12-cache-line read.  Grant CAP_BPF+CAP_PERFMON "
                    "to see the real read cost.\n");
    }

    bench::emit_reports_json(reports, json);
    return 0;
}
