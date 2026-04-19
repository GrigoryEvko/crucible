// CKernelTable::classify — binary-search dispatch lookup.
//
// Called once per op during build_trace() (bg thread).  At 146 real
// ops + ~50 aliases, the table typically holds ~200 entries: log2(200)
// ≈ 8 comparisons.  Target: ≤20 ns for hit, ≤20 ns for miss (OPAQUE).

#include <cstdint>
#include <cstdio>

#include <crucible/CKernel.h>

#include "bench_harness.h"

using crucible::CKernelId;
using crucible::CKernelTable;
using crucible::SchemaHash;

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    // Populate a realistic table: 200 entries spread over the hash space.
    // Shared across all three Runs; holds stable for their lifetime.
    constexpr uint32_t N = 200;
    CKernelTable table{};
    SchemaHash hit_hashes[N];
    for (uint32_t i = 0; i < N; i++) {
        const uint64_t h = 0x9E3779B97F4A7C15ULL * (i + 1);
        hit_hashes[i] = SchemaHash{h};
        const auto id = static_cast<CKernelId>(
            1 + (i % (static_cast<uint32_t>(CKernelId::NUM_KERNELS) - 1)));
        table.register_op(hit_hashes[i], id);
    }

    std::printf("=== ckernel ===\n");
    std::printf("  table entries: %u (log2 = ~8 probes)\n\n", N);

    bench::Report reports[] = {
        // Rotating hit — indices march forward, pointer-chasing into the
        // table alternates between cache lines the same way build_trace
        // would as it walks op by op.
        [&]{
            uint32_t idx = 0;
            return bench::run("classify hit  (200-entry table)", [&]{
                const auto h = hit_hashes[idx];
                idx = (idx + 1) % N;
                const auto id = table.classify(h);
                bench::do_not_optimize(id);
            });
        }(),

        // Miss — rolling LCG-generated hashes unlikely to collide with
        // any registered entry; exercises the full binary-search depth
        // before fallback to OPAQUE.
        [&]{
            uint64_t miss_key = 0xDEADBEEFCAFEBABEULL;
            return bench::run("classify miss (OPAQUE fallback)", [&]{
                miss_key = miss_key * 6364136223846793005ULL
                         + 1442695040888963407ULL;
                const auto id = table.classify(SchemaHash{miss_key});
                bench::do_not_optimize(id);
            });
        }(),

        // Branch-predictor-friendly: same hash every time. Lower bound on
        // classify's cost when the call site is on a hot path.
        [&]{
            const auto hot = hit_hashes[N / 2];
            return bench::run("classify hot  (same hash repeated)", [&]{
                const auto id = table.classify(hot);
                bench::do_not_optimize(id);
            });
        }(),
    };

    bench::emit_reports_text(reports);

    // Hit vs. hot: same classify() path, same table; the only difference
    // is branch predictability of the recurrent-vs-rotating access. The
    // delta is a clean measurement of the branch-predictor win.
    std::printf("\n=== compare ===\n");
    bench::compare(reports[0], reports[2]).print_text(stdout);

    bench::emit_reports_json(reports, json);
    return 0;
}
