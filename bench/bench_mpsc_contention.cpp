// MpscRing multi-producer contention bench.
//
// Validates the claim from the bitmap commit:
//
//   "Real win for THIS API: multi-producer contention.
//    Single FAA(tail, N) replaces N × FAA(tail, 1)."
//
// Single-thread bench (bench_mpmc_saturation) showed ~4.5× speedup
// from batched API.  Multi-thread bench shows the contention story:
//
//   Without batched API: every try_push contends on head_ via CAS.
//     Per-call cost grows with producer count.
//
//   With batched API: each batch contends on head_ ONCE per N items
//     (single CAS claims N tickets).  Contention reduction = N×.
//
// Methodology:
//   * Sweep producer count ∈ {1, 2, 4, 8}
//   * Per producer: push K items (single API or batched API)
//   * Single consumer drains
//   * Measure: total wall-clock time, items/sec/thread, missing items
//   * Verify exact-once delivery (zero loss, zero duplicates)
//
// We measure WALL-CLOCK time on the producer side — that's the metric
// that matters under contention.  Per-thread throughput = items / (per-
// thread wall time).  Aggregate throughput = items / (system wall time).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <span>
#include <thread>
#include <vector>

#include <crucible/concurrent/MpscRing.h>

#include "bench_harness.h"

namespace {

using crucible::concurrent::MpscRing;
using Item = std::uint64_t;

constexpr std::size_t kCap = 1U << 14;       // 16K cells; enough headroom
constexpr std::size_t kItemsPerProducer = 200'000;

// Producer-side single-call write loop.  Each producer pushes K items
// via individual try_push calls.  Caller's thread spin-waits if full.
struct SinglePushWorker {
    MpscRing<Item, kCap>& ring;
    std::atomic<bool>&    start;
    std::size_t           producer_id;
    Item                  base;

    void operator()(std::stop_token) noexcept {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t i = 0; i < kItemsPerProducer; ++i) {
            const Item value = base + static_cast<Item>(i);
            while (!ring.try_push(value)) {
                std::this_thread::yield();
            }
        }
    }
};

// Producer-side batched write loop.  Each producer pushes K items via
// try_push_batch<BATCH> calls.  This amortizes the head_ CAS contention
// across BATCH items per call.
template <std::size_t BATCH>
struct BatchedPushWorker {
    MpscRing<Item, kCap>& ring;
    std::atomic<bool>&    start;
    std::size_t           producer_id;
    Item                  base;

    void operator()(std::stop_token) noexcept {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::array<Item, BATCH> buf{};
        std::size_t pushed = 0;
        while (pushed < kItemsPerProducer) {
            const std::size_t remaining = kItemsPerProducer - pushed;
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

// Result row from one (producer_count, api_kind) experiment.
struct Row {
    const char*   api;        // "single" or "batched<N>"
    std::size_t   N;          // producer count
    double        wall_ms;    // total wall time across all producers (max)
    double        items_per_sec_total;     // sum across producers / wall
    double        items_per_sec_per_prod;  // per-producer mean throughput
    int           missing;
    int           duplicates;
};

template <typename Worker>
Row run_one(const char* label, std::size_t producer_count) {
    auto ring_ptr = std::make_unique<MpscRing<Item, kCap>>();
    auto& ring = *ring_ptr;

    const std::size_t total = producer_count * kItemsPerProducer;
    std::vector<std::atomic<int>> seen(total);
    std::atomic<bool> start{false};
    std::atomic<std::size_t> consumed{0};

    // Spawn producers — Worker constructed in-place with refs.
    std::vector<std::jthread> producers;
    producers.reserve(producer_count);
    for (std::size_t p = 0; p < producer_count; ++p) {
        const Item base = static_cast<Item>(p) * kItemsPerProducer;
        producers.emplace_back(Worker{ring, start, p, base});
    }

    // Spawn consumer
    std::jthread consumer([&ring, &seen, &consumed, &start, total](
            std::stop_token) noexcept {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::array<Item, 64> buf{};
        while (consumed.load(std::memory_order_relaxed) < total) {
            const std::size_t n = ring.try_pop_batch(std::span<Item>(buf));
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (std::size_t i = 0; i < n; ++i) {
                seen[buf[i]].fetch_add(1, std::memory_order_relaxed);
            }
            consumed.fetch_add(n, std::memory_order_relaxed);
        }
    });

    // Time the producer-side wall clock.
    const auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    producers.clear();  // join all producers
    const auto t1 = std::chrono::steady_clock::now();
    consumer.join();

    const double wall_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double wall_sec = wall_ms / 1000.0;

    int missing = 0, dup = 0;
    for (std::size_t i = 0; i < total; ++i) {
        const int c = seen[i].load(std::memory_order_relaxed);
        if (c == 0) ++missing;
        else if (c > 1) dup += c - 1;
    }

    return Row{
        label,
        producer_count,
        wall_ms,
        static_cast<double>(total) / wall_sec,
        static_cast<double>(kItemsPerProducer) / wall_sec,
        missing,
        dup,
    };
}

void print_header() {
    std::printf("\n  %-16s %4s  %10s  %14s  %14s  %s\n",
                "API", "P", "wall (ms)",
                "items/s total", "items/s/prod", "miss/dup");
    std::printf("  %-16s %4s  %10s  %14s  %14s  %s\n",
                std::string(16, '-').c_str(),
                "---",
                std::string(10, '-').c_str(),
                std::string(14, '-').c_str(),
                std::string(14, '-').c_str(),
                "--------");
}

void print_row(const Row& r) {
    std::printf("  %-16s %4zu  %10.2f  %14.2e  %14.2e  %d/%d\n",
                r.api, r.N, r.wall_ms,
                r.items_per_sec_total,
                r.items_per_sec_per_prod,
                r.missing, r.duplicates);
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    std::printf("=== mpsc_contention ===\n");
    std::printf("  Item: uint64_t\n");
    std::printf("  Cap:  %zu (16K cells)\n", kCap);
    std::printf("  Per producer: %zu items\n", kItemsPerProducer);
    std::printf("\n");
    std::printf("  Sweep: producer count {1, 2, 4, 8} × {single, batched<16>,\n");
    std::printf("                                          batched<64>}\n");

    std::vector<Row> rows;
    for (std::size_t P : {std::size_t{1}, std::size_t{2},
                          std::size_t{4}, std::size_t{8}}) {
        rows.push_back(run_one<SinglePushWorker>     ("single",      P));
        rows.push_back(run_one<BatchedPushWorker<16>>("batched<16>", P));
        rows.push_back(run_one<BatchedPushWorker<64>>("batched<64>", P));
    }

    print_header();
    for (const auto& r : rows) print_row(r);

    // ── Headline analysis ────────────────────────────────────────────
    std::printf("\n=== contention reduction (single → batched<64>) ===\n");
    std::printf("  P    single (items/s)    batched<64> (items/s)    speedup\n");
    std::printf("  --   ------------------  -----------------------  -------\n");
    for (std::size_t pi = 0; pi < 4; ++pi) {
        const auto& s  = rows[pi * 3 + 0];   // single
        const auto& b64 = rows[pi * 3 + 2];  // batched<64>
        const double ratio = b64.items_per_sec_total / s.items_per_sec_total;
        std::printf("  %2zu      %12.2e    %12.2e         %5.2f×\n",
                    s.N, s.items_per_sec_total, b64.items_per_sec_total,
                    ratio);
    }

    std::printf("\n  Interpretation:\n");
    std::printf("  • At P=1 (no contention), batched API gives the single-thread\n");
    std::printf("    speedup measured in bench_mpmc_saturation (~4.5×).\n");
    std::printf("  • At P=2..8 (contention rises), the gap should WIDEN —\n");
    std::printf("    batched API does 1 CAS per N items, so head_ contention\n");
    std::printf("    is reduced by N× vs single-call which CASes per item.\n");
    std::printf("  • If the gap stays flat, single-thread cost dominates.\n");
    std::printf("    If the gap widens, contention reduction is the win.\n");

    return 0;
}
