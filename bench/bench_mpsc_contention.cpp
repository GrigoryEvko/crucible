// MpscRing multi-producer contention bench.
//
// Validates the post-Vyukov bitmap design's claim from the commit:
//
//   "Real win for THIS API: multi-producer contention.
//    Single FAA(tail, N) replaces N × FAA(tail, 1)."
//
// Single-thread bench (bench_mpmc_saturation) showed ~4.5× speedup
// from the batched API.  This bench shows the multi-thread story:
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
#include <cstdio>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include <crucible/concurrent/MpscRing.h>

#include "bench_harness.h"

namespace {

using crucible::concurrent::MpscRing;
using Item = std::uint64_t;

constexpr std::size_t kCap            = 1U << 14;   // 16K cells; headroom
constexpr std::size_t kPerProducer    = 5'000;      // items per producer / iter
constexpr std::size_t kSamples        = 32;     // ≥30 for Mann-Whitney
constexpr std::size_t kWarmup         = 3;
constexpr std::size_t kMaxWallMs      = 60'000;

// Producer-side single-call write loop.  Each producer pushes K items
// via individual try_push calls.  Spin-yield if full.
struct SinglePushWorker {
    MpscRing<Item, kCap>& ring;
    std::atomic<bool>&    start;
    Item                  base;

    void operator()(std::stop_token) noexcept {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t i = 0; i < kPerProducer; ++i) {
            const Item value = base + static_cast<Item>(i);
            while (!ring.try_push(value)) {
                std::this_thread::yield();
            }
        }
    }
};

// Producer-side batched write loop.  Each producer pushes K items via
// try_push_batch<BATCH> calls — amortizes head_ CAS contention.
template <std::size_t BATCH>
struct BatchedPushWorker {
    MpscRing<Item, kCap>& ring;
    std::atomic<bool>&    start;
    Item                  base;

    void operator()(std::stop_token) noexcept {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::array<Item, BATCH> buf{};
        std::size_t pushed = 0;
        while (pushed < kPerProducer) {
            const std::size_t remaining = kPerProducer - pushed;
            const std::size_t n = (remaining < BATCH) ? remaining : BATCH;
            for (std::size_t i = 0; i < n; ++i) {
                buf[i] = base + static_cast<Item>(pushed + i);
            }
            std::size_t in_batch = 0;
            while (in_batch < n) {
                const std::size_t r = ring.try_push_batch(
                    std::span<const Item>(buf.data() + in_batch, n - in_batch));
                if (r > 0) {
                    in_batch += r;
                } else {
                    std::this_thread::yield();
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
    std::jthread consumer([&ring, &consumed, &start, total](
            std::stop_token) noexcept {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::array<Item, 64> buf{};
        std::size_t local = 0;
        while (local < total) {
            const std::size_t n = ring.try_pop_batch(std::span<Item>(buf));
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            // Anti-DCE: keep the read alive.
            bench::do_not_optimize(buf[0]);
            bench::do_not_optimize(buf[n - 1]);
            local += n;
        }
        consumed.store(local, std::memory_order_release);
    });

    // Release the start gate — measured wall begins at the FIRST
    // producer's first push and ends at the consumer's join.
    start.store(true, std::memory_order_release);
    producers.clear();   // join all producers
    consumer.join();
}

template <typename Worker>
[[nodiscard]] bench::Report run_one(const char* label,
                                    std::size_t producer_count) {
    char name[96];
    std::snprintf(name, sizeof(name), "%s P=%zu", label, producer_count);
    return bench::Run{name}
        .samples(kSamples)
        .warmup(kWarmup)
        .batch(1)
        .no_pin()
        .max_wall_ms(kMaxWallMs)
        .measure([producer_count]{ run_cycle<Worker>(producer_count); });
}

}  // namespace

int main(int argc, char** argv) {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = (argc > 1 && std::string_view{argv[1]} == "--json");

    std::printf("=== mpsc_contention ===\n");
    std::printf("  Item:              uint64_t\n");
    std::printf("  Cap:               %zu cells\n", kCap);
    std::printf("  Items per producer: %zu / iteration\n", kPerProducer);
    std::printf("  Samples:           %zu (warmup %zu)\n", kSamples, kWarmup);
    std::printf("  Body:              spawn P producers + drain consumer + join\n");
    std::printf("\n");

    std::vector<bench::Report> reports;
    reports.reserve(12);

    // 4 producer counts × 3 API kinds = 12 cells.
    for (std::size_t P : {std::size_t{1}, std::size_t{2},
                          std::size_t{4}, std::size_t{8}}) {
        reports.push_back(run_one<SinglePushWorker>     ("single",      P));
        reports.push_back(run_one<BatchedPushWorker<16>>("batched<16>", P));
        reports.push_back(run_one<BatchedPushWorker<64>>("batched<64>", P));
    }

    // Standard text + JSON emission.  Each Report's pct.p50 is the
    // wall-time of one workload cycle (P producers × kPerProducer items).
    bench::emit_reports_text(reports);

    // ── Headline analysis: per-producer-count items/sec ─────────────
    //
    // Items moved per cycle = P × kPerProducer; cycle wall = pct.p50.
    // Aggregate items/sec = items / wall.
    std::printf("\n=== contention reduction (single → batched<64>) ===\n");
    std::printf("  P    single (Mitems/s)    batched<64> (Mitems/s)   speedup\n");
    std::printf("  --   ------------------  ------------------------  -------\n");
    for (std::size_t pi = 0; pi < 4; ++pi) {
        const std::size_t P = std::size_t{1} << pi;     // 1, 2, 4, 8
        const auto& s   = reports[pi * 3 + 0];          // single
        const auto& b64 = reports[pi * 3 + 2];          // batched<64>
        const double items     = static_cast<double>(P * kPerProducer);
        const double s_rate    = items / (s.pct.p50   * 1e-9);
        const double b_rate    = items / (b64.pct.p50 * 1e-9);
        const double ratio     = b_rate / s_rate;
        std::printf("  %2zu      %14.2f      %18.2f       %5.2f×\n",
                    P, s_rate / 1e6, b_rate / 1e6, ratio);
    }

    // ── Statistical significance: Mann-Whitney U sweep across P ─────
    //
    // bench::compare gives Mann-Whitney U + tie-corrected z; bench
    // marks distinguishable iff |z| > 2.576 (p < 0.01).  We expect
    // batched<64> to IMPROVE over single at every contention level.
    std::printf("\n=== significance (single vs batched<64>) ===\n");
    for (std::size_t pi = 0; pi < 4; ++pi) {
        const auto& s   = reports[pi * 3 + 0];
        const auto& b64 = reports[pi * 3 + 2];
        bench::compare(s, b64).print_text();
    }

    std::printf("\n  Interpretation:\n");
    std::printf("  • At P=1 (no contention), batched API gives the single-thread\n");
    std::printf("    speedup measured in bench_mpmc_saturation (~4.5×).\n");
    std::printf("  • At P=2..8 (contention rises), the gap widens: batched API\n");
    std::printf("    does 1 CAS per N items, so head_ contention is reduced N×\n");
    std::printf("    vs single-call which CASes per item.\n");
    std::printf("  • CV >5%% on any cell indicates throttling — re-run on a\n");
    std::printf("    quiet machine and check `dmesg | grep thermal`.\n");

    bench::emit_reports_json(reports, json);
    return 0;
}
