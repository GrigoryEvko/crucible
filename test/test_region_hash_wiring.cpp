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

#include "test_assert.h"

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

// The arrays of one region of three ops: op 0 and op 1 each write one output
// of the extent of their slot, and op 2 reads the two outputs in the order of
// first_read and second_read.
struct Region {
    ::fixy::FixedArray<TensorMeta, 1> out_a{vector_meta(4)};
    ::fixy::FixedArray<SlotId, 1> slot_a{SlotId{0}};
    ::fixy::FixedArray<TensorMeta, 1> out_b{vector_meta(4)};
    ::fixy::FixedArray<SlotId, 1> slot_b{SlotId{1}};
    ::fixy::FixedArray<TensorMeta, 2> in_c{vector_meta(4), vector_meta(4)};
    ::fixy::FixedArray<OpIndex, 2> producers_c{};
    ::fixy::FixedArray<SlotId, 2> slots_c{};
    ::fixy::FixedArray<TraceEntry, 3> ops{};

    Region(uint32_t first_read, uint32_t second_read, int64_t extent_a, OpDepth depth_a) {
        out_a.at(::fixy::index<0>()) = vector_meta(extent_a);
        producers_c = ::fixy::FixedArray<OpIndex, 2>{OpIndex{first_read}, OpIndex{second_read}};
        slots_c = ::fixy::FixedArray<SlotId, 2>{SlotId{first_read}, SlotId{second_read}};
        TraceEntry& op_a = ops.at(::fixy::index<0>());
        op_a.schema_hash = SchemaHash{0xA};
        op_a.set_outputs(out_a.view(), slot_a.view());
        op_a.depth = depth_a;
        TraceEntry& op_b = ops.at(::fixy::index<1>());
        op_b.schema_hash = SchemaHash{0xB};
        op_b.set_outputs(out_b.view(), slot_b.view());
        TraceEntry& op_c = ops.at(::fixy::index<2>());
        op_c.schema_hash = SchemaHash{0xC};
        op_c.set_inputs(in_c.view(), producers_c.view(), slots_c.view());
    }

    Region(const Region&) = delete("the ops of a region point into its arrays");
    Region& operator=(const Region&) = delete("the ops of a region point into its arrays");

    [[nodiscard]] ContentHash hash() const { return crucible::compute_content_hash(ops.view()); }
};

void test_swapped_wiring_hashes_apart() {
    const Region forward{0, 1, 4, OpDepth{}};
    const Region again{0, 1, 4, OpDepth{}};
    const Region swapped{1, 0, 4, OpDepth{}};
    assert(forward.hash() == again.hash() && "one region hashes the same each time");
    assert(!(forward.hash() == swapped.hash()) && "the producer of each input folds into the region hash");
    crucible::test::pass("  test_swapped_wiring_hashes_apart: PASSED\n");
}

void test_output_metadata_hashes_apart() {
    const Region narrow{0, 1, 4, OpDepth{}};
    const Region wide{0, 1, 8, OpDepth{}};
    assert(!(narrow.hash() == wide.hash()) && "the metadata of each output folds into the region hash");
    crucible::test::pass("  test_output_metadata_hashes_apart: PASSED\n");
}

void test_nesting_hashes_apart() {
    const Region after{0, 1, 4, OpDepth{}};
    const Region inside{0, 1, 4, OpDepth{1}};
    assert(!(after.hash() == inside.hash()) && "the depth of each op folds into the region hash");
    crucible::test::pass("  test_nesting_hashes_apart: PASSED\n");
}

}  // namespace

int main() {
    test_swapped_wiring_hashes_apart();
    test_output_metadata_hashes_apart();
    test_nesting_hashes_apart();
    crucible::test::pass("test_region_hash_wiring: ALL PASSED\n");
    return 0;
}
