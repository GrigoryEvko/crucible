// The slots that the build of a region gives the tensors of one iteration.
// A slot holds one storage, and the key of a storage is the address of its
// first byte.
//
// Op 0 writes a tensor at the address X, op 1 reads it and writes Y, and op 2
// reads Y.  In one run op 2 writes a new address Z.  In the other run X is
// dead after op 1, and the allocator gives the output of op 2 the address X.
// The two runs are one computation, so the content hash of the region must be
// the same for the two, and the new tensor at X takes a slot of its own.
//
// A view at an offset has a data pointer past the first byte of its storage,
// and it reads the slot of its base.  An op that returns the tensor that an op
// in its nest wrote, as linear returns the result of addmm, keeps the slot.
// Each read of one external storage reads one slot.

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

// A tensor of `elements` floats whose storage starts at the offset base of
// g_memory, at element `first` of that storage.  A tensor past the first
// element of its storage is a view.
[[nodiscard]] TensorMeta vector_at(uint32_t base, uint32_t first = 0, int64_t elements = 64) {
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(elements);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.layout = crucible::Layout::Strided;
    meta.storage_offset = first;
    if (first != 0) meta.flags = crucible::meta_flags::IS_VIEW;
    meta.data_ptr = crucible::external_data_ptr(
        &*g_memory.view().get(base + first * sizeof(float)).expect("the offset is in the memory"));
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

    // An op of the depth that reads the inputs and writes the outputs.  Its
    // records go to the log, the inputs first.
    void push_op(MetaLog& log, uint32_t op, ::fixy::View<const TensorMeta> inputs,
                 ::fixy::View<const TensorMeta> outputs, crucible::OpDepth depth = {}) {
        assert(count < CAPACITY);
        ::fixy::FixedArray<TensorMeta, 4> records{};
        std::size_t filled = 0;
        for (const TensorMeta& meta : inputs) {
            *records.view().get(filled).expect("an op of the test holds at most four tensors") = meta;
            ++filled;
        }
        for (const TensorMeta& meta : outputs) {
            *records.view().get(filled).expect("an op of the test holds at most four tensors") = meta;
            ++filled;
        }
        const MetaIndex index = log.try_append(
            ::fixy::View<const TensorMeta>{records.view()}.window(0, filled).expect("the records fit the array"));
        assert(index.is_valid());
        TraceRing::Entry& entry = crucible::test::element_at(trace, count);
        entry.schema_hash = crucible::SchemaHash{0x7000ULL + op};
        entry.shape_hash = crucible::ShapeHash{0x8000ULL + op};
        entry.num_inputs = static_cast<uint16_t>(inputs.size());
        entry.num_outputs = static_cast<uint16_t>(outputs.size());
        crucible::test::element_at(meta_starts, count) = index;
        crucible::test::element_at(depths, count) = depth;
        ++count;
    }

    // An op that writes the tensor output and reads no tensor.
    void push_source(MetaLog& log, uint32_t op, const TensorMeta& output) {
        const ::fixy::FixedArray<TensorMeta, 1> outputs{output};
        push_op(log, op, {}, outputs.view());
    }

    // An op that reads the tensor input and writes the tensor output.
    void push_unary(MetaLog& log, uint32_t op, const TensorMeta& input, const TensorMeta& output,
                    crucible::OpDepth depth = {}) {
        const ::fixy::FixedArray<TensorMeta, 1> inputs{input};
        const ::fixy::FixedArray<TensorMeta, 1> outputs{output};
        push_op(log, op, inputs.view(), outputs.view(), depth);
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

// The thread, the log and the work of one build.
struct Rig {
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    ::fixy::Box<MetaLog> log = ::fixy::mint_box<MetaLog>(::foundation::effects::testing::test().alloc,
                                                         ::foundation::effects::testing::test().alloc);
    Work work;

    Rig() { bt.meta_log.set(&*log); }
    Rig(const Rig&) = delete("the thread holds the address of the log");
    Rig& operator=(const Rig&) = delete("the thread holds the address of the log");

    [[nodiscard]] BackgroundThread::BuiltRegion build() { return work.build(bt); }
};

[[nodiscard]] crucible::SlotId input_slot(const BackgroundThread::BuiltRegion& region, uint32_t op, uint32_t input) {
    return *region.ops.view()
                .get(op)
                .expect("the region holds the op")
                ->input_slot_ids()
                .get(input)
                .expect("the op holds the input");
}

[[nodiscard]] crucible::SlotId output_slot(const BackgroundThread::BuiltRegion& region, uint32_t op, uint32_t output) {
    return *region.ops.view()
                .get(op)
                .expect("the region holds the op")
                ->output_slot_ids()
                .get(output)
                .expect("the op holds the output");
}

[[nodiscard]] crucible::OpIndex producer(const BackgroundThread::BuiltRegion& region, uint32_t op, uint32_t input) {
    return *region.ops.view()
                .get(op)
                .expect("the region holds the op")
                ->input_trace_indices()
                .get(input)
                .expect("the op holds the input");
}

[[nodiscard]] const crucible::TensorSlot& slot_of(const BackgroundThread::BuiltRegion& region, crucible::SlotId slot) {
    return *region.slots.view().get(slot.raw()).expect("the region holds the slot");
}

[[nodiscard]] BackgroundThread::BuiltRegion build_three_ops(uint32_t last_output) {
    Rig rig;
    rig.work.push_source(*rig.log, 0, vector_at(kX));
    rig.work.push_unary(*rig.log, 1, vector_at(kX), vector_at(kY));
    rig.work.push_unary(*rig.log, 2, vector_at(kY), vector_at(last_output));
    return rig.build();
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

// The output of op 2 at the address of the dead output of op 0 is a new
// storage: it takes a slot of its own, and the slot of op 0 dies at op 1.
void test_reused_address_gets_a_new_slot() {
    const BackgroundThread::BuiltRegion reused = build_three_ops(kX);
    assert(reused.slots.size() == 3 && "a new storage at the address of a dead one takes a slot of its own");
    const crucible::SlotId first = output_slot(reused, 0, 0);
    const crucible::SlotId last = output_slot(reused, 2, 0);
    assert(first != last);
    assert(slot_of(reused, first).death_op == crucible::OpIndex{1});
    assert(slot_of(reused, last).birth_op == crucible::OpIndex{2});
    assert(!slot_of(reused, first).is_external && !slot_of(reused, last).is_external);
    crucible::test::pass("  test_reused_address_gets_a_new_slot: PASSED\n");
}

// Op 0 writes the base B of 64 floats.  Op 1 reads the view of B from element
// 16, op 2 returns the view of B from element 32, and op 3 reads that view.
// Each view has the slot of B and the producer of the last write of B.
void test_view_at_an_offset_reads_its_base() {
    Rig rig;
    rig.work.push_source(*rig.log, 0, vector_at(kX));
    rig.work.push_unary(*rig.log, 1, vector_at(kX, 16, 48), vector_at(kY));
    rig.work.push_unary(*rig.log, 2, vector_at(kX), vector_at(kX, 32, 32));
    rig.work.push_unary(*rig.log, 3, vector_at(kX, 32, 32), vector_at(kZ));
    const BackgroundThread::BuiltRegion region = rig.build();

    const crucible::SlotId base = output_slot(region, 0, 0);
    assert(region.slots.size() == 3 && "the views of B take the slot of B, and no external slot");
    assert(input_slot(region, 1, 0) == base && producer(region, 1, 0) == crucible::OpIndex{0}
           && "a view at an offset reads the slot of its base");
    assert(output_slot(region, 2, 0) == base && "a view that an op returns keeps the slot of its base");
    assert(input_slot(region, 3, 0) == base && producer(region, 3, 0) == crucible::OpIndex{2});
    assert(!slot_of(region, base).is_external);
    assert(slot_of(region, base).nbytes == 64 * sizeof(float) && "the slot holds the span of each view of B");
    crucible::test::pass("  test_view_at_an_offset_reads_its_base: PASSED\n");
}

// Op 1 of depth 0 returns the tensor R that op 0 of depth 1, in its nest,
// wrote.  The record is in post-order, so op 0 comes first.  R is one
// storage, so the two outputs have one slot.  Op 2 of depth 0 writes R again
// after op 1: op 1 is no op of the nest of op 2, so the output of op 2 is a
// new storage at a reused address.
void test_return_of_the_nest_keeps_the_slot() {
    Rig rig;
    rig.work.push_unary(*rig.log, 0, vector_at(kX), vector_at(kY), crucible::OpDepth{1});
    rig.work.push_unary(*rig.log, 1, vector_at(kX), vector_at(kY), crucible::OpDepth{0});
    rig.work.push_unary(*rig.log, 2, vector_at(kZ), vector_at(kY), crucible::OpDepth{0});
    const BackgroundThread::BuiltRegion region = rig.build();

    assert(output_slot(region, 1, 0) == output_slot(region, 0, 0) && "the tensor of the nest keeps its slot");
    assert(output_slot(region, 2, 0) != output_slot(region, 1, 0) && "a sibling writes a new storage");

    // The nest of linear in the record of an MLP: copy_ of depth 2 writes the
    // bias into the result R in place, addmm of depth 1 writes R, and linear
    // of depth 0 returns R.  The three outputs are one storage.
    Rig chain;
    const ::fixy::FixedArray<TensorMeta, 2> copy_inputs{vector_at(kY), vector_at(kZ)};
    const ::fixy::FixedArray<TensorMeta, 1> result{vector_at(kY)};
    chain.work.push_op(*chain.log, 0, copy_inputs.view(), result.view(), crucible::OpDepth{2});
    chain.work.push_unary(*chain.log, 1, vector_at(kX), vector_at(kY), crucible::OpDepth{1});
    chain.work.push_unary(*chain.log, 2, vector_at(kX), vector_at(kY), crucible::OpDepth{0});
    const BackgroundThread::BuiltRegion nested = chain.build();
    const crucible::SlotId copied = output_slot(nested, 0, 0);
    assert(output_slot(nested, 1, 0) == copied && output_slot(nested, 2, 0) == copied);
    assert(input_slot(nested, 0, 0) == copied);
    crucible::test::pass("  test_return_of_the_nest_keeps_the_slot: PASSED\n");
}

// The ops of the first layer of the MLP of test_region_ops.py, in the order
// of the record: copy_ of depth 2 writes the bias into the new result R in
// place, addmm of depth 1 writes R, linear of depth 0 returns R, and relu
// reads R.  copy_ meets R first, as an input, but addmm made R, so R is a
// slot of the region and takes bytes of the pool.  A parameter W that a
// nested op meets first and that its parent reads and returns stays external.
void test_result_of_a_nest_is_internal() {
    constexpr uint32_t kW = 3072;
    Rig mlp;
    const ::fixy::FixedArray<TensorMeta, 2> copy_inputs{vector_at(kY), vector_at(kZ)};
    const ::fixy::FixedArray<TensorMeta, 2> addmm_inputs{vector_at(kZ), vector_at(kX)};
    const ::fixy::FixedArray<TensorMeta, 2> linear_inputs{vector_at(kX), vector_at(kZ)};
    const ::fixy::FixedArray<TensorMeta, 1> result{vector_at(kY)};
    mlp.work.push_op(*mlp.log, 0, copy_inputs.view(), result.view(), crucible::OpDepth{2});
    mlp.work.push_op(*mlp.log, 1, addmm_inputs.view(), result.view(), crucible::OpDepth{1});
    mlp.work.push_op(*mlp.log, 2, linear_inputs.view(), result.view(), crucible::OpDepth{0});
    mlp.work.push_unary(*mlp.log, 3, vector_at(kY), vector_at(kW));
    const BackgroundThread::BuiltRegion layer = mlp.build();

    const crucible::SlotId made = output_slot(layer, 2, 0);
    assert(!slot_of(layer, made).is_external && "the result that addmm makes is a slot of the region");
    assert(slot_of(layer, made).birth_op == crucible::OpIndex{0}
           && slot_of(layer, made).death_op == crucible::OpIndex{3});
    assert(input_slot(layer, 3, 0) == made && producer(layer, 3, 0) == crucible::OpIndex{2});
    assert(slot_of(layer, input_slot(layer, 0, 1)).is_external && "the bias lives before the region");
    assert(slot_of(layer, input_slot(layer, 1, 1)).is_external && "the input x lives before the region");

    Rig param;
    param.work.push_unary(*param.log, 0, vector_at(kX), vector_at(kX), crucible::OpDepth{1});
    param.work.push_unary(*param.log, 1, vector_at(kX), vector_at(kX), crucible::OpDepth{0});
    const BackgroundThread::BuiltRegion returned = param.build();
    assert(slot_of(returned, output_slot(returned, 1, 0)).is_external
           && "a storage that the parent reads lives before it");
    assert(slot_of(returned, output_slot(returned, 1, 0)).birth_op == crucible::OpIndex{0});
    crucible::test::pass("  test_result_of_a_nest_is_internal: PASSED\n");
}

// Two ops read the external storage X, the first one at the first element
// and the second one at element 8.  The two reads have one external slot and
// no producer.
void test_reads_of_an_external_take_one_slot() {
    Rig rig;
    rig.work.push_unary(*rig.log, 0, vector_at(kX), vector_at(kY));
    rig.work.push_unary(*rig.log, 1, vector_at(kX, 8, 56), vector_at(kZ));
    const BackgroundThread::BuiltRegion region = rig.build();

    const crucible::SlotId external = input_slot(region, 0, 0);
    assert(input_slot(region, 1, 0) == external && "each read of one external storage reads one slot");
    assert(!producer(region, 1, 0).is_valid());
    assert(slot_of(region, external).is_external && slot_of(region, external).death_op == crucible::OpIndex{1});
    assert(region.slots.size() == 3);
    crucible::test::pass("  test_reads_of_an_external_take_one_slot: PASSED\n");
}

}  // namespace

int main() {
    // The kernel classification of a build reads the global tables, which
    // start() seals.  These tests build without start(), so they seal here.
    crucible::global_schema_table().seal();
    crucible::global_ckernel_table().value()->seal();
    test_reused_address_keeps_the_hash();
    test_reused_address_gets_a_new_slot();
    test_view_at_an_offset_reads_its_base();
    test_return_of_the_nest_keeps_the_slot();
    test_result_of_a_nest_is_internal();
    test_reads_of_an_external_take_one_slot();
    crucible::test::pass("test_build_address_reuse: ALL PASSED\n");
    return 0;
}
