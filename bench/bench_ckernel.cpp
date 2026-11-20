// CKernelTable::classify — binary-search dispatch lookup.
//
// Called once per op during build_trace() (bg thread).  At 146 real
// ops + ~50 aliases, the table typically holds ~200 entries: log2(200)
// ≈ 8 comparisons.  Target: ≤20 ns for hit, ≤20 ns for miss (OPAQUE).

#include <cstdint>

#include <crucible/CKernel.h>

#include "bench_harness.h"

using crucible::CKernelId;
using crucible::CKernelTable;
using crucible::SchemaHash;

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    // Populate a realistic table: 200 entries spread over the hash space.
    // Shared across all three Runs; holds stable for their lifetime.
    constexpr uint32_t N = 200;
    CKernelTable table{};
    // The view and each registration ask for the context of a Vigil's
    // producer claim.  The bench uses no Vigil, so it takes that context from
    // the test door.  The bench has one writer, so the writer of the claim
    // takes the section with one compare-exchange.
    constexpr crucible::VigilFgCtx writer = ::foundation::effects::testing::foreground<crucible::Vigil>();
    const auto mutable_view = table.mint_mutable_view(writer).expect("a fresh table mints a mutable view");
    ::fixy::FixedArray<SchemaHash, N> hit_hashes{};
    for (::fixy::Index<N> const i : hit_hashes.indices()) {
        const uint64_t h = 0x9E3779B97F4A7C15ULL * (i.raw() + 1);
        hit_hashes.at(i) = SchemaHash{h};
        const auto id = static_cast<CKernelId>(
            1 + (static_cast<uint32_t>(i.raw()) % (static_cast<uint32_t>(CKernelId::NUM_KERNELS) - 1)));
        const bool was_registered = table.register_op(writer, mutable_view, hit_hashes.at(i), id);
        CRUCIBLE_FATAL_INVARIANT(was_registered);
    }

    ::fixy::report(::fixy::Sink::Out, "=== ckernel ===\n");
    ::fixy::report(::fixy::Sink::Out, "  table entries: {} (log2 = ~8 probes)\n\n", N);

    ::fixy::FixedArray<bench::Report, 3> reports{
        // Rotating hit — indices march forward, pointer-chasing into the
        // table alternates between cache lines the same way build_trace
        // would as it walks op by op.
        [&] {
            // The rotation (idx + 1) % N stays below N, so the compiler folds
            // the check of the index and the body holds no branch for it.
            // The index is below N, so its 32-bit value is exact, and the
            // remainder is the 32-bit remainder of a counter of 32 bits.
            ::fixy::Index<N> idx = ::fixy::index<0>();
            return bench::run("classify hit  (200-entry table)", [&] {
                const auto h = hit_hashes.at(idx);
                idx = ::fixy::Index<N>::check((::fixy::wrapping_narrow<uint32_t>(idx.raw()) + 1) % N)
                          .expect("a rotation modulo N stays below N");
                const auto id = table.classify(h);
                bench::do_not_optimize(id);
            });
        }(),

        // Miss — rolling LCG-generated hashes unlikely to collide with
        // any registered entry; exercises the full binary-search depth
        // before fallback to OPAQUE.
        [&] {
            uint64_t miss_key = 0xDEADBEEFCAFEBABEULL;
            return bench::run("classify miss (OPAQUE fallback)", [&] {
                miss_key = miss_key * 6364136223846793005ULL + 1442695040888963407ULL;
                const auto id = table.classify(SchemaHash{miss_key});
                bench::do_not_optimize(id);
            });
        }(),

        // Branch-predictor-friendly: same hash every time. Lower bound on
        // classify's cost when the call site is on a hot path.
        [&] {
            const auto hot = hit_hashes.at(::fixy::index<N / 2>());
            return bench::run("classify hot  (same hash repeated)", [&] {
                const auto id = table.classify(hot);
                bench::do_not_optimize(id);
            });
        }(),
    };

    bench::emit_reports_text(reports.view());

    // Hit vs. hot: same classify() path, same table; the only difference
    // is branch predictability of the recurrent-vs-rotating access. The
    // delta is a clean measurement of the branch-predictor win.
    ::fixy::report(::fixy::Sink::Out, "\n=== compare ===\n");
    bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<2>())).print_text();

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
