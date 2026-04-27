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
        // ── HARNESS BASELINE: pure do_not_optimize cost ───────────────
        // Measures the bench-loop overhead: ++i + a single noipa call
        // to do_not_optimize.  Subtract this from any single-call ring
        // bench below to recover the actual ring-op cost (the noipa
        // attribute on do_not_optimize forces a real CALL+RET that
        // contributes ~3-4 cycles ≈ 0.7-1ns at 4.6GHz).
        [&]{
            std::uint64_t i = 0;
            return bench::run("HARNESS BASELINE: do_not_optimize(++i)", [&]{
                bench::do_not_optimize(++i);
            });
        }(),

        // ── HARNESS BASELINE: clobber-only (no call, asm barrier) ─────
        // Cheaper alternative: bench::clobber() is "asm volatile("":::
        // memory")" — zero runtime cost, just a compiler memory fence.
        // Difference between this and the do_not_optimize baseline is
        // the pure CALL+RET cost.
        [&]{
            std::uint64_t i = 0;
            return bench::run("HARNESS BASELINE: clobber + ++i", [&]{
                ++i;
                bench::clobber();
            });
        }(),

        // ── SpscRing: pure push (huge cap, never fills) ───────────────
        [&]{
            auto ring = std::make_unique<HugeSpsc>();
            std::uint64_t i = 0;
            return bench::run("spsc_ring.try_push (huge cap, no drain)", [&]{
                const bool ok = ring->try_push(++i);
                bench::do_not_optimize(ok);
            });
        }(),

        // ── SpscRing: BATCHED push+pop pair (per-item amortized) ──────
        // Push 64 items into ring, immediately pop 64 items out.  Keeps
        // the ring at depth 0/64 so every iteration does FULL work
        // (no degenerate ring-full early-returns).  Per-item cost =
        // (whole_batch_ns / 2) / 64 — divides by 2 because each
        // iteration does push+pop, then by 64 for per-item.
        //
        // Codegen on this AMD Ryzen 9 5950X (Zen 3, AVX2-only):
        // memcpy of 64×8B = 512B vectorizes to 16 × VMOVDQA ymm
        // (AVX2, 256-bit, 32B per store) — verified by objdump.
        // Zen 3's L1d store-buffer absorbs ~1 store/cycle issue, so
        // 16 stores ≈ 16 cycles ≈ 3.5ns per direction.  Round-trip
        // total ≈ 7ns memcpy + 6 atomics (~1.3ns) + harness overhead
        // (~5ns) ≈ 13-15ns measured.  Per-item ≈ 0.117ns.
        // (Zen 4+ would emit AVX-512 ZMM stores; this CPU does not.)
        [&]{
            auto ring = std::make_unique<HugeSpsc>();
            alignas(64) static std::array<std::uint64_t, 64> tx{};
            alignas(64) static std::array<std::uint64_t, 64> rx{};
            for (std::size_t k = 0; k < tx.size(); ++k) tx[k] = k;
            return bench::run("spsc_ring.{try_push_batch,try_pop_batch}<64> round-trip", [&]{
                const std::size_t np = ring->try_push_batch(std::span{tx});
                const std::size_t nc = ring->try_pop_batch(std::span{rx});
                bench::do_not_optimize(np);
                bench::do_not_optimize(nc);
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

    // Index map (after PERF-1 additions):
    //   [0] HARNESS BASELINE: do_not_optimize(++i)      ← bench overhead
    //   [1] HARNESS BASELINE: clobber + ++i             ← pure compiler-fence
    //   [2] spsc_ring.try_push (single, huge cap)
    //   [3] spsc_ring.{try_push_batch,try_pop_batch}<64> round-trip
    //   [4] spsc_ring.try_pop (pre-filled)
    //   [5] spsc_ring round-trip (push+pop)
    //   [6] mpmc_ring.try_push (1T, huge cap)
    //   [7] mpmc_ring.try_pop (1T, pre-filled)
    //   [8] atomic_snapshot.publish
    //   [9] atomic_snapshot.load (uncontended)
    //   [10] atomic_snapshot.try_load (uncontended)
    //   [11] trace_ring.try_append (reference, +reset-on-full)

    // Headline #1: actual ring cost = single-push p50 - harness overhead p50.
    // (do_not_optimize is a [[gnu::noipa]] real CALL+RET ≈ 3-4 cycles ≈
    // 0.7-1ns at 4.6GHz on Zen 3.)
    std::printf("\n[spsc_ring.try_push] vs [HARNESS BASELINE do_not_optimize]:\n");
    bench::compare(reports[2], reports[0]).print_text(stdout);

    // Headline #2: SpscRing vs TraceRing — SpscRing is FASTER (no
    // parallel-array writes, no prefetches, no slot-mask compute).
    std::printf("\n[spsc_ring.try_push] vs [trace_ring.try_append]:\n");
    bench::compare(reports[2], reports[11]).print_text(stdout);

    // Headline #3: per-item batched cost vs per-item single-call cost.
    // batch[3] reports WHOLE-BATCH-PUSH-AND-POP time for 64 items each
    // way; per-item-push ≈ batch_p50 / (2 × 64).
    std::printf("\n[spsc_ring batch<64> push+pop round-trip] vs [single try_push]:\n");
    bench::compare(reports[3], reports[2]).print_text(stdout);

    // Headline #4: MpmcRing vs SpscRing under no contention.
    std::printf("\n[mpmc_ring.try_push (1T)] vs [spsc_ring.try_push]:\n");
    bench::compare(reports[6], reports[2]).print_text(stdout);

    // Headline #5: AtomicSnapshot.load vs publish.
    std::printf("\n[atomic_snapshot.load] vs [atomic_snapshot.publish]:\n");
    bench::compare(reports[9], reports[8]).print_text(stdout);

    // Bootstrap CIs on the headline percentiles + batched per-item.
    std::printf("\n=== confidence intervals (95%%) ===\n");
    {
        const auto ci = reports[0].ci(0.50);
        std::printf("  HARNESS BASELINE p50: [%.2f, %.2f] ns\n", ci.lo, ci.hi);
    }
    {
        const auto ci = reports[2].ci(0.50);
        std::printf("  spsc_ring.try_push p50: [%.2f, %.2f] ns\n", ci.lo, ci.hi);
    }
    {
        const auto ci = reports[2].ci(0.99);
        std::printf("  spsc_ring.try_push p99: [%.2f, %.2f] ns\n", ci.lo, ci.hi);
    }
    {
        const auto ci = reports[3].ci(0.50);
        // round-trip = push 64 + pop 64.  Per-item-push ≈ ci / (2×64).
        std::printf("  spsc_ring batch<64> push+pop round-trip p50: [%.2f, %.2f] ns "
                    "(per-item ≈ %.3f ns)\n",
                    ci.lo, ci.hi, ci.lo / 128.0);
    }
    {
        const auto ci = reports[11].ci(0.50);
        std::printf("  trace_ring.try_append p50: [%.2f, %.2f] ns\n", ci.lo, ci.hi);
    }

    bench::emit_reports_json(reports, json);
    return 0;
}
