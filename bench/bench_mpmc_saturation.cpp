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

#include <crucible/concurrent/MpmcRing.h>
#include <crucible/concurrent/SpscRing.h>

#include "bench_harness.h"

namespace {

using crucible::concurrent::MpmcRing;
using crucible::concurrent::SpscRing;

using Item = std::uint64_t;

// 1M-slot ring — never fills under any single-thread bench.
using MpmcLarge = MpmcRing<Item, (1U << 20)>;
using SpscLarge = SpscRing<Item, (1U << 20)>;

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

        // ── SPSC reference points ─────────────────────────────────────
        spsc_single_push(),
        spsc_batched_push_pop<64>(),
        spsc_batched_push_pop<256>(),
        spsc_batched_push_pop<1024>(),
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

    std::printf("\n=== SPSC reference (the gap shows MPMC algorithmic cost) ===\n");
    std::printf("\n  %-58s  %12s\n", "Bench", "ns/item");
    std::printf("  %-58s  %12s\n",
                std::string(58, '-').c_str(),
                std::string(12, '-').c_str());
    print_one(reports[15], reports[15].pct.p50);
    print_one(reports[16], per_item_round_trip_ns(reports[16].pct.p50, 64));
    print_one(reports[17], per_item_round_trip_ns(reports[17].pct.p50, 256));
    print_one(reports[18], per_item_round_trip_ns(reports[18].pct.p50, 1024));

    // ── Headline: gap to SPSC + batched-API opportunity ───────────────
    std::printf("\n=== gap to SPSC reference ===\n");
    {
        const double mpmc_push   = reports[0].pct.p50;
        const double mpmc_inner  = per_item_ns(reports[8].pct.p50, 1024);
        const double spsc_push   = reports[15].pct.p50;
        const double spsc_b1024  = per_item_round_trip_ns(reports[18].pct.p50,
                                                          1024);

        std::printf("  Single MpmcRing.try_push:     %.3f ns\n", mpmc_push);
        std::printf("  Single SpscRing.try_push:     %.3f ns  (gap: %.1f×)\n",
                    spsc_push, mpmc_push / spsc_push);
        std::printf("  MpmcRing inner-loop ×1024:    %.3f ns/item\n",
                    mpmc_inner);
        std::printf("  SpscRing batched<1024>:       %.3f ns/item  (gap: %.1f×)\n",
                    spsc_b1024, mpmc_inner / spsc_b1024);
        std::printf("\n  ── interpretation ──\n");
        std::printf("  MPMC has NO batched API today.  Inner-loop ×1024 still\n");
        std::printf("  pays one FAA+CAS per item.  A future try_push_batch<N>\n");
        std::printf("  primitive could amortize FAA contention to ~1 per batch,\n");
        std::printf("  bringing per-item cost toward the SPSC batched floor of\n");
        std::printf("  %.3f ns/item.  Maximum theoretical batched-MPMC payoff:\n",
                    spsc_b1024);
        std::printf("  ~%.1f× speedup vs current MPMC inner-loop cost.\n",
                    mpmc_inner / spsc_b1024);
    }

    bench::emit_reports_json(reports, json);
    return 0;
}
