// Philox4x32-10 throughput bench.
//
// One scalar generate() call produces 4 × uint32 = 16 bytes of random
// data.  Target: ≤20 ns / call → ≥800 MB/s on a single thread.
//
// SIMD-9 batched generate produces 8 × (4 × uint32) = 128 bytes per
// call.  Target: ≤25 ns / call → ≥5 GB/s = ~6× scalar throughput.
// Both scale linearly across element indices (counter-based, no
// sequential state), so the streaming buffer-fill number is the
// throughput that matters.

#include <cstdint>
#include <cstdio>

#include <crucible/Philox.h>
#include <crucible/PhiloxSimd.h>
#include <crucible/safety/Simd.h>

#include "bench_harness.h"

using crucible::ContentHash;
using crucible::Philox;

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    // volatile seeds so the compiler can't constant-fold the generate()
    // call out of existence. Live in outer scope so every lambda sees them.
    volatile uint64_t offset  = 0x1234'5678'9abc'def0ULL;
    volatile uint64_t key     = 0xcafe'babe'dead'beefULL;
    volatile uint32_t seed    = 1;
    volatile uint32_t raw     = 0x12345678;
    volatile uint32_t u1      = 0xAABBCCDD;
    volatile uint32_t u2      = 0x11223344;
    volatile uint64_t master  = 42;
    volatile uint32_t op_idx  = 7;
    const ContentHash content_hash{0xfeedfacecafeULL};

    auto make_ctr = [](uint32_t a) noexcept {
        return Philox::Ctr{a, a + 1, a + 2, a + 3};
    };
    auto make_key = [](uint32_t a) noexcept {
        return Philox::Key{a, a + 1};
    };

    // 16 KB scratch for the streaming fill — alignas(64) so the store
    // path hits full cache-line writes.
    constexpr size_t BUF_WORDS = 4096;
    alignas(64) static uint32_t scratch[BUF_WORDS];

    std::printf("=== philox ===\n\n");

    bench::Report reports[] = {
        bench::run("generate(offset, key)           [4×u32 = 16B]", [&]{
            auto ctr = Philox::generate(offset, key);
            bench::do_not_optimize(ctr);
        }),
        bench::run("generate raw 4×32 Ctr+Key", [&]{
            auto r = Philox::generate(make_ctr(seed), make_key(seed + 4));
            bench::do_not_optimize(r);
        }),
        // Counter increments per iteration — mimics per-thread invocation
        // in a parallel kernel. Key is constant for the run.
        [&]{
            uint64_t i = 0;
            return bench::run("generate streaming (i++, key)", [&]{
                auto ctr = Philox::generate(i++, key);
                bench::do_not_optimize(ctr);
            });
        }(),
        bench::run("to_uniform(u32) → float", [&]{
            float f = Philox::to_uniform(raw);
            bench::do_not_optimize(f);
        }),
        bench::run("box_muller(u32, u32) → 2×f32", [&]{
            auto pair = Philox::box_muller(u1, u2);
            bench::do_not_optimize(pair);
        }),
        bench::run("op_key(master, op_idx, hash)", [&]{
            uint64_t k = Philox::op_key(master, op_idx, content_hash);
            bench::do_not_optimize(k);
        }),
        // Streaming buffer fill: 4096 u32 = 1024 generate() calls.
        // Gives effective throughput per 16 KB batch.
        bench::run("fill 4096 u32 (16 KB batch)", [&]{
            for (size_t i = 0; i < BUF_WORDS; i += 4) {
                auto c = Philox::generate(static_cast<uint64_t>(i), key);
                scratch[i + 0] = c[0];
                scratch[i + 1] = c[1];
                scratch[i + 2] = c[2];
                scratch[i + 3] = c[3];
            }
            bench::do_not_optimize(scratch[0]);
        }),

        // ── SIMD-9: batched generator ─────────────────────────────
        //
        // One philox_batch8 call = 8 scalar generate() calls' worth
        // of work (32 × uint32 = 128 bytes).  The straight-line
        // version measures pure per-batch latency.
        [&]{
            using crucible::simd::u32x8;
            const u32x8 ctr0(static_cast<uint32_t>(seed));
            const u32x8 ctr1(static_cast<uint32_t>(seed + 1));
            const u32x8 ctr2(0u);
            const u32x8 ctr3(0u);
            const u32x8 key0(static_cast<uint32_t>(key));
            const u32x8 key1(static_cast<uint32_t>(key >> 32));
            return bench::run("philox_batch8 (8×4×u32 = 128B)", [&]{
                auto out = crucible::detail::philox_batch8(
                    ctr0, ctr1, ctr2, ctr3, key0, key1);
                bench::do_not_optimize(out.r0);
                bench::do_not_optimize(out.r1);
                bench::do_not_optimize(out.r2);
                bench::do_not_optimize(out.r3);
            });
        }(),

        // SIMD streaming fill: 4096 u32 = 128 batch8 calls.  Same
        // 16 KB target as the scalar streaming fill — head-to-head
        // throughput comparison.
        //
        // Layout note: stores 4 contiguous u32x8 vectors per call
        // (r0[8], r1[8], r2[8], r3[8]) — NOT interleaved per-lane.
        // This makes each call write 4 vector stores instead of 32
        // scalar stores; the streaming bench is then dominated by
        // generate cost, not store cost.  Callers that need
        // per-stream output simply read 4 stride-8 streams out of
        // the same buffer.
        [&]{
            using crucible::simd::u32x8;
            return bench::run("philox_batch8 fill 4096 u32 (16 KB batch)", [&]{
                const u32x8 key0(static_cast<uint32_t>(key));
                const u32x8 key1(static_cast<uint32_t>(key >> 32));
                // 32 u32 per batch8 call → 4096/32 = 128 calls.
                // Lane i of the i-th call sees counter
                // base = (call_idx * 8 + i) on ctr0; ctr1..3 stay 0.
                const u32x8 lane_offsets =
                    crucible::simd::iota_v<u32x8>();
                for (uint32_t call_idx = 0;
                     call_idx < BUF_WORDS / 32; ++call_idx)
                {
                    const u32x8 base(call_idx * 8u);
                    const u32x8 ctr0 = base + lane_offsets;
                    const u32x8 ctr1(0u);
                    const u32x8 ctr2(0u);
                    const u32x8 ctr3(0u);
                    auto out = crucible::detail::philox_batch8(
                        ctr0, ctr1, ctr2, ctr3, key0, key1);
                    // Four aligned vector stores — 32 u32 per call
                    // in 4 instructions instead of 32.  Iterator +
                    // count form: unchecked_store(v, first, n, flag).
                    const std::size_t base_idx = call_idx * 32u;
                    constexpr std::ptrdiff_t LANES = u32x8::size();
                    std::simd::unchecked_store(
                        out.r0, scratch + base_idx +  0, LANES,
                        std::simd::flag_aligned);
                    std::simd::unchecked_store(
                        out.r1, scratch + base_idx +  8, LANES,
                        std::simd::flag_aligned);
                    std::simd::unchecked_store(
                        out.r2, scratch + base_idx + 16, LANES,
                        std::simd::flag_aligned);
                    std::simd::unchecked_store(
                        out.r3, scratch + base_idx + 24, LANES,
                        std::simd::flag_aligned);
                }
                bench::do_not_optimize(scratch[0]);
            });
        }(),
    };

    bench::emit_reports_text(reports);

    // Determinism sanity check — same inputs, same bits. Philox is
    // stateless, so this must never fail; any drift means the algorithm
    // has been miscompiled or a wrong-endianness assumption crept in.
    auto r1 = Philox::generate(uint64_t{1}, uint64_t{2});
    auto r2 = Philox::generate(uint64_t{1}, uint64_t{2});
    for (int i = 0; i < 4; ++i) {
        if (r1[i] != r2[i]) {
            std::fprintf(stderr, "\n[FATAL] Philox non-deterministic!\n");
            return 1;
        }
    }
    std::printf("\nphilox: deterministic, ok\n");

    bench::emit_reports_json(reports, json);
    return 0;
}
