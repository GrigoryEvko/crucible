// The build of a region from the ring entries of one iteration, when the
// allocator gives a new tensor the address of a dead one.
//
// Op 0 writes a tensor at the address X, op 1 reads it and writes Y, and op 2
// reads Y.  In one run op 2 writes a new address Z.  In the other run X is
// dead after op 1, and the allocator gives the output of op 2 the address X.
// The two runs are one computation, so the content hash of the region must be
// the same for the two.

#include <crucible/BackgroundThread.h>
#include <crucible/CKernel.h>
#include <crucible/MetaLog.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <foundation/effects/Effect.h>

#include "test_assert.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using crucible::BackgroundThread;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TensorMeta;
using crucible::TraceRing;

// The memory that the data addresses of the tensors point into.  The build
// reads an address as a key and never through it.
::fixy::FixedArray<std::byte, 4096> g_memory{};

// A tensor of 64 floats at the offset of g_memory.
[[nodiscard]] TensorMeta vector_at(uint32_t offset) {
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(64);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.layout = crucible::Layout::Strided;
    meta.data_ptr = crucible::external_data_ptr(&*g_memory.view().get(offset).expect("the offset is in the memory"));
    return meta;
}

// The parallel arrays of one work of at most CAPACITY ops, for build_trace_from.
struct Work {
    static constexpr uint32_t CAPACITY = 8;
    uint32_t count = 0;
    ::fixy::FixedArray<TraceRing::Entry, CAPACITY> trace{};
    ::fixy::FixedArray<MetaIndex, CAPACITY> meta_starts{};
    ::fixy::FixedArray<crucible::ScopeHash, CAPACITY> scope_hashes{};
    ::fixy::FixedArray<crucible::CallsiteHash, CAPACITY> callsite_hashes{};
    ::fixy::FixedArray<crucible::OpDepth, CAPACITY> depths{};

    // An op that reads the tensor at the offset input, when input is not
    // kNoInput, and writes the tensor at the offset output.  Its records go
    // to the log.
    static constexpr uint32_t kNoInput = UINT32_MAX;
    void push_op(MetaLog& log, uint32_t op, uint32_t input, uint32_t output) {
        assert(count < CAPACITY);
        const bool has_input = input != kNoInput;
        const ::fixy::FixedArray<TensorMeta, 2> records{vector_at(has_input ? input : output), vector_at(output)};
        const uint16_t inputs = has_input ? 1 : 0;
        // The records of the op: the output alone, or the input and the output.
        const std::size_t first = has_input ? 0 : 1;
        const MetaIndex index = log.try_append(::fixy::View<const TensorMeta>{records.view()}
                                                   .window(first, records.size() - first)
                                                   .expect("the records of the op fit the array"));
        assert(index.is_valid());
        TraceRing::Entry& entry = crucible::test::element_at(trace, count);
        entry.schema_hash = crucible::SchemaHash{0x7000ULL + op};
        entry.shape_hash = crucible::ShapeHash{0x8000ULL + op};
        entry.num_inputs = inputs;
        entry.num_outputs = 1;
        crucible::test::element_at(meta_starts, count) = index;
        ++count;
    }

    template <class T>
    [[nodiscard]] ::fixy::View<const T> first_ops(::fixy::FixedArray<T, CAPACITY> const& column) const {
        return column.view().window(0, count).expect("a work holds at most CAPACITY ops");
    }

    [[nodiscard]] BackgroundThread::BuiltRegion build(BackgroundThread& bt) const {
        ::fixy::BumpArena scratch{size_t{1} << 16};
        auto built = bt.build_trace_from(::foundation::effects::testing::test().alloc, scratch, first_ops(trace),
                                         first_ops(meta_starts), first_ops(scope_hashes), first_ops(callsite_hashes),
                                         first_ops(depths));
        return std::move(built).expect("a work with the metadata of each op builds").region;
    }
};

// The offsets of the three addresses of the tensors.
constexpr uint32_t kX = 0;
constexpr uint32_t kY = 1024;
constexpr uint32_t kZ = 2048;

[[nodiscard]] BackgroundThread::BuiltRegion build_three_ops(uint32_t last_output) {
    const auto test = ::foundation::effects::testing::test();
    BackgroundThread bt{test.alloc};
    ::fixy::Box<MetaLog> log = ::fixy::mint_box<MetaLog>(test.alloc, test.alloc);
    bt.meta_log.set(&*log);
    Work work;
    work.push_op(*log, 0, Work::kNoInput, kX);
    work.push_op(*log, 1, kX, kY);
    work.push_op(*log, 2, kY, last_output);
    return work.build(bt);
}

void test_reused_address_keeps_the_hash() {
    const BackgroundThread::BuiltRegion fresh = build_three_ops(kZ);
    const BackgroundThread::BuiltRegion reused = build_three_ops(kX);
    assert(fresh.content_hash == reused.content_hash
           && "the content hash does not depend on the address that the allocator gives a new tensor");
    assert(fresh.content_hash == crucible::compute_content_hash(fresh.ops.view()));
    assert(reused.content_hash == crucible::compute_content_hash(reused.ops.view()));
    crucible::test::pass("  test_reused_address_keeps_the_hash: PASSED\n");
}

}  // namespace

int main() {
    // The kernel classification of a build reads the global tables, which
    // start() seals.  These tests build without start(), so they seal here.
    crucible::global_schema_table().seal();
    crucible::global_ckernel_table().value()->seal();
    test_reused_address_keeps_the_hash();
    crucible::test::pass("test_build_address_reuse: ALL PASSED\n");
    return 0;
}
