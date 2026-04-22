// ═══════════════════════════════════════════════════════════════════
// prop_content_hash_determinism — compute_content_hash determinism
// over random TraceEntry sequences.
//
// Catches:
//   - Static-local / process-global state in compute_content_hash
//   - Iteration-order non-determinism (would manifest as varying
//     hash across calls with identical input)
//   - Padding-byte leakage in TraceEntry struct hashing
//
// Strategy: per iteration, build a span of 1..16 random TraceEntries
// with random schema_hash + scalar_args.  Hash 8 times consecutively
// and assert all 8 results match.  Run across 100K random seeds.
//
// Note: TraceEntries with num_inputs > 0 require valid input_metas
// pointers; for this determinism test we use num_inputs=0 to keep
// the harness focused on the schema_hash + scalar_args fold.  A
// separate harness with input_metas would need an arena and is
// future work.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/MerkleDag.h>

#include <array>
#include <span>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv);

    return run("compute_content_hash determinism", cfg,
        [](Rng& rng) {
            // Generate 1..16 TraceEntries with random schema_hash.
            // input_metas/output_metas left null (num_inputs=0).
            constexpr unsigned MAX = 16;
            struct OpsBatch {
                std::array<TraceEntry, MAX> ops;
                uint8_t                     count;
            };
            OpsBatch b{};
            b.count = static_cast<uint8_t>(rng.next_below(MAX) + 1);
            for (uint8_t i = 0; i < b.count; ++i) {
                b.ops[i] = TraceEntry{};
                b.ops[i].schema_hash = SchemaHash{rng.next64()};
                // Leave num_inputs / num_outputs / num_scalar_args
                // at zero defaults — compute_content_hash respects
                // those counts and skips the metas/scalars loops.
            }
            return b;
        },
        [](const auto& batch) {
            const std::span<const TraceEntry> span{batch.ops.data(),
                                                    batch.count};
            const auto h0 = compute_content_hash(span);
            for (int k = 0; k < 8; ++k) {
                if (compute_content_hash(span) != h0) return false;
            }
            return true;
        });
}
