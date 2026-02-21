// TraceRing SPSC ring buffer benchmark.
//
// Measures try_append() latency under various conditions:
//   1. Steady-state: ring never full, no contention
//   2. Append + drain alternating (simulated two-thread)
//   3. Burst throughput: fill ring, measure sustained rate
//   4. Sequential vs random schema_hash patterns
//   5. Cached tail effectiveness: compare cached vs uncached paths
//
// Build:  cmake --preset bench && cmake --build --preset bench -j$(nproc)
// Run:    ./build-bench/bench/bench_trace_ring

#include "bench_harness.h"
#include <crucible/TraceRing.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

using crucible::TraceRing;
using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::ScopeHash;
using crucible::CallsiteHash;
using crucible::MetaIndex;

// Pre-build a representative Entry to avoid construction cost in the loop.
static TraceRing::Entry make_entry(uint64_t schema, uint64_t shape) {
    TraceRing::Entry e{};
    e.schema_hash = SchemaHash{schema};
    e.shape_hash = ShapeHash{shape};
    e.num_inputs = 2;
    e.num_outputs = 1;
    e.num_scalar_args = 1;
    e.grad_enabled = true;
    e.inference_mode = false;
    e.scalar_values[0] = 42;
    return e;
}

int main() {
    std::printf("=== TraceRing SPSC Benchmark ===\n");
    std::printf("    Entry size: %zu bytes (expect 64)\n", sizeof(TraceRing::Entry));
    std::printf("    Ring capacity: %u entries\n", TraceRing::CAPACITY);
    std::printf("\n");

    // ── 1. try_append() isolated: pure append, no drain overhead ──
    // Manually timed loop: append 4096 entries, time with rdtsc,
    // reset ring, repeat. This isolates the pure try_append cost
    // without any drain, counter, or branch overhead in the loop.
    {
        std::printf("-- Pure try_append() (isolated, no drain in loop) --\n");
        auto* ring = new TraceRing();
        TraceRing::Entry e = make_entry(0xDEADBEEFCAFEBABE, 0x1234567890ABCDEF);
        MetaIndex mi{100};
        ScopeHash sh{0xAAAA};
        CallsiteHash ch{0xBBBB};

        // Warmup to prime caches.
        for (uint32_t i = 0; i < 1000; i++)
            static_cast<void>(ring->try_append(e, mi, sh, ch));
        ring->reset();

        constexpr uint32_t BATCH = 4096;
        constexpr uint32_t ROUNDS = 51;
        static double ratio = bench::tsc_ns_ratio();
        std::vector<double> samples;
        samples.reserve(ROUNDS);

        for (uint32_t r = 0; r < ROUNDS; r++) {
            ring->reset();
            bench::ClobberMemory();
            uint64_t t0 = bench::rdtsc();
            for (uint32_t i = 0; i < BATCH; i++) {
                static_cast<void>(ring->try_append(e, mi, sh, ch));
            }
            bench::ClobberMemory();
            uint64_t t1 = bench::rdtsc();
            double ns = static_cast<double>(t1 - t0) * ratio / BATCH;
            samples.push_back(ns);
        }

        std::sort(samples.begin(), samples.end());
        bench::Result res{};
        res.name = "try_append (isolated 4096)";
        res.ns_per_op = samples[ROUNDS / 2];
        res.min_ns = samples[0];
        res.max_ns = samples[ROUNDS - 1];
        res.median_ns = samples[ROUNDS / 2];
        res.p10_ns = samples[ROUNDS / 10];
        res.p90_ns = samples[ROUNDS - 1 - ROUNDS / 10];
        res.iterations = BATCH;
        bench::print_result(res);
        bench::check_regression(res, 4.5);

        // Also measure with interleaved drain for steady-state comparison.
        ring->reset();
        TraceRing::Entry drain_buf[4096];
        uint64_t drain_counter = 0;
        constexpr uint64_t ITERS = 2'000'000;
        BENCH_CHECK("try_append (w/ drain amortized)", ITERS, 6.0, {
            static_cast<void>(ring->try_append(e, mi, sh, ch));
            bench::DoNotOptimize(ring);
            drain_counter++;
            if ((drain_counter & 4095) == 0) [[unlikely]] {
                static_cast<void>(ring->drain(drain_buf, 4096));
            }
        });

        delete ring;
    }

    // ── 2. try_append() with varying schema hashes ──
    // Models realistic workload where each op has a different identity.
    {
        std::printf("\n-- Varying schema_hash (realistic op stream) --\n");
        auto* ring = new TraceRing();
        TraceRing::Entry drain_buf[4096];

        constexpr uint64_t ITERS = 2'000'000;
        uint64_t counter = 0;
        BENCH_CHECK("try_append (varying hash)", ITERS, 6.2, {
            TraceRing::Entry e{};
            e.schema_hash = SchemaHash{counter * 0x9E3779B97F4A7C15ULL};
            e.shape_hash = ShapeHash{counter ^ 0xDEADBEEF};
            e.num_inputs = 2;
            e.num_outputs = 1;
            static_cast<void>(ring->try_append(e,
                MetaIndex{static_cast<uint32_t>(counter & 0xFFFF)},
                ScopeHash{counter}, CallsiteHash{counter >> 16}));
            bench::DoNotOptimize(ring);
            counter++;
            if ((counter & 4095) == 0) [[unlikely]] {
                static_cast<void>(ring->drain(drain_buf, 4096));
            }
        });

        delete ring;
    }

    // ── 3. try_append() with default parallel arrays ──
    // Measures the cost when caller passes only the Entry (default args).
    {
        std::printf("\n-- Default args (entry only, no meta/scope/callsite) --\n");
        auto* ring = new TraceRing();
        TraceRing::Entry e = make_entry(0xCAFE, 0xBEEF);
        TraceRing::Entry drain_buf[4096];

        constexpr uint64_t ITERS = 2'000'000;
        uint64_t drain_counter = 0;
        BENCH_CHECK("try_append (defaults)", ITERS, 6.0, {
            static_cast<void>(ring->try_append(e));
            bench::DoNotOptimize(ring);
            drain_counter++;
            if ((drain_counter & 4095) == 0) [[unlikely]] {
                static_cast<void>(ring->drain(drain_buf, 4096));
            }
        });

        delete ring;
    }

    // ── 4. Burst append: fill ring to 50%, measure throughput ──
    // Stresses the cached tail: no draining, so tail never advances.
    // The cached tail is set once at the beginning and stays valid
    // for the entire burst.
    {
        std::printf("\n-- Burst (no drain, ring filling up) --\n");
        auto* ring = new TraceRing();
        TraceRing::Entry e = make_entry(0xB0B0B0B0, 0xA1A1A1A1);

        // Fill half the ring to ensure we don't hit full.
        constexpr uint64_t BURST_SIZE = TraceRing::CAPACITY / 2;
        // Variance limit 20.0: burst fills span cold→hot cache hierarchy
        // (2MB ring crosses L1/L2/L3). p90/p10 of 10-15× is structural.
        BENCH_CHECK_V("try_append (burst 32K)", BURST_SIZE, 1.2, 20.0, {
            static_cast<void>(ring->try_append(e, MetaIndex{42}, ScopeHash{1}, CallsiteHash{2}));
            bench::DoNotOptimize(ring);
        });

        delete ring;
    }

    // ── 5. Append + drain alternating: producer/consumer handoff ──
    // Append N entries, drain all, repeat. Measures the amortized
    // cost including drain overhead.
    {
        std::printf("\n-- Append+drain alternating (amortized) --\n");
        auto* ring = new TraceRing();
        TraceRing::Entry e = make_entry(0xABCD, 0x1234);
        TraceRing::Entry drain_buf[256];
        MetaIndex meta_buf[256];
        ScopeHash scope_buf[256];
        CallsiteHash callsite_buf[256];

        constexpr uint64_t ITERS = 1'000'000;
        constexpr uint32_t BATCH = 128;
        BENCH_CHECK("append+drain (batch 128)", ITERS, 596.0, {
            for (uint32_t i = 0; i < BATCH; i++) {
                static_cast<void>(ring->try_append(e,
                    MetaIndex{i}, ScopeHash{static_cast<uint64_t>(i)},
                    CallsiteHash{static_cast<uint64_t>(i)}));
            }
            static_cast<void>(ring->drain(drain_buf, BATCH,
                meta_buf, scope_buf, callsite_buf));
            bench::DoNotOptimize(drain_buf);
        });

        delete ring;
    }

    // ── 6. drain() throughput: bulk read performance ──
    {
        std::printf("\n-- drain() bulk read --\n");
        auto* ring = new TraceRing();
        TraceRing::Entry e = make_entry(0xD0D0D0D0, 0xE1E1E1E1);

        // Pre-fill ring with 4096 entries for drain measurement.
        for (uint32_t i = 0; i < 4096; i++)
            static_cast<void>(ring->try_append(e,
                MetaIndex{i}, ScopeHash{static_cast<uint64_t>(i)},
                CallsiteHash{static_cast<uint64_t>(i)}));

        TraceRing::Entry drain_buf[4096];
        MetaIndex meta_buf[4096];
        ScopeHash scope_buf[4096];
        CallsiteHash callsite_buf[4096];

        // Drain all, then refill between rounds.
        constexpr uint64_t ITERS = 10'000;
        BENCH_CHECK("drain (4096 entries)", ITERS, 21134.0, {
            // Refill
            ring->reset();
            for (uint32_t i = 0; i < 4096; i++)
                static_cast<void>(ring->try_append(e,
                    MetaIndex{i}, ScopeHash{static_cast<uint64_t>(i)},
                    CallsiteHash{static_cast<uint64_t>(i)}));
            // Measure drain
            uint32_t n = ring->drain(drain_buf, 4096,
                meta_buf, scope_buf, callsite_buf);
            bench::DoNotOptimize(n);
            bench::DoNotOptimize(drain_buf);
        });

        delete ring;
    }

    // ── 7. Cached tail effectiveness ──
    {
        std::printf("\n-- Cached tail validation --\n");
        auto* ring = new TraceRing();
        TraceRing::Entry e = make_entry(0xCA00CA00, 0x7A117A11);
        TraceRing::Entry drain_buf[4096];

        // Fill near capacity without draining.
        // The cached tail slow path triggers once when the ring appears
        // full from the cached tail (which is 0, the real tail).
        uint32_t appended = 0;
        uint32_t failed = 0;
        for (uint32_t i = 0; i < 65000; i++) {
            if (ring->try_append(e)) {
                appended++;
            } else {
                failed++;
            }
        }
        std::printf("  Near-capacity test: appended=%u, failed=%u (capacity=%u)\n",
                    appended, failed, TraceRing::CAPACITY);

        // Now drain half and try again to verify cached_tail refresh works.
        uint32_t drained = ring->drain(drain_buf, 4096);
        std::printf("  Drained %u entries\n", drained);

        // These appends should succeed after cached_tail refresh.
        uint32_t after = 0;
        for (uint32_t i = 0; i < 4096; i++) {
            if (ring->try_append(e))
                after++;
        }
        std::printf("  After drain: %u of 4096 appends succeeded\n", after);

        delete ring;
    }

    // ── 8. Two-thread contention: real SPSC scenario ──
    {
        std::printf("\n-- Two-thread SPSC (real contention) --\n");
        auto* ring = new TraceRing();
        TraceRing::Entry e = make_entry(0x20202020, 0x30303030);

        constexpr uint64_t PRODUCE_COUNT = 4'000'000;
        std::atomic<bool> consumer_done{false};
        std::atomic<uint64_t> consumed{0};

        // Consumer thread: drain as fast as possible.
        std::thread consumer([&] {
            TraceRing::Entry buf[4096];
            MetaIndex meta_buf[4096];
            uint64_t total = 0;
            while (!consumer_done.load(std::memory_order_relaxed) || ring->size() > 0) {
                uint32_t n = ring->drain(buf, 4096, meta_buf);
                total += n;
                if (n == 0) {
                    // Brief pause: 100 iterations of empty loop.
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            consumed.store(total, std::memory_order_relaxed);
        });

        // Producer: append PRODUCE_COUNT entries as fast as possible.
        bench::ClobberMemory();
        uint64_t start = bench::rdtsc();

        uint64_t produced = 0;
        uint64_t drops = 0;
        for (uint64_t i = 0; i < PRODUCE_COUNT; i++) {
            if (ring->try_append(e, MetaIndex{static_cast<uint32_t>(i & 0xFFFF)})) {
                produced++;
            } else {
                drops++;
            }
        }

        bench::ClobberMemory();
        uint64_t end = bench::rdtsc();

        consumer_done.store(true, std::memory_order_relaxed);
        consumer.join();

        static double ratio = bench::tsc_ns_ratio();
        double total_ns = static_cast<double>(end - start) * ratio;
        double ns_per_op = total_ns / static_cast<double>(PRODUCE_COUNT);

        std::printf("  %-40s %6.1f ns/op  (produced=%lu, drops=%lu, consumed=%lu)\n",
                    "try_append (2-thread SPSC)",
                    ns_per_op, produced, drops,
                    consumed.load(std::memory_order_relaxed));

        delete ring;
    }

    // ── 9. size() query cost ──
    {
        std::printf("\n-- size() query --\n");
        auto* ring = new TraceRing();
        TraceRing::Entry e = make_entry(0x51510000, 0x7E570000);
        for (uint32_t i = 0; i < 1000; i++)
            static_cast<void>(ring->try_append(e));

        constexpr uint64_t ITERS = 10'000'000;
        volatile uint32_t sink = 0;
        BENCH_CHECK("size() query", ITERS, 0.8, {
            sink = ring->size();
            bench::DoNotOptimize(sink);
        });

        delete ring;
    }

    // ── 10. reset() cost ──
    {
        std::printf("\n-- reset() --\n");
        auto* ring = new TraceRing();

        constexpr uint64_t ITERS = 10'000'000;
        BENCH_CHECK("reset()", ITERS, 2.5, {
            ring->reset();
            bench::DoNotOptimize(ring);
        });

        delete ring;
    }

    std::printf("\nDone.\n");
    return 0;
}
