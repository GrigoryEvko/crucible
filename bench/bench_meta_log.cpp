// MetaLog SPSC buffer performance benchmark.
//
// Measures try_append() latency for realistic tensor metadata counts:
//   - 1 meta  (scalar op, single input)
//   - 2 metas (binary op: 2 inputs)
//   - 3 metas (typical: 2 inputs + 1 output recorded together)
//   - 8 metas (conv2d: multiple inputs/outputs)
//   - Throughput: sustained fill rate (ns per TensorMeta entry)
//
// Target: try_append() <= 10ns for typical workloads (n=1..3).
//
// Build & run:
//   cmake --preset bench && cmake --build --preset bench -j$(nproc)
//   ./build-bench/bench/bench_meta_log

#include "bench_harness.h"
#include <crucible/MetaLog.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace crucible;

// Create a realistic TensorMeta (2D float tensor, contiguous).
static TensorMeta make_meta(int64_t dim0, int64_t dim1) {
  TensorMeta m{};
  m.sizes[0] = dim0;
  m.sizes[1] = dim1;
  m.strides[0] = dim1;
  m.strides[1] = 1;
  m.ndim = 2;
  m.dtype = ScalarType::Float;
  m.device_type = DeviceType::CUDA;
  m.device_idx = 0;
  m.data_ptr = reinterpret_cast<void*>(0xDEAD'BEEF'0000ULL);
  return m;
}

// Create a 4D tensor meta (e.g., NCHW convolution).
static TensorMeta make_meta_4d(int64_t n, int64_t c, int64_t h, int64_t w) {
  TensorMeta m{};
  m.sizes[0] = n;   m.sizes[1] = c;   m.sizes[2] = h;   m.sizes[3] = w;
  m.strides[0] = c * h * w;
  m.strides[1] = h * w;
  m.strides[2] = w;
  m.strides[3] = 1;
  m.ndim = 4;
  m.dtype = ScalarType::Float;
  m.device_type = DeviceType::CUDA;
  m.device_idx = 0;
  m.data_ptr = reinterpret_cast<void*>(0xCAFE'BABE'0000ULL);
  return m;
}

// Manual benchmark with drain-between-rounds to avoid overflow.
//
// Each round: run ITERS try_append calls (no drain) → measure latency.
// Between rounds: reset the MetaLog (drain all, outside timed region).
// This measures pure producer-side latency with cached_tail warm.
struct ManualBench {
  static bench::Result run_append(
      const char* name,
      MetaLog* log,
      const TensorMeta* metas,
      uint32_t n_metas,
      uint64_t iters,
      uint32_t rounds) {
    static double ratio = bench::tsc_ns_ratio();
    std::vector<double> samples;
    samples.reserve(rounds);

    for (uint32_t r = 0; r < rounds; r++) {
      // Reset between rounds (outside timed region).
      log->reset();

      bench::ClobberMemory();
      uint64_t start = bench::rdtsc();

      for (uint64_t i = 0; i < iters; i++) {
        auto idx = log->try_append(metas, n_metas);
        bench::DoNotOptimize(idx);
      }

      bench::ClobberMemory();
      uint64_t end = bench::rdtsc();

      double total_ns = static_cast<double>(end - start) * ratio;
      samples.push_back(total_ns / static_cast<double>(iters));
    }

    std::sort(samples.begin(), samples.end());
    bench::Result res{};
    res.name = name;
    res.ns_per_op = samples[rounds / 2];
    res.min_ns = samples[0];
    res.max_ns = samples[rounds - 1];
    res.median_ns = samples[rounds / 2];
    res.p10_ns = samples[rounds / 10];
    res.p90_ns = samples[rounds - 1 - rounds / 10];
    res.iterations = iters;
    return res;
  }
};

int main() {
  std::printf("=== MetaLog SPSC Buffer Benchmark ===\n");
  std::printf("  TensorMeta size: %zu bytes\n", sizeof(TensorMeta));
  std::printf("  MetaLog capacity: %u entries (~%zu MB)\n\n",
              MetaLog::CAPACITY,
              MetaLog::CAPACITY * sizeof(TensorMeta) / (1024 * 1024));

  // ── Prepare test data ──

  TensorMeta meta1[1] = { make_meta(64, 768) };
  TensorMeta meta2[2] = { make_meta(64, 768), make_meta(768, 768) };
  TensorMeta meta3[3] = { make_meta(64, 768), make_meta(768, 768), make_meta(64, 768) };
  TensorMeta meta8[8] = {
    make_meta_4d(32, 64, 56, 56),  make_meta_4d(64, 64, 3, 3),
    make_meta_4d(1, 64, 1, 1),     make_meta_4d(32, 64, 56, 56),
    make_meta_4d(32, 128, 28, 28), make_meta_4d(128, 64, 3, 3),
    make_meta_4d(1, 128, 1, 1),    make_meta_4d(32, 128, 28, 28),
  };

  // ── Producer-only latency (realistic: background thread drains separately) ──
  //
  // Pure producer cost with cached_tail warm. Between rounds we reset
  // the MetaLog to prevent overflow. Each round writes ITERS*n entries
  // into a fresh buffer, hitting every cache line in sequence.
  //
  // ITERS chosen so n_metas * ITERS < CAPACITY (no overflow within round).
  // - n=1: ITERS=500K (500K < 1M)
  // - n=2: ITERS=500K (1M = 1M, tight)
  // - n=3: ITERS=300K (900K < 1M)
  // - n=8: ITERS=100K (800K < 1M)

  std::printf("--- Latency: try_append() (producer-only, realistic) ---\n");

  {
    auto* log = new MetaLog();
    auto r = ManualBench::run_append("try_append(1 meta = 144B)", log, meta1, 1, 500'000, 21);
    bench::print_result(r);
    bench::check_regression(r, 12.9);
    delete log;
  }

  {
    auto* log = new MetaLog();
    auto r = ManualBench::run_append("try_append(2 metas = 288B)", log, meta2, 2, 400'000, 21);
    bench::print_result(r);
    bench::check_regression(r, 23.1);
    delete log;
  }

  {
    auto* log = new MetaLog();
    auto r = ManualBench::run_append("try_append(3 metas = 432B)", log, meta3, 3, 300'000, 21);
    bench::print_result(r);
    bench::check_regression(r, 37.1);
    delete log;
  }

  {
    auto* log = new MetaLog();
    auto r = ManualBench::run_append("try_append(8 metas = 1152B)", log, meta8, 8, 100'000, 21);
    bench::print_result(r);
    bench::check_regression(r, 103.4);
    delete log;
  }

  // ── Same-thread drain (worst case baseline) ──
  //
  // This shows the penalty when the consumer drains on the same thread
  // as the producer — defeats cached_tail, adds tail write cost.
  // NOT realistic but useful as an upper bound.

  std::printf("\n--- Latency: with same-thread drain (worst case) ---\n");

  {
    auto* log = new MetaLog();
    BENCH_CHECK("try_append(1) + drain", 1'000'000, 12.2, {
      auto idx = log->try_append(meta1, 1);
      bench::DoNotOptimize(idx);
      log->advance_tail(log->head.load(std::memory_order_relaxed));
    });
    delete log;
  }

  {
    auto* log = new MetaLog();
    BENCH_CHECK("try_append(3) + drain", 1'000'000, 37.2, {
      auto idx = log->try_append(meta3, 3);
      bench::DoNotOptimize(idx);
      log->advance_tail(log->head.load(std::memory_order_relaxed));
    });
    delete log;
  }

  // ── Throughput: sustained fill with batch drain ──
  //
  // Drain every 50K calls (simulating background thread batch drain).
  // This amortizes drain cost across many appends.

  std::printf("\n--- Throughput: sustained with batch drain ---\n");

  {
    auto* log = new MetaLog();
    uint32_t ops_since_drain = 0;
    constexpr uint32_t DRAIN_EVERY = 50'000;

    // Variance limit 10.0: 100K×3 metas = 21MB working set spans L3,
    // causing structural p90/p10 from cache-hierarchy transitions.
    BENCH_ROUNDS_CHECK_V("throughput (3 metas/call)", 100'000, 11, 40.2, 10.0, {
      auto idx = log->try_append(meta3, 3);
      bench::DoNotOptimize(idx);
      ops_since_drain++;
      if (ops_since_drain >= DRAIN_EVERY) [[unlikely]] {
        log->advance_tail(log->head.load(std::memory_order_relaxed));
        ops_since_drain = 0;
      }
    });
    std::printf("  (divide ns/op by 3 for ns/meta)\n");
    delete log;
  }

  // ── Baselines ──

  std::printf("\n--- Baselines ---\n");

  {
    // Memcpy to advancing destination (captures cache miss cost).
    static constexpr uint32_t BUF_SIZE = 4096;
    auto* buf = static_cast<TensorMeta*>(
        std::aligned_alloc(64, BUF_SIZE * sizeof(TensorMeta)));
    TensorMeta src = make_meta(64, 768);
    uint32_t pos = 0;
    BENCH_CHECK("memcpy(144B) to advancing dst", 1'000'000, 5.9, {
      std::memcpy(&buf[pos & (BUF_SIZE - 1)], &src, sizeof(TensorMeta));
      bench::DoNotOptimize(buf[pos & (BUF_SIZE - 1)]);
      pos++;
    });
    std::free(buf);
  }

  {
    // Same-location memcpy (L1 hit — theoretical minimum).
    TensorMeta src = make_meta(64, 768);
    TensorMeta dst{};
    BENCH_CHECK("memcpy(144B) same dst (L1 hit)", 10'000'000, 3.0, {
      std::memcpy(&dst, &src, sizeof(TensorMeta));
      bench::DoNotOptimize(dst);
    });
  }

  {
    // 3x144B to advancing destination.
    static constexpr uint32_t BUF_SIZE = 4096;
    auto* buf = static_cast<TensorMeta*>(
        std::aligned_alloc(64, BUF_SIZE * sizeof(TensorMeta)));
    TensorMeta src[3] = { make_meta(64, 768), make_meta(768, 768), make_meta(64, 768) };
    uint32_t pos = 0;
    BENCH_CHECK("memcpy(432B) to advancing dst", 1'000'000, 7.5, {
      uint32_t p = pos & (BUF_SIZE - 4); // leave room for 3 entries
      std::memcpy(&buf[p], src, 3 * sizeof(TensorMeta));
      bench::DoNotOptimize(buf[p]);
      pos++;
    });
    std::free(buf);
  }

  {
    auto* log = new MetaLog();
    BENCH_CHECK("atomic load (relaxed)", 10'000'000, 0.3, {
      auto h = log->head.load(std::memory_order_relaxed);
      bench::DoNotOptimize(h);
    });
    delete log;
  }

  {
    auto* log = new MetaLog();
    uint32_t val = 0;
    BENCH_CHECK("atomic store (release)", 10'000'000, 0.3, {
      log->head.store(val++, std::memory_order_release);
      bench::DoNotOptimize(val);
    });
    delete log;
  }

  std::printf("\nDone.\n");
  return 0;
}
