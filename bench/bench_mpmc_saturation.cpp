// MpmcRing saturation sweep — find the SCQ algorithmic per-call floor.
//
// MpmcRing (Nikolaev SCQ, DISC 2019) does NOT ship a batched API.
// Each try_push is structurally:
//   1 × FAA(tail)        — claim a ticket
//   1 × cell.state.load  — observe cycle
//   1 × cell.data write  — payload commit
//   1 × cell.state CAS   — publish state
//   1 × threshold load   — fast-path empty check
//   (potentially) 1 × threshold store on first commit after empty
//
// And try_pop is symmetric:
//   1 × threshold load   — empty fast-path bail
//   1 × FAA(head)        — claim a ticket
//   1 × cell.state.load  — observe cycle
//   1 × cell.data read   — payload extract
//   1 × cell.state.fetch_and — clear Occupied
//
// Per call: roughly 4-5 atomic operations on the hot path, all on
// SEPARATE cache lines (head_ / tail_ / threshold_ / per-cell state
// are all alignas(64)).  No false sharing; but the FAA + CAS pair on
// every call is the algorithmic floor.
//
// Compare to SpscRing's batched API:
//   try_push_batch<N> — 1 head load + 1 tail load + N cell stores +
//                       1 head release-store.  3 atomics per BATCH,
//                       not per item.  N=1024 → 0.075 ns/item (port-
//                       limited L1d store-buffer throughput).
//
// MpmcRing's per-call ~20-30 ns single-thread baseline therefore
// represents the ALGORITHMIC bound of FAA-then-CAS, NOT the L1d-port
// bound.  This bench measures it and reports the gap to SPSC's
// batched ceiling — that gap is the upper bound on what a batched
// MPMC API (1 FAA(N) + N cell-CAS bursts) could deliver.
//
// Methodology:
//   1. Single-call try_push uncontended (push, no concurrent consumer).
//   2. Single-call try_pop uncontended (pop after pre-fill).
//   3. Round-trip pairs: push+pop in tight loop (single thread).
//      Reveals whether inner-loop replication amortizes anything
//      (it shouldn't — each call still does its own FAA+CAS, and the
//      atomic cost is per-call regardless of batch).
//   4. Cell-state cache-warmth sweep: push N items consecutively from
//      cold start — measures whether per-cell cache-line warming
//      affects throughput as we walk through the buffer.

#include <array>
#include <cstdio>
#include <cstdint>
#include <span>

#include <crucible/concurrent/BitmapMpscRing.h>
#include <crucible/concurrent/MpmcRing.h>
#include <crucible/concurrent/MpscRing.h>
#include <crucible/concurrent/SpscRing.h>

#include "bench_harness.h"

namespace {

using crucible::concurrent::BitmapMpscRing;
using crucible::concurrent::MpmcRing;
using crucible::concurrent::MpscRing;
using crucible::concurrent::SpscRing;

using Item = std::uint64_t;

// 1M-slot ring — never fills under any single-thread bench.
using MpmcLarge       = MpmcRing<Item, (1U << 20)>;
using MpscLarge       = MpscRing<Item, (1U << 20)>;
using BitmapMpscLarge = BitmapMpscRing<Item, (1U << 20)>;
using SpscLarge       = SpscRing<Item, (1U << 20)>;

// ── Single-call benches ───────────────────────────────────────────────

bench::Report mpmc_single_push() {
    auto ring = std::make_unique<MpmcLarge>();
    Item i = 0;
    return bench::run("mpmc_ring.try_push (single-call, uncontended)", [&]{
        const bool ok = ring->try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report mpmc_single_pop() {
    auto ring = std::make_unique<MpmcLarge>();
    // Pre-fill enough items to drain through the bench iterations
    // without ever hitting empty.
    for (Item i = 0; i < 4'000'000; ++i) {
        if (!ring->try_push(i)) break;
    }
    return bench::run("mpmc_ring.try_pop (single-call, prefilled)", [&]{
        auto v = ring->try_pop();
        bench::do_not_optimize(v);
    });
}

bench::Report mpmc_round_trip() {
    auto ring = std::make_unique<MpmcLarge>();
    Item i = 0;
    return bench::run("mpmc_ring round-trip (push+pop, single thread)", [&]{
        const bool ok = ring->try_push(++i);
        auto v = ring->try_pop();
        bench::do_not_optimize(ok);
        bench::do_not_optimize(v);
    });
}

// ── Inner-loop replication sweep ──────────────────────────────────────
//
// Calls try_push N times in a tight loop, divides by N.  Reveals
// per-call cost when the head/tail/threshold cache lines stay hot
// in the calling thread's L1.  Gap vs single-call shows whether the
// per-call atomic cost has any hidden invariant overhead that could
// be amortized.

template <std::size_t N>
bench::Report mpmc_push_inner_loop() {
    auto ring = std::make_unique<MpmcLarge>();
    Item i = 0;
    char name[128];
    std::snprintf(name, sizeof(name),
                  "mpmc_ring.try_push × %zu (inner loop, single thread)", N);
    return bench::run(name, [&]{
        for (std::size_t k = 0; k < N; ++k) {
            const bool ok = ring->try_push(++i);
            bench::do_not_optimize(ok);
        }
    });
}

template <std::size_t N>
bench::Report mpmc_round_trip_inner_loop() {
    auto ring = std::make_unique<MpmcLarge>();
    Item i = 0;
    char name[128];
    std::snprintf(name, sizeof(name),
                  "mpmc_ring round-trip × %zu (inner loop)", N);
    return bench::run(name, [&]{
        for (std::size_t k = 0; k < N; ++k) {
            const bool ok = ring->try_push(++i);
            auto v = ring->try_pop();
            bench::do_not_optimize(ok);
            bench::do_not_optimize(v);
        }
    });
}

// ── MPSC batched API ──────────────────────────────────────────────────
//
// The new try_push_batch / try_pop_batch primitives.  Single producer
// claims N tickets in ONE CAS, then publishes via N pure stores
// (no per-cell atomic).  Per-batch atomic cost amortizes across all
// items.

template <std::size_t N>
bench::Report mpsc_batched_push_pop() {
    auto ring = std::make_unique<MpscLarge>();
    alignas(64) static std::array<Item, N> tx{};
    alignas(64) static std::array<Item, N> rx{};
    for (std::size_t k = 0; k < N; ++k) tx[k] = k;
    char name[128];
    std::snprintf(name, sizeof(name),
                  "mpsc_ring batch<%zu> round-trip (push+pop)", N);
    return bench::run(name, [&]{
        const std::size_t np = ring->try_push_batch(std::span<const Item>(tx));
        const std::size_t nc = ring->try_pop_batch(std::span<Item>(rx));
        bench::do_not_optimize(np);
        bench::do_not_optimize(nc);
        bench::do_not_optimize(rx[0]);
        bench::do_not_optimize(rx[N - 1]);
    });
}

bench::Report mpsc_single_push_ref() {
    auto ring = std::make_unique<MpscLarge>();
    Item i = 0;
    return bench::run("mpsc_ring.try_push (single-call, REFERENCE)", [&]{
        const bool ok = ring->try_push(++i);
        bench::do_not_optimize(ok);
    });
}

// ── Bitmap-backed MPSC (the new design) ───────────────────────────────
//
// Pure-data cells + out-of-band bitmap.  No per-cell metadata.  Same
// 8-byte cell density as SPSC.

bench::Report bitmap_mpsc_single_push() {
    auto ring = std::make_unique<BitmapMpscLarge>();
    Item i = 0;
    return bench::run("bitmap_mpsc.try_push (single-call)", [&]{
        const bool ok = ring->try_push(++i);
        bench::do_not_optimize(ok);
    });
}

template <std::size_t N>
bench::Report bitmap_mpsc_batched_push_pop() {
    auto ring = std::make_unique<BitmapMpscLarge>();
    alignas(64) static std::array<Item, N> tx{};
    alignas(64) static std::array<Item, N> rx{};
    for (std::size_t k = 0; k < N; ++k) tx[k] = k;
    char name[128];
    std::snprintf(name, sizeof(name),
                  "bitmap_mpsc batch<%zu> round-trip (push+pop)", N);
    return bench::run(name, [&]{
        const std::size_t np = ring->try_push_batch(std::span<const Item>(tx));
        const std::size_t nc = ring->try_pop_batch(std::span<Item>(rx));
        bench::do_not_optimize(np);
        bench::do_not_optimize(nc);
        bench::do_not_optimize(rx[0]);
        bench::do_not_optimize(rx[N - 1]);
    });
}

// ── SPSC cross-reference at matched batch sizes ───────────────────────
//
// The SpscRing equivalent — both single-call and BATCHED.  Single-
// call SPSC measures the atomic-pair floor (one acquire-load + one
// release-store, no CAS).  Batched SPSC measures the L1d-port floor
// (one head/tail update covering N cells).  The gap MpmcRing → SPSC
// single-call → SPSC batched is the upper bound on batched-MPMC
// payoff.

bench::Report spsc_single_push() {
    auto ring = std::make_unique<SpscLarge>();
    Item i = 0;
    return bench::run("spsc_ring.try_push (single-call, REFERENCE)", [&]{
        const bool ok = ring->try_push(++i);
        bench::do_not_optimize(ok);
    });
}

template <std::size_t N>
bench::Report spsc_batched_push_pop() {
    auto ring = std::make_unique<SpscLarge>();
    alignas(64) static std::array<Item, N> tx{};
    alignas(64) static std::array<Item, N> rx{};
    for (std::size_t k = 0; k < N; ++k) tx[k] = k;
    char name[128];
    std::snprintf(name, sizeof(name),
                  "spsc_ring batch<%zu> round-trip (REFERENCE)", N);
    return bench::run(name, [&]{
        const std::size_t np = ring->try_push_batch(std::span{tx});
        const std::size_t nc = ring->try_pop_batch(std::span{rx});
        bench::do_not_optimize(np);
        bench::do_not_optimize(nc);
        bench::do_not_optimize(rx[0]);
        bench::do_not_optimize(rx[N - 1]);
    });
}

inline double per_item_ns(double whole_p50_ns, std::size_t N) noexcept {
    return whole_p50_ns / static_cast<double>(N);
}

inline double per_item_round_trip_ns(double whole_p50_ns,
                                      std::size_t N) noexcept {
    // Round-trip moves each item TWICE (push once, pop once).
    return whole_p50_ns / (2.0 * static_cast<double>(N));
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== mpmc_saturation ===\n");
    std::printf("  Item: uint64_t (8 bytes)\n");
    std::printf("  Ring: MpmcRing<uint64_t, 1M>\n");
    std::printf("  Reference: SpscRing<uint64_t, 1M>\n\n");

    bench::Report reports[] = {
        // ── Single-call MPMC baselines ────────────────────────────────
        mpmc_single_push(),
        mpmc_single_pop(),
        mpmc_round_trip(),

        // ── MPMC inner-loop sweep (single thread) ─────────────────────
        // Push-only at increasing inner-loop depth.  Per-item cost
        // should plateau quickly — there's no batching opportunity
        // in single-thread without a batched API.
        mpmc_push_inner_loop<1>(),
        mpmc_push_inner_loop<4>(),
        mpmc_push_inner_loop<16>(),
        mpmc_push_inner_loop<64>(),
        mpmc_push_inner_loop<256>(),
        mpmc_push_inner_loop<1024>(),

        // ── MPMC round-trip inner-loop sweep ──────────────────────────
        mpmc_round_trip_inner_loop<1>(),
        mpmc_round_trip_inner_loop<4>(),
        mpmc_round_trip_inner_loop<16>(),
        mpmc_round_trip_inner_loop<64>(),
        mpmc_round_trip_inner_loop<256>(),
        mpmc_round_trip_inner_loop<1024>(),

        // ── MPSC batched API — the load-bearing test of "batched
        //    MPMC can approach SPSC throughput" ────────────────────────
        mpsc_single_push_ref(),                  // [15] ← was spsc_single_push
        mpsc_batched_push_pop<64>(),             // [16]
        mpsc_batched_push_pop<256>(),            // [17]
        mpsc_batched_push_pop<1024>(),           // [18]

        // ── SPSC reference points ─────────────────────────────────────
        spsc_single_push(),                      // [19]
        spsc_batched_push_pop<64>(),             // [20]
        spsc_batched_push_pop<256>(),            // [21]
        spsc_batched_push_pop<1024>(),           // [22]

        // ── BitmapMpscRing — the new design (post-Vyukov) ─────────────
        bitmap_mpsc_single_push(),               // [23]
        bitmap_mpsc_batched_push_pop<64>(),      // [24]
        bitmap_mpsc_batched_push_pop<256>(),     // [25]
        bitmap_mpsc_batched_push_pop<1024>(),    // [26]
    };

    bench::emit_reports_text(reports);

    // ── Headline tables ───────────────────────────────────────────────
    std::printf("\n=== MPMC per-call cost ===\n");
    std::printf("\n  %-58s  %12s\n", "Bench", "ns/item");
    std::printf("  %-58s  %12s\n",
                std::string(58, '-').c_str(),
                std::string(12, '-').c_str());

    auto print_one = [](const bench::Report& r, double per_item) {
        std::printf("  %-58s  %10.3f ns\n", r.name.c_str(), per_item);
    };

    print_one(reports[0], reports[0].pct.p50);
    print_one(reports[1], reports[1].pct.p50);
    print_one(reports[2], reports[2].pct.p50 / 2.0);  // round-trip = 2 ops
    std::printf("\n  ── push inner-loop sweep (per-call cost) ──\n");
    const std::size_t push_ns[] = {1, 4, 16, 64, 256, 1024};
    for (std::size_t k = 0; k < 6; ++k) {
        const auto& r = reports[3 + k];
        print_one(r, per_item_ns(r.pct.p50, push_ns[k]));
    }
    std::printf("\n  ── round-trip inner-loop sweep (per-item moved) ──\n");
    for (std::size_t k = 0; k < 6; ++k) {
        const auto& r = reports[9 + k];
        print_one(r, per_item_round_trip_ns(r.pct.p50, push_ns[k]));
    }

    std::printf("\n=== MPSC batched API — the headline test ===\n");
    std::printf("\n  %-58s  %12s\n", "Bench", "ns/item");
    std::printf("  %-58s  %12s\n",
                std::string(58, '-').c_str(),
                std::string(12, '-').c_str());
    print_one(reports[15], reports[15].pct.p50);                    // mpsc single
    print_one(reports[16], per_item_round_trip_ns(reports[16].pct.p50, 64));
    print_one(reports[17], per_item_round_trip_ns(reports[17].pct.p50, 256));
    print_one(reports[18], per_item_round_trip_ns(reports[18].pct.p50, 1024));

    std::printf("\n=== SPSC reference (the L1d-port floor) ===\n");
    std::printf("\n  %-58s  %12s\n", "Bench", "ns/item");
    std::printf("  %-58s  %12s\n",
                std::string(58, '-').c_str(),
                std::string(12, '-').c_str());
    print_one(reports[19], reports[19].pct.p50);
    print_one(reports[20], per_item_round_trip_ns(reports[20].pct.p50, 64));
    print_one(reports[21], per_item_round_trip_ns(reports[21].pct.p50, 256));
    print_one(reports[22], per_item_round_trip_ns(reports[22].pct.p50, 1024));

    // ── Headline: MPSC single → MPSC batched → SPSC batched ──────────
    std::printf("\n=== headline: batched MPSC vs single-call MPSC vs SPSC ===\n");
    {
        const double mpmc_push       = reports[0].pct.p50;
        const double mpmc_inner_1024 = per_item_ns(reports[8].pct.p50, 1024);
        const double mpsc_single     = reports[15].pct.p50;
        const double mpsc_b64        = per_item_round_trip_ns(reports[16].pct.p50, 64);
        const double mpsc_b256       = per_item_round_trip_ns(reports[17].pct.p50, 256);
        const double mpsc_b1024      = per_item_round_trip_ns(reports[18].pct.p50, 1024);
        const double spsc_single     = reports[19].pct.p50;
        const double spsc_b1024      = per_item_round_trip_ns(reports[22].pct.p50, 1024);

        std::printf("  Single MpmcRing.try_push:        %.3f ns/op\n", mpmc_push);
        std::printf("  Single MpscRing.try_push:        %.3f ns/op\n", mpsc_single);
        std::printf("  Single SpscRing.try_push:        %.3f ns/op\n", spsc_single);
        std::printf("\n");
        std::printf("  MpmcRing inner-loop ×1024:       %.3f ns/item (no batched API)\n",
                    mpmc_inner_1024);
        std::printf("  MpscRing batched<64>:            %.3f ns/item (speedup vs single: %.1f×)\n",
                    mpsc_b64, mpsc_single / mpsc_b64);
        std::printf("  MpscRing batched<256>:           %.3f ns/item (speedup: %.1f×)\n",
                    mpsc_b256, mpsc_single / mpsc_b256);
        std::printf("  MpscRing batched<1024>:          %.3f ns/item (speedup: %.1f×)\n",
                    mpsc_b1024, mpsc_single / mpsc_b1024);
        std::printf("  SpscRing batched<1024>:          %.3f ns/item (the floor)\n",
                    spsc_b1024);
        std::printf("\n  ── interpretation (honest after measurement) ──\n");
        std::printf("  Single-thread MPSC batched<1024> reaches %.3f ns/item.\n",
                    mpsc_b1024);
        std::printf("  vs single-call MPSC %.2f ns: %.1f× speedup.\n",
                    mpsc_single, mpsc_single / mpsc_b1024);
        std::printf("  vs SPSC batched<1024> %.3f ns: %.1f× SLOWER.\n",
                    spsc_b1024, mpsc_b1024 / spsc_b1024);
        std::printf("\n  Why we don't reach SPSC throughput:\n");
        std::printf("  • Each MpscRing Cell is alignas(64) → 1024 cells touch\n");
        std::printf("    64 KB of cache lines (vs SPSC's 8 KB) → exceeds L1d\n");
        std::printf("    (32 KB on Zen 3) → cache-line bandwidth dominates.\n");
        std::printf("  • Per-cell publish (data + sequence release-store) costs\n");
        std::printf("    2× the store-buffer pressure of SPSC's single data write.\n");
        std::printf("\n  The user's challenge — \"can MPMC be a special case of\n");
        std::printf("  SPSC at full speed\" — is partly true:\n");
        std::printf("  • The ALGORITHM scales (1 CAS per N items, N pure stores).\n");
        std::printf("  • The CACHE LAYOUT does not (64-byte cells × N items).\n");
        std::printf("  Closing the gap requires packed 16-byte cells (loses\n");
        std::printf("  false-sharing protection across producer batches) OR a\n");
        std::printf("  different algorithm without per-cell metadata.\n");
        std::printf("\n  Real win for THIS API: multi-producer contention.\n");
        std::printf("  Single FAA(tail, N) replaces N × FAA(tail, 1).  Bench\n");
        std::printf("  this with multi-thread harness (separate task) to see.\n");
        (void)mpmc_push;
        (void)mpmc_inner_1024;
    }

    // ── Bitmap-MPSC headline (the post-Vyukov design) ─────────────────
    std::printf("\n=== BitmapMpscRing — out-of-band bitmap design ===\n");
    std::printf("\n  %-58s  %12s\n", "Bench", "ns/item");
    std::printf("  %-58s  %12s\n",
                std::string(58, '-').c_str(),
                std::string(12, '-').c_str());
    {
        auto print_one = [](const bench::Report& r, double per_item) {
            std::printf("  %-58s  %10.3f ns\n", r.name.c_str(), per_item);
        };
        print_one(reports[23], reports[23].pct.p50);
        print_one(reports[24], per_item_round_trip_ns(reports[24].pct.p50, 64));
        print_one(reports[25], per_item_round_trip_ns(reports[25].pct.p50, 256));
        print_one(reports[26], per_item_round_trip_ns(reports[26].pct.p50, 1024));

        const double bm_single   = reports[23].pct.p50;
        const double bm_b1024    = per_item_round_trip_ns(reports[26].pct.p50, 1024);
        const double mpsc_b1024  = per_item_round_trip_ns(reports[18].pct.p50, 1024);
        const double spsc_b1024  = per_item_round_trip_ns(reports[22].pct.p50, 1024);

        std::printf("\n  ── BitmapMpscRing vs the field ──\n");
        std::printf("  BitmapMpsc batched<1024>:        %.3f ns/item\n", bm_b1024);
        std::printf("  Vyukov MpscRing batched<1024>:   %.3f ns/item  (gap: %.2f×)\n",
                    mpsc_b1024, mpsc_b1024 / bm_b1024);
        std::printf("  SpscRing batched<1024> (floor):  %.3f ns/item  (gap: %.2f×)\n",
                    spsc_b1024, bm_b1024 / spsc_b1024);
        std::printf("  Single Bitmap.try_push:          %.3f ns/op\n", bm_single);
        std::printf("\n  Out-of-band bitmap moves metadata OUT of the cell:\n");
        std::printf("  cells stay packed at sizeof(T) (= 8B for u64), bitmap\n");
        std::printf("  is 1 bit per cell (= 128 B for 1024 cells).  Working\n");
        std::printf("  set: 8.125 KB vs Vyukov's 64 KB.  Fits L1d.\n");
    }

    bench::emit_reports_json(reports, json);
    return 0;
}
