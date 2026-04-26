// concurrent/* primitives — head-to-head ns-precision bench against
// TraceRing (the canonical reference SPSC primitive at ~5-8ns/op).
//
// What this validates:
//   1. SpscRing.try_push / try_pop hits the same single-L1-op envelope
//      (~1-3 ns p50, ~5-8 ns p99 on a pinned core) as TraceRing.try_append.
//      Same pattern (load(relaxed) + store(release) inside an
//      AtomicMonotonic wrapper); same codegen under hot-TU contract
//      semantics.
//   2. AtomicMonotonic wrapper introduces ZERO overhead vs raw atomic
//      — the AtomicMonotonic-wrapped SpscRing should be statistically
//      indistinguishable from the bespoke TraceRing's bare-atomic
//      head/tail.
//   3. AtomicSnapshot publish (~15 ns) and load (~5-10 ns) match
//      THREADING.md §10.1 published targets after the seqlock
//      AtomicMonotonic migration.
//   4. MpmcRing push (~15-25 ns uncontended; ~30-60 ns at 16-way
//      contention per Nikolaev SCQ) matches THREADING.md §10.1.
//
// Pinning + measurement: bench_harness.h handles isolcpu pinning,
// rdtsc-derived ns timing (calibrated against steady_clock at process
// start), drift detection, bootstrap CIs.  CRUCIBLE_BENCH_CORE=N
// selects a specific core; CRUCIBLE_BENCH_HARDENING=production applies
// the full sched-FIFO + mlock + THP policy from crucible::rt.
//
// All push-only benches use a HUGE capacity (1 << 20 = 1M slots) so
// the ring never fills inside the default 100k sample run.  This
// isolates pure try_push cost from drain/reset noise.

#include <cstdio>
#include <cstdint>

#include <crucible/TraceRing.h>
#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/concurrent/MpmcRing.h>
#include <crucible/concurrent/SpscRing.h>

#include "bench_harness.h"

namespace {

using crucible::concurrent::AtomicSnapshot;
using crucible::concurrent::MpmcRing;
using crucible::concurrent::SpscRing;

// 1M-slot SPSC ring: 1M × 8B = 8 MB.  Default 100k samples ≪ 1M ⇒ no
// fill across a sample run; pure try_push cost without drain noise.
using HugeSpsc = SpscRing<std::uint64_t, (1U << 20)>;
// 1024-slot for the round-trip (push then pop) bench — depth stays 0/1.
using SmallSpsc = SpscRing<std::uint64_t, 1024>;

// MpmcRing: 1M Capacity = 2M cells = ~16 MB inline.
using HugeMpmc = MpmcRing<std::uint64_t, (1U << 20)>;
using SmallMpmc = MpmcRing<std::uint64_t, 1024>;

// AtomicSnapshot payload — 8B uint64_t for the simplest-possible
// publish/load measurement (single cache line for storage_).
using Snap = AtomicSnapshot<std::uint64_t>;

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== concurrent_queues ===\n");
    std::printf("  SpscRing<uint64,1M> sizeof: %zu bytes\n", sizeof(HugeSpsc));
    std::printf("  MpmcRing<uint64,1M> sizeof: %zu bytes\n", sizeof(HugeMpmc));
    std::printf("  AtomicSnapshot<uint64> sizeof: %zu bytes\n", sizeof(Snap));
    std::printf("\n");

    bench::Report reports[] = {
        // ── SpscRing: pure push (huge cap, never fills) ───────────────
        [&]{
            auto ring = std::make_unique<HugeSpsc>();
            std::uint64_t i = 0;
            return bench::run("spsc_ring.try_push (huge cap, no drain)", [&]{
                const bool ok = ring->try_push(++i);
                bench::do_not_optimize(ok);
            });
        }(),

        // ── SpscRing: pure pop (pre-filled, never empties) ────────────
        [&]{
            auto ring = std::make_unique<HugeSpsc>();
            // Pre-fill enough so 100k pops don't drain — push 200k.
            for (std::uint64_t i = 0; i < 200'000; ++i) {
                (void)ring->try_push(i);
            }
            return bench::run("spsc_ring.try_pop (pre-filled)", [&]{
                auto v = ring->try_pop();
                bench::do_not_optimize(v);
            });
        }(),

        // ── SpscRing: round-trip push+pop (depth stays 0/1) ───────────
        [&]{
            auto ring = std::make_unique<SmallSpsc>();
            std::uint64_t i = 0;
            return bench::run("spsc_ring round-trip (push+pop)", [&]{
                (void)ring->try_push(++i);
                auto v = ring->try_pop();
                bench::do_not_optimize(v);
            });
        }(),

        // ── MpmcRing: pure push, single-thread (no contention) ────────
        [&]{
            auto ring = std::make_unique<HugeMpmc>();
            std::uint64_t i = 0;
            return bench::run("mpmc_ring.try_push (1T, huge cap)", [&]{
                const bool ok = ring->try_push(++i);
                bench::do_not_optimize(ok);
            });
        }(),

        // ── MpmcRing: pure pop, single-thread ─────────────────────────
        [&]{
            auto ring = std::make_unique<HugeMpmc>();
            for (std::uint64_t i = 0; i < 200'000; ++i) {
                (void)ring->try_push(i);
            }
            return bench::run("mpmc_ring.try_pop (1T, pre-filled)", [&]{
                auto v = ring->try_pop();
                bench::do_not_optimize(v);
            });
        }(),

        // ── AtomicSnapshot: publish (writer-only) ─────────────────────
        [&]{
            auto snap = std::make_unique<Snap>();
            std::uint64_t i = 0;
            return bench::run("atomic_snapshot.publish", [&]{
                snap->publish(++i);
            });
        }(),

        // ── AtomicSnapshot: load (reader-only, no writer) ─────────────
        [&]{
            auto snap = std::make_unique<Snap>(42ULL);
            return bench::run("atomic_snapshot.load (uncontended)", [&]{
                const auto v = snap->load();
                bench::do_not_optimize(v);
            });
        }(),

        // ── AtomicSnapshot: try_load fast path ────────────────────────
        [&]{
            auto snap = std::make_unique<Snap>(42ULL);
            return bench::run("atomic_snapshot.try_load (uncontended)", [&]{
                auto v = snap->try_load();
                bench::do_not_optimize(v);
            });
        }(),

        // ── Reference: TraceRing try_append (existing reference bench) ─
        // Same body shape as bench_trace_ring.cpp's "ring.try_append
        // (+reset-on-full, const entry)" — periodic reset on full keeps
        // the bench from saturating; tail samples include reset cost.
        [&]{
            auto ring = std::make_unique<crucible::TraceRing>();
            crucible::TraceRing::Entry e{};
            e.schema_hash = crucible::SchemaHash{0xABCDEF};
            return bench::run("trace_ring.try_append (reference, +reset-on-full)", [&]{
                const bool ok = ring->try_append(e);
                bench::do_not_optimize(ok);
                if (!ok) ring->reset();
            });
        }(),
    };

    bench::emit_reports_text(reports);

    // ── Comparisons (the headline result) ─────────────────────────────
    std::printf("\n=== compare ===\n");

    // Headline #1: SpscRing.try_push (canonical, AtomicMonotonic-wrapped)
    // vs TraceRing.try_append (bespoke, also AtomicMonotonic-wrapped post-#505).
    // SpscRing should be FASTER because it doesn't write the 3 parallel
    // arrays (meta_starts, scope_hashes, callsite_hashes) that TraceRing
    // does on every append.
    std::printf("\n[spsc_ring.try_push] vs [trace_ring.try_append]:\n");
    bench::compare(reports[0], reports[8]).print_text(stdout);

    // Headline #2: MpmcRing.try_push at 1T should be ~2-4× SpscRing
    // (per Nikolaev SCQ targets: SCQ uncontended ~15-25ns, SPSC ~5-8ns).
    std::printf("\n[mpmc_ring.try_push] vs [spsc_ring.try_push]:\n");
    bench::compare(reports[3], reports[0]).print_text(stdout);

    // Headline #3: AtomicSnapshot.load is the cheapest of the three
    // (single acquire load + memcpy + acquire fence + acquire load);
    // publish is the most expensive (two fetch_add + memcpy).
    std::printf("\n[atomic_snapshot.load] vs [atomic_snapshot.publish]:\n");
    bench::compare(reports[6], reports[5]).print_text(stdout);

    // Bootstrap CIs on the headline percentiles.
    std::printf("\n=== confidence intervals (95%%) ===\n");
    {
        const auto ci = reports[0].ci(0.50);
        std::printf("  spsc_ring.try_push p50: [%.2f, %.2f] ns\n", ci.lo, ci.hi);
    }
    {
        const auto ci = reports[0].ci(0.99);
        std::printf("  spsc_ring.try_push p99: [%.2f, %.2f] ns\n", ci.lo, ci.hi);
    }
    {
        const auto ci = reports[8].ci(0.50);
        std::printf("  trace_ring.try_append p50: [%.2f, %.2f] ns\n", ci.lo, ci.hi);
    }
    {
        const auto ci = reports[8].ci(0.99);
        std::printf("  trace_ring.try_append p99: [%.2f, %.2f] ns\n", ci.lo, ci.hi);
    }

    bench::emit_reports_json(reports, json);
    return 0;
}
