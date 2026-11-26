// The content hash of a region folds the wiring of its ops, the metadata of
// their outputs, and their nesting depths.
//
// Two regions with the same ops and the same input metadata can still differ:
//   - Op 2 reads the output of op 0 and then the output of op 1, or the two in
//     the other order.  The producer of each input and its slot tell them apart.
//   - Op 0 writes an output of 4 elements or of 8.  Op 1 reads a tensor of
//     its own, so no input of the region shows the difference.
//   - The post-order "op 0, op 1" is the list of "op 0 inside op 1" and of
//     "op 0, then op 1".  The depth of op 0 tells them apart.
// A region hash that misses one of these gives two different computations
// one key of the kernel cache.

#include <crucible/MerkleDag.h>
#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

#include <cstdint>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using crucible::ContentHash;
using crucible::OpDepth;
using crucible::OpIndex;
using crucible::SchemaHash;
using crucible::SlotId;
using crucible::TensorMeta;
using crucible::TraceEntry;

[[nodiscard]] TensorMeta vector_meta(int64_t extent) {
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(extent);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    return meta;
}

// The capabilities of the test: the arena of each region takes its alloc.
auto g_test = ::foundation::effects::testing::test();

// One region of three ops: op 0 and op 1 each write one output of the extent
// of their slot, and op 2 reads the two outputs in the order of first_read and
// second_read.  One frame on the arena of the region builds the ops and their
// arrays, so the arrays of each op are in the arena of the op.
struct Region {
    ::fixy::BumpArena<crucible::DagArena> arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{4096});
    ::fixy::ArenaRun<TraceEntry const, crucible::DagArena> ops;

    Region(uint32_t first_read, uint32_t second_read, int64_t extent_a, OpDepth depth_a)
        : ops{build_ops_(arena, first_read, second_read, extent_a, depth_a)} {}

    Region(const Region&) = delete("the ops of a region point into its arrays");
    Region& operator=(const Region&) = delete("the ops of a region point into its arrays");

    [[nodiscard]] ContentHash hash() const { return crucible::compute_content_hash(ops.view()); }

private:
    [[nodiscard]] static ::fixy::ArenaRun<TraceEntry const, crucible::DagArena>
    build_ops_(::fixy::BumpArena<crucible::DagArena>& arena, uint32_t first_read, uint32_t second_read,
               int64_t extent_a, OpDepth depth_a) {
        return ::foundation::core::with_arena_frame(
            arena, [first_read, second_read, extent_a, depth_a](auto& frame) noexcept {
                const auto alloc = g_test.alloc;
                auto ops_run = crucible::make_op_run(alloc, frame, 3);
                const auto op_a = crucible::hold_op(ops_run, 0);
                op_a->schema_hash = SchemaHash{0xA};
                op_a->depth = depth_a;
                TraceEntry::set_outputs(frame, op_a, ::fixy::check::run_of(kTest, alloc, frame, vector_meta(extent_a)),
                                        ::fixy::check::run_of(kTest, alloc, frame, SlotId{0}));
                const auto op_b = crucible::hold_op(ops_run, 1);
                op_b->schema_hash = SchemaHash{0xB};
                TraceEntry::set_outputs(frame, op_b, ::fixy::check::run_of(kTest, alloc, frame, vector_meta(4)),
                                        ::fixy::check::run_of(kTest, alloc, frame, SlotId{1}));
                const auto op_c = crucible::hold_op(ops_run, 2);
                op_c->schema_hash = SchemaHash{0xC};
                TraceEntry::set_inputs(
                    frame, op_c, ::fixy::check::run_of(kTest, alloc, frame, vector_meta(4), vector_meta(4)),
                    ::fixy::check::run_of(kTest, alloc, frame, OpIndex{first_read}, OpIndex{second_read}),
                    ::fixy::check::run_of(kTest, alloc, frame, SlotId{first_read}, SlotId{second_read}));
                const auto& built = ops_run;
                return built.weaken();
            });
    }
};

void test_swapped_wiring_hashes_apart() {
    const Region forward{0, 1, 4, OpDepth{}};
    const Region again{0, 1, 4, OpDepth{}};
    const Region swapped{1, 0, 4, OpDepth{}};
    CRUCIBLE_TEST_ASSERT(kTest, forward.hash() == again.hash() && "one region hashes the same each time");
    CRUCIBLE_TEST_ASSERT(kTest, !(forward.hash() == swapped.hash())
                                    && "the producer of each input folds into the region hash");
    ::fixy::check::pass(kTest, "  test_swapped_wiring_hashes_apart: PASSED\n");
}

void test_output_metadata_hashes_apart() {
    const Region narrow{0, 1, 4, OpDepth{}};
    const Region wide{0, 1, 8, OpDepth{}};
    CRUCIBLE_TEST_ASSERT(kTest,
                         !(narrow.hash() == wide.hash()) && "the metadata of each output folds into the region hash");
    ::fixy::check::pass(kTest, "  test_output_metadata_hashes_apart: PASSED\n");
}

void test_nesting_hashes_apart() {
    const Region after{0, 1, 4, OpDepth{}};
    const Region inside{0, 1, 4, OpDepth{1}};
    CRUCIBLE_TEST_ASSERT(kTest, !(after.hash() == inside.hash()) && "the depth of each op folds into the region hash");
    ::fixy::check::pass(kTest, "  test_nesting_hashes_apart: PASSED\n");
}

}  // namespace

int main() {
    test_swapped_wiring_hashes_apart();
    test_output_metadata_hashes_apart();
    test_nesting_hashes_apart();
    ::fixy::check::pass(kTest, "test_region_hash_wiring: ALL PASSED\n");
    return 0;
}
