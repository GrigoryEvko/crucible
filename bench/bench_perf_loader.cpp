// Worked example demonstrating the GAPS-004 BPF observability surface
// through the `crucible::perf::Senses` aggregator (#1285).
//
// What this bench measures, end-to-end:
//
//   (A) ONE-SHOT load cost: `Senses::load_all()` and `load_subset()`.  Both
//       are libbpf-bound; expected wall-clock is ~50-200 ms per program ×
//       7 programs (5 legacy + 2 BTF GAPS-004f) for the verifier + attach +
//       mmap path — total in the ~300-1500 ms range with the BTF facades
//       loaded.  Printed as a banner — NOT a bench-iteration body.
//       Putting libbpf load into a `.measure(...)` loop would be a
//       textbook misuse: each iteration would re-attach and blow past
//       the 1-second total budget on the first call.
//
//   (B) Per-call accessor cost: `s.sense_hub()`, `s.sched_switch()`, etc.
//       Each is a single load from the optional's bit (held in cache),
//       sub-ns in steady state — the cost a production caller pays per
//       facade access.  Demonstrates that the aggregator adds zero overhead
//       on the read path vs hand-holding 7 std::optional<Facade>.
//
//   (C) Coverage diagnostics: `s.coverage()` plus `attached_count()`.  Used
//       by Augur for drift attribution ("can't read CPU stalls — PmuSample
//       is missing") and by bench banners.  Both sub-ns; we bench them
//       separately because attached_count() does 7 conditional adds and
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
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <utility>

#include <unistd.h>  // ::getpid (workload-shaped demo syscall driver)

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
    // GAPS-004g-AUDIT-4: denominator is 7 (5 legacy facades + 2 BTF
    // variants per GAPS-004f), not 5.  The "/5" was correct at GAPS-004y
    // ship time but never updated when SchedTpBtf+SyscallTpBtf were added
    // to the aggregator (commit d165811).
    std::printf("  attached: %zu/7", cov.attached_count());
    if (cov.attached_count() == 0) {
        // Coverage 0/7 is the most common surprise.  Tell the user how to
        // fix it instead of leaving them to grep.  Two recovery paths:
        //   • make bench-caps  (one-time, sticky until rebuild)
        //   • sudo ./bench_perf_loader  (per-invocation)
        std::printf(" — run `cmake --build --preset bench --target bench-caps`"
                    " or invoke under sudo");
    }
    std::printf("\n  sense_hub=%d sched_switch=%d pmu_sample=%d "
                "lock_contention=%d syscall_latency=%d "
                "sched_tp_btf=%d syscall_tp_btf=%d\n",
                cov.sense_hub_attached, cov.sched_switch_attached,
                cov.pmu_sample_attached, cov.lock_contention_attached,
                cov.syscall_latency_attached,
                cov.sched_tp_btf_attached, cov.syscall_tp_btf_attached);
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
        // GAPS-004g-AUDIT-4: BTF-typed facades (GAPS-004f) had no
        // accessor bench.  Same shape as the legacy facades — single
        // optional-bit load — so cost is sub-ns identical, but the
        // omission left the API-surface coverage incomplete.
        [&]{
            return bench::run("senses.sched_tp_btf()", [&]{
                const auto* p = s.sched_tp_btf();
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            return bench::run("senses.syscall_tp_btf()", [&]{
                const auto* p = s.syscall_tp_btf();
                bench::do_not_optimize(p);
            });
        }(),
        // (C) Coverage diagnostic — 7 bools by value (≤ 4 B).  Augur reads
        // this once per drift-detection sample.
        [&]{
            return bench::run("senses.coverage()", [&]{
                const auto c = s.coverage();
                bench::do_not_optimize(c);
            });
        }(),
        [&]{
            return bench::run("coverage.attached_count() [7 cond. adds]", [&]{
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
        // (F) Per-facade snapshot() — the consumer-shaped capture of
        // (scalar metric, timeline_index) on each timeline facade.
        // Every consumer that wants windowed deltas calls snapshot()
        // pre + post and computes `post - pre`; these benches show
        // the per-facade per-snapshot cost.  When attached, each
        // `snapshot()` is dominated by the scalar accessor's
        // `bpf_map_lookup_elem` (~1 µs); when un-attached it costs
        // ~1 ns (single null check).
        [&]{
            return bench::run("sched_switch->snapshot() [1 syscall + 1 mmap-load]", [&]{
                if (const auto* h = s.sched_switch()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                } else {
                    bench::do_not_optimize(h);
                }
            });
        }(),
        [&]{
            return bench::run("pmu_sample->snapshot() [1 mmap-load]", [&]{
                if (const auto* h = s.pmu_sample()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                } else {
                    bench::do_not_optimize(h);
                }
            });
        }(),
        [&]{
            return bench::run("lock_contention->snapshot() [1 syscall + 1 mmap-load]", [&]{
                if (const auto* h = s.lock_contention()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                } else {
                    bench::do_not_optimize(h);
                }
            });
        }(),
        [&]{
            return bench::run("syscall_latency->snapshot() [1 syscall + 1 mmap-load]", [&]{
                if (const auto* h = s.syscall_latency()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                } else {
                    bench::do_not_optimize(h);
                }
            });
        }(),
        [&]{
            return bench::run("sched_tp_btf->snapshot() [1 syscall + 1 mmap-load]", [&]{
                if (const auto* h = s.sched_tp_btf()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                } else {
                    bench::do_not_optimize(h);
                }
            });
        }(),
        [&]{
            return bench::run("syscall_tp_btf->snapshot() [1 syscall + 1 mmap-load]", [&]{
                if (const auto* h = s.syscall_tp_btf()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                } else {
                    bench::do_not_optimize(h);
                }
            });
        }(),
        // (G) Snapshot::operator- — sub_sat cost on a 2-field timeline
        // Snapshot.  Should be ~1-2 ns (two __builtin_sub_overflow).
        // SchedSwitch chosen as the representative; the other 5 are
        // structurally identical.
        [&]{
            crucible::perf::SchedSwitch::Snapshot ss_pre{};
            if (const auto* h = s.sched_switch()) ss_pre = h->snapshot();
            return bench::run("sched_switch::Snapshot::operator- [2 sub_sat]", [&]{
                if (const auto* h = s.sched_switch()) {
                    const auto post  = h->snapshot();
                    const auto delta = post - ss_pre;
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

    // ── (H) Consumer-shaped workload demo ──────────────────────────────
    //
    // Bench is the single consumer of the perf facades for now; this
    // section demonstrates the canonical pre/work/post/delta read
    // pattern that production consumers (Keeper, WorkloadProfiler,
    // Augur, DeadlineWatchdog) will eventually use.
    //
    // Snapshots all 7 facades, runs a workload that should nudge every
    // metric (CPU spin → ctx_switches via timer interrupts; getpid loop
    // → total_syscalls; brief mutex+CV → futex/lock_contention),
    // snapshots again, and prints `post - pre` deltas.
    //
    // When CAP_BPF is missing the deltas are all zero — the print
    // banner is still useful as a "what fields are available" reference
    // for prospective consumers writing against the same API.
    std::printf("\n=== consumer-shaped workload demo ===\n");
    std::printf("Pattern: snapshot pre → run nudge workload → snapshot post → print delta\n");

    // Pre snapshots
    crucible::perf::Snapshot                       sh_pre {};
    crucible::perf::SchedSwitch::Snapshot          ss_pre {};
    crucible::perf::PmuSample::Snapshot            pm_pre {};
    crucible::perf::LockContention::Snapshot       lc_pre {};
    crucible::perf::SyscallLatency::Snapshot       sl_pre {};
    crucible::perf::SchedTpBtf::Snapshot           stp_pre{};
    crucible::perf::SyscallTpBtf::Snapshot         syt_pre{};
    if (const auto* h = s.sense_hub())       sh_pre  = h->read();
    if (const auto* h = s.sched_switch())    ss_pre  = h->snapshot();
    if (const auto* h = s.pmu_sample())      pm_pre  = h->snapshot();
    if (const auto* h = s.lock_contention()) lc_pre  = h->snapshot();
    if (const auto* h = s.syscall_latency()) sl_pre  = h->snapshot();
    if (const auto* h = s.sched_tp_btf())    stp_pre = h->snapshot();
    if (const auto* h = s.syscall_tp_btf())  syt_pre = h->snapshot();

    // Workload: short CPU spin (timer-interrupt-driven ctx switches),
    // syscall driver (raw_syscalls/sys_exit events), and a futex_wait
    // (sys_enter_futex / sys_exit_futex events for lock_contention).
    // Sized for ~50 ms total wall-clock so the deltas are large enough
    // to be visible above noise floor on a quiet system.
    {
        std::uint64_t spin_acc = 0;
        const auto spin_until = steady::now() + std::chrono::milliseconds(30);
        while (steady::now() < spin_until) {
            for (int i = 0; i < 1024; ++i) {
                spin_acc += static_cast<std::uint64_t>(i) * 2654435761ULL;
            }
            bench::do_not_optimize(spin_acc);
        }
        for (int i = 0; i < 200; ++i) (void)::getpid();
        // Brief futex_wait via a timed cv.wait_for on a never-notified cv.
        // 1 ms timeout is enough for sys_enter_futex / sys_exit_futex to
        // both record without slowing the demo significantly.
        std::mutex m;
        std::condition_variable cv;
        std::unique_lock<std::mutex> lk(m);
        (void)cv.wait_for(lk, std::chrono::milliseconds(1));
    }

    // Post snapshots
    crucible::perf::Snapshot                       sh_post {};
    crucible::perf::SchedSwitch::Snapshot          ss_post {};
    crucible::perf::PmuSample::Snapshot            pm_post {};
    crucible::perf::LockContention::Snapshot       lc_post {};
    crucible::perf::SyscallLatency::Snapshot       sl_post {};
    crucible::perf::SchedTpBtf::Snapshot           stp_post{};
    crucible::perf::SyscallTpBtf::Snapshot         syt_post{};
    if (const auto* h = s.sense_hub())       sh_post  = h->read();
    if (const auto* h = s.sched_switch())    ss_post  = h->snapshot();
    if (const auto* h = s.pmu_sample())      pm_post  = h->snapshot();
    if (const auto* h = s.lock_contention()) lc_post  = h->snapshot();
    if (const auto* h = s.syscall_latency()) sl_post  = h->snapshot();
    if (const auto* h = s.sched_tp_btf())    stp_post = h->snapshot();
    if (const auto* h = s.syscall_tp_btf())  syt_post = h->snapshot();

    // Print deltas — what a consumer sees per-window.
    if (cov.sense_hub_attached) {
        using Idx = crucible::perf::Idx;
        const auto sh_d = sh_post - sh_pre;
        std::printf("  SenseHub.delta:        ctx_vol=%llu  ctx_invol=%llu  migrations=%llu  "
                    "futex_waits=%llu\n",
                    static_cast<unsigned long long>(sh_d[Idx::SCHED_CTX_VOL]),
                    static_cast<unsigned long long>(sh_d[Idx::SCHED_CTX_INVOL]),
                    static_cast<unsigned long long>(sh_d[Idx::SCHED_MIGRATIONS]),
                    static_cast<unsigned long long>(sh_d[Idx::FUTEX_WAIT_COUNT]));
    }
    if (cov.sched_switch_attached) {
        const auto d = ss_post - ss_pre;
        std::printf("  SchedSwitch.delta:     ctx_switches=%llu  timeline_events=%llu\n",
                    (unsigned long long)d.ctx_switches, (unsigned long long)d.timeline_index);
    }
    if (cov.pmu_sample_attached) {
        const auto d = pm_post - pm_pre;
        std::printf("  PmuSample.delta:       samples=%llu\n",
                    (unsigned long long)d.samples);
    }
    if (cov.lock_contention_attached) {
        const auto d = lc_post - lc_pre;
        std::printf("  LockContention.delta:  wait_count=%llu  timeline_events=%llu\n",
                    (unsigned long long)d.wait_count, (unsigned long long)d.timeline_index);
    }
    if (cov.syscall_latency_attached) {
        const auto d = sl_post - sl_pre;
        std::printf("  SyscallLatency.delta:  total_syscalls=%llu  timeline_events=%llu\n",
                    (unsigned long long)d.total_syscalls, (unsigned long long)d.timeline_index);
    }
    if (cov.sched_tp_btf_attached) {
        const auto d = stp_post - stp_pre;
        std::printf("  SchedTpBtf.delta:      ctx_switches=%llu  timeline_events=%llu\n",
                    (unsigned long long)d.ctx_switches, (unsigned long long)d.timeline_index);
    }
    if (cov.syscall_tp_btf_attached) {
        const auto d = syt_post - syt_pre;
        std::printf("  SyscallTpBtf.delta:    total_syscalls=%llu  timeline_events=%llu\n",
                    (unsigned long long)d.total_syscalls, (unsigned long long)d.timeline_index);
    }
    if (cov.attached_count() == 0) {
        std::printf("  (no facades attached; deltas would have been printed here.\n"
                    "   Grant CAP_BPF+CAP_PERFMON to see real metric movement.)\n");
    }

    bench::emit_reports_json(reports, json);
    return 0;
}
