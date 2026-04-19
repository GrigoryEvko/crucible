// MetaLog SPSC buffer — per-sample tail-latency benchmark.
//
// TensorMeta is 168 B (2×64 B size/stride arrays + 24 B extended fields
// + 16 B core fields); 1 M entries ≈ 168 MB backing, which exercises
// rt::Hardening's huge_hint path when `CRUCIBLE_BENCH_HARDENING=
// production` or `cloud_vm`. MetaLog self-registers the region with
// the HotRegionRegistry, so bench::Run(...)·hardening(p) mlocks and
// MADV_HUGEPAGE's it automatically for the measured runs below.
//
// Typical numbers on Alder Lake + 4.8 GHz:
//   try_append(n=1..3)  p50 ≈ 8-20 ns   (auto-batch amortizes rdtsc overhead)
//   try_append(n=8)     p50 ≈ 50-100 ns (memcpy dominates; 8×168 B = 1344 B)
//
// Build & run:
//   cmake --preset bench && cmake --build --preset bench -j$(nproc)
//   ./build-bench/bench/bench_meta_log
//   CRUCIBLE_BENCH_HARDENING=production ./build-bench/bench/bench_meta_log

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <crucible/MetaLog.h>

#include "bench_harness.h"

using crucible::DeviceType;
using crucible::MetaLog;
using crucible::ScalarType;
using crucible::TensorMeta;

// Realistic 2D float tensor (linear / matmul input).
[[nodiscard]] static TensorMeta make_meta_2d(int64_t dim0, int64_t dim1) noexcept {
    TensorMeta m{};
    m.sizes[0]    = dim0;
    m.sizes[1]    = dim1;
    m.strides[0]  = dim1;
    m.strides[1]  = 1;
    m.ndim        = 2;
    m.dtype       = ScalarType::Float;
    m.device_type = DeviceType::CUDA;
    m.device_idx  = 0;
    m.data_ptr    = reinterpret_cast<void*>(0xDEAD'BEEF'0000ULL);
    return m;
}

// 4D NCHW tensor (conv input/weight).
[[nodiscard]] static TensorMeta make_meta_4d(int64_t n, int64_t c, int64_t h, int64_t w) noexcept {
    TensorMeta m{};
    m.sizes[0]    = n;   m.sizes[1]   = c;   m.sizes[2]   = h;   m.sizes[3]   = w;
    m.strides[0]  = c * h * w;
    m.strides[1]  = h * w;
    m.strides[2]  = w;
    m.strides[3]  = 1;
    m.ndim        = 4;
    m.dtype       = ScalarType::Float;
    m.device_type = DeviceType::CUDA;
    m.device_idx  = 0;
    m.data_ptr    = reinterpret_cast<void*>(0xCAFE'BABE'0000ULL);
    return m;
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== meta_log ===\n");
    std::printf("  TensorMeta size: %zu bytes (expect 168)\n", sizeof(TensorMeta));
    std::printf("  MetaLog capacity: %u entries (~%zu MB)\n\n",
                MetaLog::CAPACITY,
                (static_cast<size_t>(MetaLog::CAPACITY) * sizeof(TensorMeta)) / (1024 * 1024));

    // Reusable test data in stack-frame storage. MetaLog::try_append
    // memcpys from (metas, n) into the ring, so the source doesn't need
    // to live longer than the call.
    const TensorMeta meta1[1] = { make_meta_2d(64, 768) };
    const TensorMeta meta2[2] = { make_meta_2d(64, 768),
                                  make_meta_2d(768, 768) };
    const TensorMeta meta3[3] = { make_meta_2d(64, 768),
                                  make_meta_2d(768, 768),
                                  make_meta_2d(64, 768) };
    const TensorMeta meta8[8] = {
        make_meta_4d(32,  64, 56, 56), make_meta_4d( 64,  64, 3, 3),
        make_meta_4d( 1,  64,  1,  1), make_meta_4d( 32,  64,56,56),
        make_meta_4d(32, 128, 28, 28), make_meta_4d(128,  64, 3, 3),
        make_meta_4d( 1, 128,  1,  1), make_meta_4d( 32, 128,28,28),
    };

    // ── Producer-side latency (+reset-on-full) ────────────────────────
    //
    // The body calls try_append(n) and resets the log when full. Same
    // trade-off documented in bench_trace_ring: p99.9 / max include
    // occasional reset costs (one atomic store + bookkeeping), which
    // dominates tail but not p50. MetaLog at 1 M capacity and n metas
    // per call saturates after ~(1M / n) calls, so reset fires
    // infrequently (1×/~333 k at n=3).
    //
    // All four buffers live heap-side; MetaLog's 168 MB allocation
    // would blow the stack.
    bench::Report reports[] = {
        [&]{
            auto log = std::make_unique<MetaLog>();
            return bench::run("try_append(1 meta  = 168B)", [&]{
                auto idx = log->try_append(meta1, 1);
                bench::do_not_optimize(idx);
                if (!idx.is_valid()) log->reset();
            });
        }(),
        [&]{
            auto log = std::make_unique<MetaLog>();
            return bench::run("try_append(2 metas = 336B)", [&]{
                auto idx = log->try_append(meta2, 2);
                bench::do_not_optimize(idx);
                if (!idx.is_valid()) log->reset();
            });
        }(),
        [&]{
            auto log = std::make_unique<MetaLog>();
            return bench::run("try_append(3 metas = 504B)", [&]{
                auto idx = log->try_append(meta3, 3);
                bench::do_not_optimize(idx);
                if (!idx.is_valid()) log->reset();
            });
        }(),
        [&]{
            auto log = std::make_unique<MetaLog>();
            return bench::run("try_append(8 metas = 1344B)", [&]{
                auto idx = log->try_append(meta8, 8);
                bench::do_not_optimize(idx);
                if (!idx.is_valid()) log->reset();
            });
        }(),

        // ── Throughput: sustained fill with batch drain ───────────────
        //
        // Drain every 50 k calls (imitates background thread batch
        // drain). Amortizes the tail-store cost across many appends.
        // Inner counter stays a local to avoid atomic-store pollution
        // of the measurement.
        [&]{
            auto log              = std::make_unique<MetaLog>();
            uint32_t since_drain  = 0;
            constexpr uint32_t kD = 50'000;
            return bench::run("throughput (3 metas/call, drain/50k)", [&]{
                auto idx = log->try_append(meta3, 3);
                bench::do_not_optimize(idx);
                if (++since_drain >= kD) [[unlikely]] {
                    log->advance_tail(log->head.load(std::memory_order_relaxed));
                    since_drain = 0;
                }
            });
        }(),

        // ── Raw atomics (floor for ordering cost) ─────────────────────
        [&]{
            auto log = std::make_unique<MetaLog>();
            return bench::run("head.load (relaxed)", [&]{
                auto h = log->head.load(std::memory_order_relaxed);
                bench::do_not_optimize(h);
            });
        }(),
        [&]{
            auto log    = std::make_unique<MetaLog>();
            uint32_t v  = 0;
            return bench::run("head.store (release)", [&]{
                log->head.store(++v, std::memory_order_release);
                bench::do_not_optimize(v);
            });
        }(),

        // ── Baselines: pure memcpy to advancing destination ───────────
        //
        // These show the lower bound of try_append — the ring's producer
        // does (at minimum) one memcpy + two atomic ops. Comparing
        // try_append against memcpy isolates the SPSC overhead.
        [&]{
            // 64 B-aligned scratch. aligned_alloc gives raw storage; run
            // each NSDMI via placement-new so the bytes are well-defined
            // before we memcpy over them (kills -Wclass-memaccess).
            constexpr uint32_t kBuf = 4096;
            auto buf = static_cast<TensorMeta*>(
                std::aligned_alloc(64, kBuf * sizeof(TensorMeta)));
            for (uint32_t i = 0; i < kBuf; ++i) ::new (&buf[i]) TensorMeta{};
            const TensorMeta src = make_meta_2d(64, 768);
            uint32_t pos = 0;
            auto r = bench::run("memcpy(168B)  to advancing dst", [&]{
                std::memcpy(&buf[pos & (kBuf - 1)], &src, sizeof(TensorMeta));
                bench::do_not_optimize(buf[pos & (kBuf - 1)]);
                ++pos;
            });
            std::free(buf);
            return r;
        }(),
        [&]{
            constexpr uint32_t kBuf = 4096;
            auto buf = static_cast<TensorMeta*>(
                std::aligned_alloc(64, kBuf * sizeof(TensorMeta)));
            for (uint32_t i = 0; i < kBuf; ++i) ::new (&buf[i]) TensorMeta{};
            const TensorMeta src[3] = { make_meta_2d(64, 768),
                                         make_meta_2d(768, 768),
                                         make_meta_2d(64, 768) };
            uint32_t pos = 0;
            auto r = bench::run("memcpy(504B)  to advancing dst", [&]{
                const uint32_t p = pos & (kBuf - 4);  // leave room for 3 entries
                std::memcpy(&buf[p], src, 3 * sizeof(TensorMeta));
                bench::do_not_optimize(buf[p]);
                ++pos;
            });
            std::free(buf);
            return r;
        }(),
        [&]{
            // Same-destination memcpy — hot in L1, theoretical minimum.
            // Gives the compiler's best case for 168 B store.
            const TensorMeta src = make_meta_2d(64, 768);
            TensorMeta dst{};
            return bench::run("memcpy(168B)  same dst (L1)", [&]{
                std::memcpy(&dst, &src, sizeof(TensorMeta));
                bench::do_not_optimize(dst);
            });
        }(),
    };

    bench::emit_reports_text(reports);

    // A/B compare — head.load vs head.store: one relaxed load vs one
    // release store. On x86 both compile to a single MOV after CPU
    // store-buffer commit; the release on store adds no extra
    // instruction but may exhibit cache-line contention differences
    // when the consumer is reading. Statistically indistinguishable
    // on a single-threaded bench.
    std::printf("\n=== compare ===\n");
    bench::compare(reports[5], reports[6]).print_text(stdout);

    // 95% bootstrap CI on the n=3 producer-side p99 — the most common
    // workload shape (2 inputs + 1 output → 3 meta slots per op).
    const auto ci99 = reports[2].ci(0.99);
    std::printf("  %s  p99 95%% CI: [%.2f, %.2f] ns\n",
                reports[2].name.c_str(), ci99.lo, ci99.hi);

    bench::emit_reports_json(reports, json);
    return 0;
}
