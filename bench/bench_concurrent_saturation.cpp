// SpscRing saturation sweep — find the L1d-port-bound floor.
//
// Sweeps batch size 1 → 1024 over try_push_batch + try_pop_batch
// round-trips.  Reports per-item cost as a function of N to find:
//   1. The elbow where atomic overhead (3 atomics per batch call)
//      stops dominating per-item cost.
//   2. The plateau where L1d store-port throughput caps further
//      improvement.
//   3. The ratio vs a raw std::memcpy of equivalent byte size
//      (the absolute "no-bookkeeping" reference).
//
// Hardware ceiling on Zen 3 (AMD Ryzen 9 5950X, 4.6 GHz boost):
//   - L1d ports: 2 load + 1 store = 3 mem-ops/cycle
//   - AVX2 stores: 32 B/cycle = ~100 GB/s store-buffer throughput
//   - For 8-byte uint64 round-trip (1 store push + 1 load + 1 store
//     pop = 3 mem-ops per item), the L1d-port floor is:
//       1 item × 3 mem-ops/item × 1 cycle / 3 mem-ops × 0.217 ns/cycle
//       ≈ 0.072 ns/item
//   - Below that floor, we'd need to move bytes faster than the
//     CPU can issue mem-ops to L1d.  Not possible without writing
//     to a different cache level (L2/L3 store-streaming) or
//     bypassing cache (non-temporal stores, MOVNTPD).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <fixy/Core.h>
#include <fixy/concurrent/SpscRing.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include "bench_harness.h"

namespace {

using ::fixy::concurrent::SpscRing;

// Item type: 8-byte uint64 — the smallest practical SPSC payload.
using Item = std::uint64_t;

// The rings of the rows live on the heap: a ring of 1M slots holds 8 MiB.
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// Batch capacity must be ≥ 1024 so we can pre-fill once and never wrap.
template <std::size_t N>
struct SaturationCase {
    using Ring = SpscRing<Item, (1U << 20)>;  // 1M slots — never fills

    static bench::Report run() {
        ::fixy::Box<Ring> ring_box = ::fixy::mint_box<Ring>(kAlloc);
        // The body reads the ring through a Ref, which holds a pointer that
        // is never null, so the timed loop tests no empty Box.
        auto const ring = ::fixy::to(*ring_box);
        alignas(64) static ::fixy::FixedArray<Item, N> tx{};
        alignas(64) static ::fixy::FixedArray<Item, N> rx{};
        for (::fixy::Index<N> const index : tx.indices())
            tx.with_element(index, [index](Item& element) noexcept { element = index.raw(); });

        return bench::run(bench::format_name("spsc_ring batch<{}> round-trip (push+pop)", N), [&] {
            const std::size_t np = ring->try_push_batch(tx.view());
            const std::size_t nc = rx.with_mut([&ring](auto run) noexcept { return ring->try_pop_batch(run.view()); });
            bench::do_not_optimize(np);
            bench::do_not_optimize(nc);
            // The sink reads and writes each byte of rx, so the copy of the
            // pop into rx stays, and no other code reads rx.  A sink of one
            // element would keep only the bytes of that element (THE SINK
            // of bench_harness.h).
            bench::mark_changed(rx);
        });
    }
};

}  // namespace

// Reference: a raw memcpy of N×8B with no atomics, no SPSC bookkeeping.
// Establishes the absolute "no-overhead" floor for moving N items
// between two buffers.  Round-trip = 2 memcpys (push + pop direction).
// The reference moves plain arrays with the memcpy of the C library, as
// the hardware sees it, so it stays raw.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a raw memcpy of plain arrays is the floor reference of the ring")
namespace {

template <std::size_t N>
bench::Report raw_memcpy_roundtrip() {
    alignas(64) static std::array<Item, N> tx{};
    alignas(64) static std::array<Item, N> mid{};
    alignas(64) static std::array<Item, N> rx{};
    for (std::size_t k = 0; k < N; ++k)
        tx[k] = k;

    return bench::run(bench::format_name("raw_memcpy<{}> round-trip (tx→mid→rx)", N), [&] {
        std::memcpy(mid.data(), tx.data(), N * sizeof(Item));
        std::memcpy(rx.data(), mid.data(), N * sizeof(Item));
        // The sink reads and writes each byte of mid and of rx, as for the
        // ring above, so neither copy is dead.  With no sink, both copies
        // went: a measure gave 76 TB/s, which no host moves.
        bench::mark_changed(mid);
        bench::mark_changed(rx);
    });
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// Per-item ns from a whole-batch round-trip p50.
// Round-trip = N pushes + N pops = 2N items moved.
inline double per_item_ns(double whole_batch_p50_ns, std::size_t N) noexcept {
    return whole_batch_p50_ns / (2.0 * static_cast<double>(N));
}

// Per-item bytes/sec throughput for an Item-sized round-trip.
// Round-trip moves 2 × N × sizeof(Item) bytes.
inline double bytes_per_sec(double whole_batch_p50_ns, std::size_t N) noexcept {
    const double ops = 2.0 * static_cast<double>(N);
    const double bytes = ops * static_cast<double>(sizeof(Item));
    const double sec = whole_batch_p50_ns * 1e-9;
    return bytes / sec;
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    using ::fixy::Align;
    using ::fixy::Fixed;
    using ::fixy::Padded;
    using ::fixy::TextView;

    bench::print("=== concurrent_saturation ===\n");
    bench::print("  Item: uint64_t (8 bytes)\n");
    bench::print("  Ring: SpscRing<uint64_t, 1M>\n");
    bench::print("\n");

    ::fixy::FixedArray<bench::Report, 15> reports{
        // Single-call SPSC — N=1 baseline.
        [&] {
            ::fixy::Box<SpscRing<Item, (1U << 20)>> ring_box = ::fixy::mint_box<SpscRing<Item, (1U << 20)>>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            std::uint64_t i = 0;
            return bench::run("spsc_ring.try_push (N=1, single-call)", [&] {
                const bool ok = ring->try_push(++i);
                bench::do_not_optimize(ok);
            });
        }(),

        // Sweep: round-trip batched push+pop, doubling N from 1 → 1024.
        SaturationCase<1>::run(),
        SaturationCase<2>::run(),
        SaturationCase<4>::run(),
        SaturationCase<8>::run(),
        SaturationCase<16>::run(),
        SaturationCase<32>::run(),
        SaturationCase<64>::run(),
        SaturationCase<128>::run(),
        SaturationCase<256>::run(),
        SaturationCase<512>::run(),
        SaturationCase<1024>::run(),

        // Raw memcpy reference at matched batch sizes (the absolute
        // ceiling — pure L1d store-buffer bandwidth, no atomics).
        raw_memcpy_roundtrip<64>(),
        raw_memcpy_roundtrip<256>(),
        raw_memcpy_roundtrip<1024>(),
    };

    bench::emit_reports_text(reports.view());

    // ── Per-item cost table (the headline) ────────────────────────────
    bench::print("\n=== saturation table (per-item cost) ===\n");
    bench::print("\n  {}  {}  {}\n", Padded<50, Align::Left>{TextView{"Bench"}}, Padded<12>{TextView{"ns/item"}},
                 Padded<12>{TextView{"GB/s"}});
    constexpr TextView kDashes12{"------------"};
    bench::print("  {}  {}  {}\n", TextView{"--------------------------------------------------"}, kDashes12,
                 kDashes12);

    auto print_row = [](const bench::Report& r, std::size_t batch_items) {
        const double per_item = per_item_ns(r.pct.p50, batch_items);
        const double gbps = bytes_per_sec(r.pct.p50, batch_items) / 1e9;
        bench::print("  {}  {} ns  {} GB/s\n", Padded<50, Align::Left>{r.name.view()}, Fixed<4, 10>{per_item},
                     Fixed<2, 8>{gbps});
    };

    // Single-call: N=1
    print_row(reports.at(::fixy::index<0>()), 1);
    // Sweep: indices 1..12 = N = 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024
    print_row(reports.at(::fixy::index<1>()), 1);
    print_row(reports.at(::fixy::index<2>()), 2);
    print_row(reports.at(::fixy::index<3>()), 4);
    print_row(reports.at(::fixy::index<4>()), 8);
    print_row(reports.at(::fixy::index<5>()), 16);
    print_row(reports.at(::fixy::index<6>()), 32);
    print_row(reports.at(::fixy::index<7>()), 64);
    print_row(reports.at(::fixy::index<8>()), 128);
    print_row(reports.at(::fixy::index<9>()), 256);
    print_row(reports.at(::fixy::index<10>()), 512);
    print_row(reports.at(::fixy::index<11>()), 1024);
    // Raw memcpy reference: matched N
    print_row(reports.at(::fixy::index<12>()), 64);
    print_row(reports.at(::fixy::index<13>()), 256);
    print_row(reports.at(::fixy::index<14>()), 1024);

    // ── L1d-port ceiling theory vs observed ───────────────────────────
    bench::print("\n=== Zen 3 L1d-port ceiling analysis ===\n");
    {
        // Round-trip per item = 1 store (push) + 1 load + 1 store (pop)
        // = 3 mem-ops.  L1d ports = 3 mem-ops/cycle on Zen 3.
        // Floor = 1 item × 1 cycle / 3 mem-ops_per_cycle × 3 mem-ops/item
        //        = 1 cycle / item ?  No — 3 mem-ops dispatched per cycle,
        //        spread across all in-flight items.  So 1 item per cycle
        //        is the sustained throughput floor for fully-pipelined ops.
        //
        // But each item IS 3 mem-ops and Zen 3 retires 3 mem-ops/cycle,
        // so one item per cycle.  At 4.6 GHz:
        //   1 item / 4.6e9 s = 0.217 ns/item.
        const double ns_floor_per_item = 0.217;
        bench::print("  Theoretical L1d-port floor (Zen 3 @ 4.6 GHz):\n");
        bench::print("    1 item per cycle × 0.217 ns/cycle = {} ns/item\n", Fixed<3>{ns_floor_per_item});
        bench::print("  Largest observed (N=1024 batch round-trip):\n");
        const double obs = per_item_ns(reports.at(::fixy::index<11>()).pct.p50, 1024);
        bench::print("    {} ns/item — gap to ceiling = {}×\n", Fixed<3>{obs}, Fixed<2>{obs / ns_floor_per_item});
        bench::print("  Raw memcpy reference (N=1024, no atomics):\n");
        const double mref = per_item_ns(reports.at(::fixy::index<14>()).pct.p50, 1024);
        bench::print("    {} ns/item — pure-bandwidth floor\n", Fixed<3>{mref});
    }

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
