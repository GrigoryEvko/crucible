// MpscRing multi-producer contention bench.
//
// Validates the claim of the batch push of the ring:
//
//   "Real win for THIS API: multi-producer contention.
//    Single FAA(tail, N) replaces N × FAA(tail, 1)."
//
// A single-thread measurement showed a ~4.5× speedup from the batched
// API.  This bench shows the multi-thread story:
//   • Without batched API: every try_push contends on head_ via CAS.
//     Per-call cost grows with producer count.
//   • With batched API: each batch contends on head_ ONCE per N items
//     (single CAS claims N tickets).  Contention reduction = N×.
//
// Harness discipline (canonical bench/bench_harness.h):
//   • bench::Run{...}.samples(...).warmup(...).max_wall_ms(...).measure(body)
//     drives N iterations of a self-contained workload cycle.
//   • One iteration body = spawn P producers + drain consumer + join.
//   • Report.pct gives p50/p99/p99.9/cv of full-workload wall time.
//   • bench::emit_reports_text + bench::emit_reports_json publish.
//   • bench::compare(single, batched<64>) gives Mann-Whitney U for
//     statistical significance of the contention-reduction claim.
//
// The body is heavy (P jthread spawns + join), so we tune for low
// sample count and large per-sample work:
//   • samples(20)     — enough for p50/p99.9
//   • warmup(2)       — flush any first-call jthread/futex overhead
//   • batch(1)        — never auto-batch a multi-thread cycle
//   • no_pin()        — multi-thread can't pin to one core
//   • max_wall_ms(60_000) — generous wall budget; cap protects CI
//   • per_producer = 5'000 — keeps each iteration ≈ 1-50 ms

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <fixy/Core.h>
#include <fixy/concurrent/MpscRing.h>

#include "bench_harness.h"

namespace {

using ::fixy::concurrent::MpscRing;
using Item = std::uint64_t;

constexpr std::size_t kCap = 1U << 14;  // 16K cells; headroom
constexpr std::size_t kPerProducer = 5000;  // items per producer / iter
constexpr std::size_t kSamples = 32;  // ≥30 for Mann-Whitney
constexpr std::size_t kWarmup = 3;
constexpr std::size_t kMaxWallMs = 60000;

}  // namespace

// The rows time the bare ring, which holds no permission of its two sides.
// The thread doors of the base refuse a borrow of such a ring beside the
// caller, so each cycle starts its producers and its consumer on
// std::jthread, as the rows of the bare rings of bench_thread_scaling do.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: each cycle of the bare MPSC ring runs its threads on std::jthread")
namespace {

// Producer-side single-call write loop.  Each producer pushes K items
// via individual try_push calls.  Spin-yield if full.
struct SinglePushWorker {
    MpscRing<Item, kCap>& ring;
    std::atomic<bool>& start;
    Item base;

    void operator()(std::stop_token) noexcept {
        while (!start.load(std::memory_order_acquire)) {
            CRUCIBLE_SPIN_PAUSE;
        }
        for (std::size_t i = 0; i < kPerProducer; ++i) {
            const Item value = base + static_cast<Item>(i);
            while (!ring.try_push(value)) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    }
};

// Producer-side batched write loop.  Each producer pushes K items via
// try_push_batch<BATCH> calls — amortizes head_ CAS contention.
template <std::size_t BATCH>
struct BatchedPushWorker {
    MpscRing<Item, kCap>& ring;
    std::atomic<bool>& start;
    Item base;

    void operator()(std::stop_token) noexcept {
        while (!start.load(std::memory_order_acquire)) {
            CRUCIBLE_SPIN_PAUSE;
        }
        ::fixy::FixedArray<Item, BATCH> buf{};
        std::size_t pushed = 0;
        while (pushed < kPerProducer) {
            const std::size_t remaining = kPerProducer - pushed;
            const std::size_t n = (remaining < BATCH) ? remaining : BATCH;
            for (std::size_t i = 0; i < n; ++i) {
                *buf.get(i).expect("a position of a batch is below BATCH") = base + (pushed + i);
            }
            std::size_t in_batch = 0;
            while (in_batch < n) {
                const std::size_t r = ring.try_push_batch(
                    buf.view().window(in_batch, n - in_batch).expect("the rest of a batch lies inside it"));
                if (r > 0) {
                    in_batch += r;
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            pushed += n;
        }
    }
};

// One full multi-thread cycle: spawn P producers, drain consumer,
// join.  Returned to bench::Run as the body callable.
template <typename Worker>
void run_cycle(std::size_t producer_count) {
    auto ring_ptr = std::make_unique<MpscRing<Item, kCap>>();
    auto& ring = *ring_ptr;

    const std::size_t total = producer_count * kPerProducer;
    std::atomic<bool> start{false};
    std::atomic<std::size_t> consumed{0};

    // Spawn producers — Worker constructed in-place with refs.
    std::vector<std::jthread> producers;
    producers.reserve(producer_count);
    for (std::size_t p = 0; p < producer_count; ++p) {
        const Item base = static_cast<Item>(p) * kPerProducer;
        producers.emplace_back(Worker{ring, start, base});
    }

    // Spawn consumer.  Drains until total items observed.
    std::jthread consumer([&ring, &consumed, &start, total](std::stop_token) noexcept {
        while (!start.load(std::memory_order_acquire)) {
            CRUCIBLE_SPIN_PAUSE;
        }
        ::fixy::FixedArray<Item, 64> buf{};
        std::size_t local = 0;
        // A pop of an MPSC ring is a merge, so it takes the tag of Nondet.
        const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
        while (local < total) {
            const std::size_t n = ring.try_pop_batch(merge, buf.view());
            if (n == 0) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            // Anti-DCE: keep the read alive.
            bench::do_not_optimize(buf.at(::fixy::index<0>()));
            bench::do_not_optimize(*buf.get(n - 1).expect("a pop gives at most the count of the buffer"));
            local += n;
        }
        consumed.store(local, std::memory_order_release);
    });

    // Release the start gate — measured wall begins at the FIRST
    // producer's first push and ends at the consumer's join.
    start.store(true, std::memory_order_release);
    producers.clear();  // join all producers
    consumer.join();
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

template <typename Worker>
[[nodiscard]] bench::Report run_one(::fixy::TextView label, std::size_t producer_count) {
    return bench::Run{bench::format_name("{} P={}", label, producer_count)}
        .samples(kSamples)
        .warmup(kWarmup)
        .batch(1)
        .no_pin()
        .max_wall_ms(kMaxWallMs)
        .measure([producer_count] { run_cycle<Worker>(producer_count); });
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== mpsc_contention ===\n");
    bench::print("  Item:              uint64_t\n");
    bench::print("  Cap:               {} cells\n", kCap);
    bench::print("  Items per producer: {} / iteration\n", kPerProducer);
    bench::print("  Samples:           {} (warmup {})\n", kSamples, kWarmup);
    bench::print("  Body:              spawn P producers + drain consumer + join\n");
    bench::print("\n");

    // 4 producer counts × 3 API kinds = 12 cells.
    ::fixy::FixedList<bench::Report, 12> reports{};
    ::fixy::FixedArray<std::size_t, 4> const producer_counts{1, 2, 4, 8};
    for (std::size_t const P : producer_counts.view()) {
        bench::add_report(reports, run_one<SinglePushWorker>("single", P));
        bench::add_report(reports, run_one<BatchedPushWorker<16>>("batched<16>", P));
        bench::add_report(reports, run_one<BatchedPushWorker<64>>("batched<64>", P));
    }

    // Standard text + JSON emission.  Each Report's pct.p50 is the
    // wall-time of one workload cycle (P producers × kPerProducer items).
    bench::emit_reports_text(reports.view());

    // ── Headline analysis: per-producer-count items/sec ─────────────
    //
    // Items moved per cycle = P × kPerProducer; cycle wall = pct.p50.
    // Aggregate items/sec = items / wall.
    bench::print("\n=== contention reduction (single → batched<64>) ===\n");
    bench::print("  P    single (Mitems/s)    batched<64> (Mitems/s)   speedup\n");
    bench::print("  --   ------------------  ------------------------  -------\n");
    for (std::size_t pi = 0; pi < 4; ++pi) {
        const std::size_t P = std::size_t{1} << pi;  // 1, 2, 4, 8
        const bench::Report& s = *reports.get(pi * 3 + 0).expect("the single row of a producer count");
        const bench::Report& b64 = *reports.get(pi * 3 + 2).expect("the batched<64> row of a producer count");
        const double items = static_cast<double>(P * kPerProducer);
        const double s_rate = items / (s.pct.p50 * 1e-9);
        const double b_rate = items / (b64.pct.p50 * 1e-9);
        const double ratio = b_rate / s_rate;
        bench::print("  {}      {}      {}       {}×\n", ::fixy::Padded<2>{P}, ::fixy::Fixed<2, 14>{s_rate / 1e6},
                     ::fixy::Fixed<2, 18>{b_rate / 1e6}, ::fixy::Fixed<2, 5>{ratio});
    }

    // ── Statistical significance: Mann-Whitney U sweep across P ─────
    //
    // bench::compare gives Mann-Whitney U + tie-corrected z; bench
    // marks distinguishable iff |z| > 2.576 (p < 0.01).  We expect
    // batched<64> to IMPROVE over single at every contention level.
    bench::print("\n=== significance (single vs batched<64>) ===\n");
    for (std::size_t pi = 0; pi < 4; ++pi) {
        const bench::Report& s = *reports.get(pi * 3 + 0).expect("the single row of a producer count");
        const bench::Report& b64 = *reports.get(pi * 3 + 2).expect("the batched<64> row of a producer count");
        bench::compare(s, b64).print_text();
    }

    bench::print("\n  Interpretation:\n");
    bench::print("  • At P=1 (no contention), batched API gives the single-thread\n");
    bench::print("    speedup (~4.5×).\n");
    bench::print("  • At P=2..8 (contention rises), the gap widens: batched API\n");
    bench::print("    does 1 CAS per N items, so head_ contention is reduced N×\n");
    bench::print("    vs single-call which CASes per item.\n");
    bench::print("  • CV >5% on any cell indicates throttling — re-run on a\n");
    bench::print("    quiet machine and check `dmesg | grep thermal`.\n");

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
