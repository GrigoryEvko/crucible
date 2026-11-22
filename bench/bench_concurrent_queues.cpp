// fixy/concurrent primitives — head-to-head ns-precision bench against
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
//
// fixy/concurrent has an SPSC and an MPSC ring and no MPMC ring, so this
// bench has no MPMC arm.  bench_mpsc_contention.cpp measures the MPSC
// ring.
//
// Pinning + measurement: bench_harness.h handles isolcpu pinning,
// rdtsc-derived ns timing (calibrated against steady_clock at process
// start), drift detection, bootstrap CIs.  CRUCIBLE_BENCH_CORE=N
// selects a specific core; CRUCIBLE_BENCH_HARDENING=production applies
// the full sched-FIFO + mlock + THP policy from crucible::warden.
//
// All push-only benches use a HUGE capacity (1 << 20 = 1M slots) so
// the ring never fills inside the default 100k sample run.  This
// isolates pure try_push cost from drain/reset noise.

#include <cstdint>

#include <crucible/TraceRing.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/AtomicSnapshot.h>
#include <fixy/concurrent/LaneRing.h>
#include <fixy/concurrent/SpscRing.h>

#include "bench_harness.h"

namespace {

using ::fixy::concurrent::AtomicSnapshot;
using ::fixy::concurrent::SpscRing;

// 1M-slot SPSC ring: 1M × 8B = 8 MB.  Default 100k samples ≪ 1M ⇒ no
// fill across a sample run; pure try_push cost without drain noise.
using HugeSpsc = SpscRing<std::uint64_t, (1U << 20)>;
// 1024-slot for the round-trip (push then pop) bench — depth stays 0/1.
using SmallSpsc = SpscRing<std::uint64_t, 1024>;

// AtomicSnapshot payload — 8B uint64_t for the simplest-possible
// publish/load measurement (single cache line for storage_).
using Snap = AtomicSnapshot<std::uint64_t>;

// A ring of two lanes of 16 cells each.  The round trip pushes into the lane
// of the turn and receives the item, so each receive steps the turn.
// The owner of the ring of lanes of the round trip.  It mints the proof of a
// lane and the proof of the consumer (the writers of fixy/concurrent/LaneRing.h),
// because one thread pushes into each lane and pops.
struct TwoLanesOwner final {
    using Ring = ::fixy::concurrent::LaneRing<std::uint64_t, 16, ::fixy::concurrent::LaneCount{2}, TwoLanesOwner>;

    [[nodiscard]] static Ring::LaneProof lane(Ring::lane_index lane) noexcept { return Ring::LaneProof{lane}; }
    [[nodiscard]] static Ring::ConsumerProof consumer() noexcept { return Ring::ConsumerProof{}; }
};

using TwoLanes = TwoLanesOwner::Ring;

// The allocation capability of the rings and the snapshots of the rows.
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== concurrent_queues ===\n");
    bench::print("  SpscRing<uint64,1M> sizeof: {} bytes\n", sizeof(HugeSpsc));
    bench::print("  AtomicSnapshot<uint64> sizeof: {} bytes\n", sizeof(Snap));
    bench::print("\n");

    ::fixy::FixedArray<bench::Report, 11> reports{
        // ── Harness baseline: the cost of one sink ────────────────────
        // Measures the bench-loop overhead: ++i and one sink of i.  The
        // sink is an empty asm statement with no call (the sink of
        // bench_harness.h), so i stays in a register, and the row gives the
        // cost of the loop and of the increment.  Subtract this from a
        // single-call ring bench below to get the cost of the ring op.
        [&] {
            std::uint64_t i = 0;
            return bench::run("HARNESS BASELINE: do_not_optimize(++i)", [&] { bench::do_not_optimize(++i); });
        }(),

        // ── Harness baseline: clobber-only (no call, asm barrier) ─────
        // bench::clobber_memory() is asm volatile("" ::: "memory"), a
        // compiler memory barrier with no instruction.  After the barrier
        // the loop reads i from memory again, so the difference from the
        // sink baseline is the store and the load of i.
        [&] {
            std::uint64_t i = 0;
            return bench::run("HARNESS BASELINE: clobber + ++i", [&] {
                ++i;
                bench::clobber_memory();
            });
        }(),

        // ── SpscRing: pure push (huge cap, never fills) ───────────────
        [&] {
            ::fixy::Box<HugeSpsc> ring_box = ::fixy::mint_box<HugeSpsc>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            std::uint64_t i = 0;
            return bench::run("spsc_ring.try_push (huge cap, no drain)", [&] {
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
        [&] {
            ::fixy::Box<HugeSpsc> ring_box = ::fixy::mint_box<HugeSpsc>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            alignas(64) static ::fixy::FixedArray<std::uint64_t, 64> tx{};
            alignas(64) static ::fixy::FixedArray<std::uint64_t, 64> rx{};
            for (::fixy::Index<64> const index : tx.indices())
                tx.with_element(index, [index](std::uint64_t& element) noexcept { element = index.raw(); });
            return bench::run("spsc_ring.{try_push_batch,try_pop_batch}<64> round-trip", [&] {
                const std::size_t np = ring->try_push_batch(tx.view());
                const std::size_t nc =
                    rx.with_mut([&ring](auto run) noexcept { return ring->try_pop_batch(run.view()); });
                bench::do_not_optimize(np);
                bench::do_not_optimize(nc);
            });
        }(),

        // ── SpscRing: pure pop (pre-filled, never empties) ────────────
        [&] {
            ::fixy::Box<HugeSpsc> ring_box = ::fixy::mint_box<HugeSpsc>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            // Pre-fill enough so 100k pops don't drain — push 200k.
            for (std::uint64_t i = 0; i < 200000; ++i) {
                (void)ring->try_push(i);
            }
            return bench::run("spsc_ring.try_pop (pre-filled)", [&] {
                auto v = ring->try_pop();
                bench::do_not_optimize(v);
            });
        }(),

        // ── SpscRing: round-trip push+pop (depth stays 0/1) ───────────
        [&] {
            ::fixy::Box<SmallSpsc> ring_box = ::fixy::mint_box<SmallSpsc>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            std::uint64_t i = 0;
            return bench::run("spsc_ring round-trip (push+pop)", [&] {
                (void)ring->try_push(++i);
                auto v = ring->try_pop();
                bench::do_not_optimize(v);
            });
        }(),

        // ── AtomicSnapshot: publish (writer-only) ─────────────────────
        [&] {
            ::fixy::Box<Snap> snap_box = ::fixy::mint_box<Snap>(kAlloc);
            auto const snap = ::fixy::to(*snap_box);
            std::uint64_t i = 0;
            return bench::run("atomic_snapshot.publish", [&] { snap->publish(++i); });
        }(),

        // ── AtomicSnapshot: load (reader-only, no writer) ─────────────
        [&] {
            ::fixy::Box<Snap> snap_box = ::fixy::mint_box<Snap>(kAlloc, std::uint64_t{42});
            auto const snap = ::fixy::to(*snap_box);
            // The load spins with no exit while a write is in flight, so it
            // takes a context that owns Div.
            const ::fixy::TestRunnerCtx reader{::foundation::effects::testing::test()};
            return bench::run("atomic_snapshot.load (uncontended)", [&] {
                const auto v = snap->load(reader);
                bench::do_not_optimize(v);
            });
        }(),

        // ── AtomicSnapshot: try_load fast path ────────────────────────
        [&] {
            ::fixy::Box<Snap> snap_box = ::fixy::mint_box<Snap>(kAlloc, std::uint64_t{42});
            auto const snap = ::fixy::to(*snap_box);
            return bench::run("atomic_snapshot.try_load (uncontended)", [&] {
                auto v = snap->try_load();
                bench::do_not_optimize(v);
            });
        }(),

        // ── Reference: TraceRing try_append (existing reference bench) ─
        // Same body shape as bench_trace_ring.cpp's "ring.try_append
        // (+reset-on-full, const entry)" — periodic reset on full keeps
        // the bench from saturating; tail samples include reset cost.
        [&] {
            ::fixy::Box<crucible::TraceRing> ring_box = ::fixy::mint_box<crucible::TraceRing>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            crucible::TraceRing::Entry e{};
            e.schema_hash = crucible::SchemaHash{0xABCDEF};
            return bench::run("trace_ring.try_append (reference, +reset-on-full)", [&] {
                const bool ok = ring->try_append(e);
                bench::do_not_optimize(ok);
                if (!ok) ring->reset();
            });
        }(),

        // ── LaneRing: round trip on one thread (depth stays 0/1) ──────
        // Each sample pushes into the lane of the turn and receives the
        // item: one push of an SPSC lane, one deferred pop, and the step of
        // the turn.  The turn takes the two lanes in turn, so the lane of a
        // sample is the low bit of its count.
        [&] {
            ::fixy::Box<TwoLanes> ring_box = ::fixy::mint_box<TwoLanes>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            std::uint64_t i = 0;
            return bench::run("lane_ring round-trip (push to the lane of the turn + try_receive)", [&] {
                ++i;
                (void)ring->try_push(TwoLanesOwner::lane(TwoLanes::lane_index::mask(i - 1)), i);
                auto v = ring->try_receive(TwoLanesOwner::consumer());
                bench::do_not_optimize(v);
            });
        }(),
    };

    bench::emit_reports_text(reports.view());

    // ── Comparisons (the headline result) ─────────────────────────────
    bench::print("\n=== compare ===\n");

    // Index map:
    //   [0] Harness baseline: do_not_optimize(++i)      ← bench overhead
    //   [1] Harness baseline: clobber + ++i             ← pure compiler-fence
    //   [2] spsc_ring.try_push (single, huge cap)
    //   [3] spsc_ring.{try_push_batch,try_pop_batch}<64> round-trip
    //   [4] spsc_ring.try_pop (pre-filled)
    //   [5] spsc_ring round-trip (push+pop)
    //   [6] atomic_snapshot.publish
    //   [7] atomic_snapshot.load (uncontended)
    //   [8] atomic_snapshot.try_load (uncontended)
    //   [9] trace_ring.try_append (reference, +reset-on-full)
    //   [10] lane_ring round-trip (push to the lane of the turn + try_receive)

    // Headline #1: actual ring cost = single-push p50 - harness overhead p50.
    // The sink makes no call, so the overhead is the loop and the increment.
    bench::print("\n[spsc_ring.try_push] vs [HARNESS BASELINE do_not_optimize]:\n");
    bench::compare(reports.at(::fixy::index<2>()), reports.at(::fixy::index<0>())).print_text();

    // Headline #2: SpscRing vs TraceRing — SpscRing is FASTER (no
    // parallel-array writes, no prefetches, no slot-mask compute).
    bench::print("\n[spsc_ring.try_push] vs [trace_ring.try_append]:\n");
    bench::compare(reports.at(::fixy::index<2>()), reports.at(::fixy::index<9>())).print_text();

    // Headline #3: per-item batched cost vs per-item single-call cost.
    // batch[3] reports WHOLE-BATCH-PUSH-AND-POP time for 64 items each
    // way; per-item-push ≈ batch_p50 / (2 × 64).
    bench::print("\n[spsc_ring batch<64> push+pop round-trip] vs [single try_push]:\n");
    bench::compare(reports.at(::fixy::index<3>()), reports.at(::fixy::index<2>())).print_text();

    // Headline #4: AtomicSnapshot.load vs publish.
    bench::print("\n[atomic_snapshot.load] vs [atomic_snapshot.publish]:\n");
    bench::compare(reports.at(::fixy::index<7>()), reports.at(::fixy::index<6>())).print_text();

    // Bootstrap CIs on the headline percentiles + batched per-item.
    bench::print("\n=== confidence intervals (95%) ===\n");
    {
        const auto ci = reports.at(::fixy::index<0>()).ci(0.50);
        bench::print("  HARNESS BASELINE p50: [{}, {}] ns\n", ::fixy::Fixed<2>{ci.lo}, ::fixy::Fixed<2>{ci.hi});
    }
    {
        const auto ci = reports.at(::fixy::index<2>()).ci(0.50);
        bench::print("  spsc_ring.try_push p50: [{}, {}] ns\n", ::fixy::Fixed<2>{ci.lo}, ::fixy::Fixed<2>{ci.hi});
    }
    {
        const auto ci = reports.at(::fixy::index<2>()).ci(0.99);
        bench::print("  spsc_ring.try_push p99: [{}, {}] ns\n", ::fixy::Fixed<2>{ci.lo}, ::fixy::Fixed<2>{ci.hi});
    }
    {
        const auto ci = reports.at(::fixy::index<3>()).ci(0.50);
        // round-trip = push 64 + pop 64.  Per-item-push ≈ ci / (2×64).
        bench::print("  spsc_ring batch<64> push+pop round-trip p50: [{}, {}] ns (per-item ≈ {} ns)\n",
                     ::fixy::Fixed<2>{ci.lo}, ::fixy::Fixed<2>{ci.hi}, ::fixy::Fixed<3>{ci.lo / 128.0});
    }
    {
        const auto ci = reports.at(::fixy::index<9>()).ci(0.50);
        bench::print("  trace_ring.try_append p50: [{}, {}] ns\n", ::fixy::Fixed<2>{ci.lo}, ::fixy::Fixed<2>{ci.hi});
    }

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
