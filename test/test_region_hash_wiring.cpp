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

// The arrays of one region of three ops: op 0 and op 1 each write one output
// of the extent of their slot, and op 2 reads the two outputs in the order of
// first_read and second_read.  The ops keep the Views of their inputs and
// their outputs, so these are runs of the arena of the region.
struct Region {
    ::fixy::BumpArena<crucible::DagArena> arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{4096});
    ::fixy::ArenaRun<TensorMeta, crucible::DagArena> out_a =
        ::fixy::check::run_of(kTest, g_test.alloc, arena, vector_meta(4));
    ::fixy::ArenaRun<SlotId, crucible::DagArena> slot_a = ::fixy::check::run_of(kTest, g_test.alloc, arena, SlotId{0});
    ::fixy::ArenaRun<TensorMeta, crucible::DagArena> out_b =
        ::fixy::check::run_of(kTest, g_test.alloc, arena, vector_meta(4));
    ::fixy::ArenaRun<SlotId, crucible::DagArena> slot_b = ::fixy::check::run_of(kTest, g_test.alloc, arena, SlotId{1});
    ::fixy::ArenaRun<TensorMeta, crucible::DagArena> in_c =
        ::fixy::check::run_of(kTest, g_test.alloc, arena, vector_meta(4), vector_meta(4));
    ::fixy::ArenaRun<OpIndex, crucible::DagArena> producers_c{};
    ::fixy::ArenaRun<SlotId, crucible::DagArena> slots_c{};
    ::fixy::FixedArray<TraceEntry, 3> ops{};

    Region(uint32_t first_read, uint32_t second_read, int64_t extent_a, OpDepth depth_a) {
        *out_a.view().get(0).expect("the run holds one output") = vector_meta(extent_a);
        producers_c = ::fixy::check::run_of(kTest, g_test.alloc, arena, OpIndex{first_read}, OpIndex{second_read});
        slots_c = ::fixy::check::run_of(kTest, g_test.alloc, arena, SlotId{first_read}, SlotId{second_read});
        ops.with_element(::fixy::index<0>(), [outputs = ::fixy::ArenaRun<TensorMeta const, crucible::DagArena>{out_a},
                                              output_slots = slot_a, depth_a](TraceEntry& op_a) noexcept {
            op_a.schema_hash = SchemaHash{0xA};
            op_a.set_outputs(outputs, output_slots);
            op_a.depth = depth_a;
        });
        ops.with_element(::fixy::index<1>(), [outputs = ::fixy::ArenaRun<TensorMeta const, crucible::DagArena>{out_b},
                                              output_slots = slot_b](TraceEntry& op_b) noexcept {
            op_b.schema_hash = SchemaHash{0xB};
            op_b.set_outputs(outputs, output_slots);
        });
        ops.with_element(::fixy::index<2>(),
                         [inputs = ::fixy::ArenaRun<TensorMeta const, crucible::DagArena>{in_c},
                          producers = producers_c, input_slots = slots_c](TraceEntry& op_c) noexcept {
                             op_c.schema_hash = SchemaHash{0xC};
                             op_c.set_inputs(inputs, producers, input_slots);
                         });
    }

    Region(const Region&) = delete("the ops of a region point into its arrays");
    Region& operator=(const Region&) = delete("the ops of a region point into its arrays");

    [[nodiscard]] ContentHash hash() const { return crucible::compute_content_hash(ops.view()); }
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
