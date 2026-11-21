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

#include <crucible/Philox.h>
#include <crucible/PhiloxSimd.h>
#include <fixy/Core.h>
#include <foundation/Simd.h>

#include "bench_harness.h"

using crucible::ContentHash;
using crucible::Philox;
using ::fixy::Philox4x32;

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    // volatile seeds so the compiler can't constant-fold the generate()
    // call out of existence. Live in outer scope so every lambda sees them.
    volatile uint64_t offset = 0x123456789abcdef0ULL;
    volatile uint64_t key = 0xcafebabedeadbeefULL;
    volatile uint32_t seed = 1;
    volatile uint32_t raw = 0x12345678;
    volatile uint32_t u1 = 0xAABBCCDD;
    volatile uint32_t u2 = 0x11223344;
    volatile uint64_t master = 42;
    volatile uint32_t op_idx = 7;
    const ContentHash content_hash{0xfeedfacecafeULL};

    auto make_ctr = [](uint32_t a) noexcept { return Philox4x32::Counter{a, a + 1, a + 2, a + 3}; };
    auto make_key = [](uint32_t a) noexcept { return Philox4x32::Key{a, a + 1}; };

    // 16 KB scratch for the streaming fill — alignas(64) so the store
    // path hits full cache-line writes.
    constexpr size_t BUF_WORDS = 4096;
    alignas(64) static ::fixy::FixedArray<std::uint32_t, BUF_WORDS> scratch{};

    bench::print("=== philox ===\n\n");

    ::fixy::FixedArray<bench::Report, 9> reports{
        bench::run("generate(offset, key)           [4×u32 = 16B]",
                   [&] {
                       auto ctr = Philox4x32::generate(offset, key);
                       bench::do_not_optimize(ctr);
                   }),
        bench::run("generate raw 4×32 Ctr+Key",
                   [&] {
                       auto r = Philox4x32::generate(make_ctr(seed), make_key(seed + 4));
                       bench::do_not_optimize(r);
                   }),
        // Counter increments per iteration — mimics per-thread invocation
        // in a parallel kernel. Key is constant for the run.
        [&] {
            uint64_t i = 0;
            return bench::run("generate streaming (i++, key)", [&] {
                auto ctr = Philox4x32::generate(i++, key);
                bench::do_not_optimize(ctr);
            });
        }(),
        bench::run("to_uniform(u32) → float",
                   [&] {
                       float f = Philox::to_uniform(raw);
                       bench::do_not_optimize(f);
                   }),
        bench::run("box_muller_polynomial_det(u32, u32) → 2×f32",
                   [&] {
                       auto pair = Philox::box_muller_polynomial_det(u1, u2).peek();
                       bench::do_not_optimize(pair);
                   }),
        bench::run("op_key_det(master, op_idx, hash).peek()",
                   [&] {
                       uint64_t k = Philox::op_key_det(master, op_idx, content_hash).peek();
                       bench::do_not_optimize(k);
                   }),
        // Streaming buffer fill: 4096 u32 = 1024 generate() calls.
        // Gives effective throughput per 16 KB batch.
        bench::run("fill 4096 u32 (16 KB batch)",
                   [&] {
                       std::uint64_t word = 0;
                       // The capture by reference keeps the volatile read of the key in each call.
                       scratch.with_mut([&word, &key](auto run) noexcept {
                           for (::fixy::View<std::uint32_t, 4> const four : run.view().template chunks<4>()) {
                               auto c = Philox4x32::generate(word, key);
                               four.at(::fixy::index<0>()) = c.at(::fixy::index<0>());
                               four.at(::fixy::index<1>()) = c.at(::fixy::index<1>());
                               four.at(::fixy::index<2>()) = c.at(::fixy::index<2>());
                               four.at(::fixy::index<3>()) = c.at(::fixy::index<3>());
                               word += 4;
                           }
                       });
                       // The sink reads each byte of the buffer, so each store
                       // of the fill stays.
                       bench::mark_changed(scratch);
                   }),

        // ── SIMD-9: batched generator ─────────────────────────────
        //
        // One philox_batch8 call = 8 scalar generate() calls' worth
        // of work (32 × uint32 = 128 bytes).  The straight-line
        // version measures pure per-batch latency.
        [&] {
            using foundation::simd::u32x8;
            const u32x8 ctr0(static_cast<uint32_t>(seed));
            const u32x8 ctr1(static_cast<uint32_t>(seed) + 1u);
            const u32x8 ctr2(0u);
            const u32x8 ctr3(0u);
            const u32x8 key0(static_cast<uint32_t>(key));
            const u32x8 key1(static_cast<uint32_t>(key >> 32));
            return bench::run("philox_batch8 (8×4×u32 = 128B)", [&] {
                auto out = crucible::detail::philox_batch8(ctr0, ctr1, ctr2, ctr3, key0, key1);
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
        [&] {
            using foundation::simd::u32x8;
            return bench::run("philox_batch8 fill 4096 u32 (16 KB batch)", [&] {
                const u32x8 key0(static_cast<uint32_t>(key));
                const u32x8 key1(static_cast<uint32_t>(key >> 32));
                // 32 u32 per batch8 call → 4096/32 = 128 calls.
                // Lane i of the i-th call sees counter
                // base = (call_idx * 8 + i) on ctr0; ctr1..3 stay 0.
                const u32x8 lane_offsets = foundation::simd::iota_v<u32x8>();
                std::uint32_t call_idx = 0;
                scratch.with_mut([&call_idx, &lane_offsets, &key0, &key1](auto run) noexcept {
                    for (::fixy::View<std::uint32_t, 32> const call : run.view().template chunks<32>()) {
                        const u32x8 base(call_idx * 8u);
                        const u32x8 ctr0 = base + lane_offsets;
                        const u32x8 ctr1(0u);
                        const u32x8 ctr2(0u);
                        const u32x8 ctr3(0u);
                        auto out = crucible::detail::philox_batch8(ctr0, ctr1, ctr2, ctr3, key0, key1);
                        // Four vector stores — 32 u32 per call in 4
                        // instructions instead of 32.  The window offsets are
                        // constants inside a run of 32 lanes, so the checks of
                        // the windows fold away.
                        foundation::simd::store(out.r0, call.window<8>(0).expect("a call fills 32 lanes"));
                        foundation::simd::store(out.r1, call.window<8>(8).expect("a call fills 32 lanes"));
                        foundation::simd::store(out.r2, call.window<8>(16).expect("a call fills 32 lanes"));
                        foundation::simd::store(out.r3, call.window<8>(24).expect("a call fills 32 lanes"));
                        ++call_idx;
                    }
                });
                bench::mark_changed(scratch);
            });
        }(),
    };

    bench::emit_reports_text(reports.view());

    // Determinism sanity check — same inputs, same bits. Philox is
    // stateless, so this must never fail; any drift means the algorithm
    // has been miscompiled or a wrong-endianness assumption crept in.
    auto r1 = Philox4x32::generate(uint64_t{1}, uint64_t{2});
    auto r2 = Philox4x32::generate(uint64_t{1}, uint64_t{2});
    if (r1 != r2) {
        ::fixy::report(::fixy::Sink::Err, "\n[FATAL] Philox non-deterministic!\n");
        return 1;
    }
    bench::print("\nphilox: deterministic, ok\n");

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
