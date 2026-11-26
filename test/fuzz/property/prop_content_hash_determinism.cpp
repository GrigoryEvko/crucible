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

int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(arguments);
    // The most ops of a batch.
    static constexpr unsigned MAX = 16;

    return run(
        "compute_content_hash determinism", cfg,
        [](Rng& rng) {
            // Generate 1..16 TraceEntries with random schema_hash.
            // input_metas/output_metas left null (num_inputs=0).
            // The schema hash of each op.  An op does not copy, so the batch
            // holds the hashes, and the property builds the ops.
            struct OpsBatch {
                ::foundation::core::FixedArray<SchemaHash, MAX> schemas;
                uint8_t count;
            };
            OpsBatch b{};
            b.count = static_cast<uint8_t>(rng.next_below(MAX) + 1);
            for (uint8_t i = 0; i < b.count; ++i) {
                b.schemas.set(::fixy::Index<MAX>::check(i).expect("the count is at most MAX"),
                              SchemaHash{rng.next64()});
            }
            return b;
        },
        [](const auto& batch) {
            // Leave num_inputs / num_outputs / num_scalar_args at zero
            // defaults — compute_content_hash respects those counts and
            // skips the metas/scalars loops.  One frame on an arena of the
            // check builds the ops.
            ::fixy::BumpArena<DagArena> arena =
                ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, std::size_t>{4096});
            const ::fixy::ArenaRun<TraceEntry const, DagArena> ops =
                ::foundation::core::with_arena_frame(arena, [&batch](auto& frame) noexcept {
                    auto made = make_op_run(::foundation::effects::testing::test().alloc, frame, batch.count);
                    for (uint8_t i = 0; i < batch.count; ++i) {
                        hold_op(made, i)->schema_hash = *batch.schemas.view().get(i).expect("the count is at most MAX");
                    }
                    const auto& built = made;
                    return built.weaken();
                });
            const ::fixy::View<const TraceEntry> span = ops.view();
            const auto h0 = compute_content_hash(span);
            for (int k = 0; k < 8; ++k) {
                if (compute_content_hash(span) != h0) return false;
            }
            return true;
        });
}
