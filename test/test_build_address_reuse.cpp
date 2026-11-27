// The slots that the build of a region gives the tensors of one iteration.
// A slot holds one storage, and the key of a storage is the address of its
// first byte and the generation of its allocation, or the period of the
// allocator for a storage with an unknown generation.
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
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "trace_rig.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using crucible::BackgroundThread;
using crucible::BuiltRegion;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TensorMeta;
using crucible::TraceRing;

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

// Four known generations, and the unknown generations of two periods of the
// allocator.
constexpr crucible::StorageGeneration kKnownFirst = crucible::StorageGeneration::known(crucible::AllocationCount{1});
constexpr crucible::StorageGeneration kKnownSecond = crucible::StorageGeneration::known(crucible::AllocationCount{2});
constexpr crucible::StorageGeneration kKnownThird = crucible::StorageGeneration::known(crucible::AllocationCount{3});
constexpr crucible::StorageGeneration kKnownFourth = crucible::StorageGeneration::known(crucible::AllocationCount{4});
constexpr crucible::StorageGeneration kUnknownInFirstPeriod =
    crucible::StorageGeneration::unknown(crucible::AllocatorEpoch{1});
constexpr crucible::StorageGeneration kUnknownInSecondPeriod =
    crucible::StorageGeneration::unknown(crucible::AllocatorEpoch{2});

// A tensor of `elements` floats whose storage starts at the offset base of
// the memory, at element `first` of that storage, with the storage
// generation `generation`.  A tensor past the first element of its storage
// is a view.  The memory starts at the synthetic address 0x10000: the build
// reads an address as a key and never through it.
[[nodiscard]] TensorMeta vector_at(uint32_t base, uint32_t first = 0, int64_t elements = 64,
                                   crucible::StorageGeneration generation = {}) {
    TensorMeta meta{};
    meta.storage_generation = generation;
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(elements);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.layout = crucible::Layout::Strided;
    meta.storage_offset = first;
    if (first != 0) meta.set_flags(crucible::meta_flags::IS_VIEW);
    meta.data_ptr =
        crucible::mint_synthetic_data_ptr(crucible::SyntheticAddress{0x10000 + base + first * sizeof(float)});
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
    void push_op(crucible::test::RigLog& log, uint32_t op, ::fixy::View<const TensorMeta> inputs,
                 ::fixy::View<const TensorMeta> outputs, crucible::OpDepth depth = {}) {
        CRUCIBLE_TEST_ASSERT(kTest, count < CAPACITY);
        ::fixy::FixedArray<TensorMeta, 4> records{};
        std::size_t filled = 0;
        for (const TensorMeta& meta : inputs) {
            records.set(::fixy::Index<4>::check(filled).expect("an op of the test holds at most four tensors"), meta);
            ++filled;
        }
        for (const TensorMeta& meta : outputs) {
            records.set(::fixy::Index<4>::check(filled).expect("an op of the test holds at most four tensors"), meta);
            ++filled;
        }
        const MetaIndex index = log.append_metas(
            ::fixy::View<const TensorMeta>{records.view()}.window(0, filled).expect("the records fit the array"));
        CRUCIBLE_TEST_ASSERT(kTest, index.is_valid());
        const ::fixy::Index<CAPACITY> slot =
            ::fixy::Index<CAPACITY>::check(count).expect("a test reads an element at or past the count of its array");
        trace.update(
            slot, [op, input_count = inputs.size(), output_count = outputs.size()](TraceRing::Entry& entry) noexcept {
                entry.schema_hash = crucible::SchemaHash{0x7000ULL + op};
                entry.shape_hash = crucible::ShapeHash{0x8000ULL + op};
                entry.num_inputs = static_cast<uint16_t>(input_count);
                entry.num_outputs = static_cast<uint16_t>(output_count);
            });
        meta_starts.set(slot, index);
        depths.set(slot, depth);
        ++count;
    }

    // An op that writes the tensor output and reads no tensor.
    void push_source(crucible::test::RigLog& log, uint32_t op, const TensorMeta& output) {
        const ::fixy::FixedArray<TensorMeta, 1> outputs{output};
        push_op(log, op, {}, outputs.view());
    }

    // An op that reads the tensor input and writes the tensor output.
    void push_unary(crucible::test::RigLog& log, uint32_t op, const TensorMeta& input, const TensorMeta& output,
                    crucible::OpDepth depth = {}) {
        const ::fixy::FixedArray<TensorMeta, 1> inputs{input};
        const ::fixy::FixedArray<TensorMeta, 1> outputs{output};
        push_op(log, op, inputs.view(), outputs.view(), depth);
    }

    template <class T>
    [[nodiscard]] ::fixy::View<const T> first_ops(::fixy::FixedArray<T, CAPACITY> const& column
                                                  CRUCIBLE_LIFETIMEBOUND) const {
        return column.view().window(0, count).expect("a work holds at most CAPACITY ops");
    }
    template <class T>
    void first_ops(::fixy::FixedArray<T, CAPACITY> const&&) const =
        delete("the View borrows the column, and a temporary ends with the full expression");

    // The region of the work.  The bodies of the frame and of the epoch capture
    // the log by a const reference, and not the thread.
    template <class Stopped>
    [[nodiscard]] BuiltRegion build(Stopped& stopped, MetaLog const& meta_log) const {
        crucible::NumericsHash const numerics = stopped.numerics();
        return stopped.with_trace_build_state(
            [this, &meta_log, numerics](BackgroundThread::TraceBuildState& state) noexcept {
                crucible::TraceScratch scratch{Bytes{size_t{1} << 16}};
                ::fixy::Option<BuiltRegion> region{};
                scratch.arena.with_epoch(
                    [this, &state, &meta_log, numerics, &region](crucible::TraceScratch::Epoch& epoch) noexcept {
                        auto built = BackgroundThread::build_trace_from(
                            state, meta_log, ::foundation::effects::testing::test().alloc, epoch, first_ops(trace),
                            first_ops(meta_starts), first_ops(scope_hashes), first_ops(callsite_hashes),
                            first_ops(depths), numerics);
                        region = ::fixy::Option<BuiltRegion>::some(
                            std::move(built).expect("a work with the metadata of each op builds").region);
                    });
                return std::move(region).expect("the epoch built the region");
            });
    }
};

// The offsets of the three addresses of the tensors.
constexpr uint32_t kX = 0;
constexpr uint32_t kY = 1024;
constexpr uint32_t kZ = 2048;

// The ring and the log of a thread, and the work of one build.
struct Rig {
    crucible::test::RigLog log{};
    crucible::test::RigRing ring{};
    Work work;

    Rig() = default;
    Rig(const Rig&) = delete("the thread of a build holds Refs to the sides of the ring and of the log");
    Rig& operator=(const Rig&) = delete("the thread of a build holds Refs to the sides of the ring and of the log");

    // The region of the work, which a thread that runs no pipeline builds with
    // the numerics of a thread that never started.
    [[nodiscard]] BuiltRegion build() {
        return build_after_([](auto& /*stopped*/) noexcept {});
    }

    // The region of the work under the numerics of the start of a run.
    [[nodiscard]] BuiltRegion build_under(crucible::NumericsHash numerics) {
        return build_after_([numerics](auto& stopped) noexcept { stopped.set_numerics(numerics); });
    }

private:
    template <class Prepare>
    [[nodiscard]] BuiltRegion build_after_(Prepare prepare) {
        ::fixy::Option<BuiltRegion> region{};
        crucible::with_background_thread(
            ::foundation::effects::testing::test().alloc, ring.side(), log.side(), {},
            [this, &prepare, &region](BackgroundThread const& /*bt*/, auto stopped) noexcept {
                prepare(stopped);
                region = ::fixy::Option<BuiltRegion>::some(work.build(stopped, *log.view()));
            });
        return std::move(region).expect("the frame built the region");
    }
};

[[nodiscard]] crucible::SlotId input_slot(const BuiltRegion& region, uint32_t op, uint32_t input) {
    return *region.ops()
                .get(op)
                .expect("the region holds the op")
                ->input_slot_ids()
                .get(input)
                .expect("the op holds the input");
}

[[nodiscard]] crucible::SlotId output_slot(const BuiltRegion& region, uint32_t op, uint32_t output) {
    return *region.ops()
                .get(op)
                .expect("the region holds the op")
                ->output_slot_ids()
                .get(output)
                .expect("the op holds the output");
}

[[nodiscard]] crucible::OpIndex producer(const BuiltRegion& region, uint32_t op, uint32_t input) {
    return *region.ops()
                .get(op)
                .expect("the region holds the op")
                ->input_trace_indices()
                .get(input)
                .expect("the op holds the input");
}

[[nodiscard]] const crucible::TensorSlot& slot_of(const BuiltRegion& region, crucible::SlotId slot) {
    return *region.slots().get(slot.raw()).expect("the region holds the slot");
}

[[nodiscard]] BuiltRegion build_three_ops(uint32_t last_output) {
    Rig rig;
    rig.work.push_source(rig.log, 0, vector_at(kX));
    rig.work.push_unary(rig.log, 1, vector_at(kX), vector_at(kY));
    rig.work.push_unary(rig.log, 2, vector_at(kY), vector_at(last_output));
    return rig.build();
}

// True when each run of a region is empty: the region holds no parcel, as the
// default region does.
[[nodiscard]] bool has_no_run(const BuiltRegion& region) {
    return region.ops().size() == 0 && region.metas().size() == 0 && region.scalars().size() == 0
        && region.producers().size() == 0 && region.slot_ids().size() == 0 && region.slots().size() == 0;
}

// A move takes the parcel of a region, and a move assignment does the same,
// so each View of the source is empty after it.  The target
// holds the runs of the build, with the same hash.
void test_moved_from_region_has_no_run() {
    BuiltRegion region = build_three_ops(kZ);
    const size_t ops = region.ops().size();
    const size_t slots = region.slots().size();
    const crucible::ContentHash hash = region.content_hash();
    CRUCIBLE_TEST_ASSERT(kTest, ops == 3 && slots > 0);

    BuiltRegion moved{std::move(region)};
    CRUCIBLE_TEST_ASSERT(kTest, has_no_run(region));
    CRUCIBLE_TEST_ASSERT(kTest, moved.ops().size() == ops && moved.slots().size() == slots);
    CRUCIBLE_TEST_ASSERT(kTest, moved.content_hash() == hash);

    BuiltRegion assigned{};
    CRUCIBLE_TEST_ASSERT(kTest, has_no_run(assigned));
    assigned = std::move(moved);
    CRUCIBLE_TEST_ASSERT(kTest, has_no_run(moved));
    CRUCIBLE_TEST_ASSERT(kTest, assigned.ops().size() == ops && assigned.slots().size() == slots);
    CRUCIBLE_TEST_ASSERT(kTest, assigned.content_hash()
                                    == crucible::compute_content_hash(assigned.ops(), assigned.numerics()));
    ::fixy::check::pass(kTest, "  test_moved_from_region_has_no_run: PASSED\n");
}

void test_reused_address_keeps_the_hash() {
    const BuiltRegion fresh = build_three_ops(kZ);
    const BuiltRegion reused = build_three_ops(kX);
    CRUCIBLE_TEST_ASSERT(
        kTest, fresh.content_hash() == reused.content_hash()
                   && "the content hash does not depend on the address that the allocator gives a new tensor");
    CRUCIBLE_TEST_ASSERT(kTest, fresh.content_hash() == crucible::compute_content_hash(fresh.ops(), fresh.numerics()));
    CRUCIBLE_TEST_ASSERT(kTest,
                         reused.content_hash() == crucible::compute_content_hash(reused.ops(), reused.numerics()));
    ::fixy::check::pass(kTest, "  test_reused_address_keeps_the_hash: PASSED\n");
}

// The output of op 2 at the address of the dead output of op 0 is a new
// storage: it takes a slot of its own, and the slot of op 0 dies at op 1.
void test_reused_address_gets_a_new_slot() {
    const BuiltRegion reused = build_three_ops(kX);
    CRUCIBLE_TEST_ASSERT(kTest, reused.slots().size() == 3
                                    && "a new storage at the address of a dead one takes a slot of its own");
    const crucible::SlotId first = output_slot(reused, 0, 0);
    const crucible::SlotId last = output_slot(reused, 2, 0);
    CRUCIBLE_TEST_ASSERT(kTest, first != last);
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(reused, first).death_op == crucible::OpIndex{1});
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(reused, last).birth_op == crucible::OpIndex{2});
    CRUCIBLE_TEST_ASSERT(kTest, !slot_of(reused, first).is_external && !slot_of(reused, last).is_external);
    ::fixy::check::pass(kTest, "  test_reused_address_gets_a_new_slot: PASSED\n");
}

// Op 0 writes the base B of 64 floats.  Op 1 reads the view of B from element
// 16, op 2 returns the view of B from element 32, and op 3 reads that view.
// Each view has the slot of B and the producer of the last write of B.
void test_view_at_an_offset_reads_its_base() {
    Rig rig;
    rig.work.push_source(rig.log, 0, vector_at(kX));
    rig.work.push_unary(rig.log, 1, vector_at(kX, 16, 48), vector_at(kY));
    rig.work.push_unary(rig.log, 2, vector_at(kX), vector_at(kX, 32, 32));
    rig.work.push_unary(rig.log, 3, vector_at(kX, 32, 32), vector_at(kZ));
    const BuiltRegion region = rig.build();

    const crucible::SlotId base = output_slot(region, 0, 0);
    CRUCIBLE_TEST_ASSERT(kTest,
                         region.slots().size() == 3 && "the views of B take the slot of B, and no external slot");
    CRUCIBLE_TEST_ASSERT(kTest, input_slot(region, 1, 0) == base && producer(region, 1, 0) == crucible::OpIndex{0}
                                    && "a view at an offset reads the slot of its base");
    CRUCIBLE_TEST_ASSERT(kTest,
                         output_slot(region, 2, 0) == base && "a view that an op returns keeps the slot of its base");
    CRUCIBLE_TEST_ASSERT(kTest, input_slot(region, 3, 0) == base && producer(region, 3, 0) == crucible::OpIndex{2});
    CRUCIBLE_TEST_ASSERT(kTest, !slot_of(region, base).is_external);
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(region, base).nbytes == 64 * sizeof(float)
                                    && "the slot holds the span of each view of B");
    ::fixy::check::pass(kTest, "  test_view_at_an_offset_reads_its_base: PASSED\n");
}

// Op 1 of depth 0 returns the tensor R that op 0 of depth 1, in its nest,
// wrote.  The record is in post-order, so op 0 comes first.  R is one
// storage, so the two outputs have one slot.  Op 2 of depth 0 writes R again
// after op 1: op 1 is no op of the nest of op 2, so the output of op 2 is a
// new storage at a reused address.
void test_return_of_the_nest_keeps_the_slot() {
    Rig rig;
    rig.work.push_unary(rig.log, 0, vector_at(kX), vector_at(kY), crucible::OpDepth{1});
    rig.work.push_unary(rig.log, 1, vector_at(kX), vector_at(kY), crucible::OpDepth{0});
    rig.work.push_unary(rig.log, 2, vector_at(kZ), vector_at(kY), crucible::OpDepth{0});
    const BuiltRegion region = rig.build();

    CRUCIBLE_TEST_ASSERT(kTest, output_slot(region, 1, 0) == output_slot(region, 0, 0)
                                    && "the tensor of the nest keeps its slot");
    CRUCIBLE_TEST_ASSERT(kTest,
                         output_slot(region, 2, 0) != output_slot(region, 1, 0) && "a sibling writes a new storage");

    // The nest of linear in the record of an MLP: copy_ of depth 2 writes the
    // bias into the result R in place, addmm of depth 1 writes R, and linear
    // of depth 0 returns R.  The three outputs are one storage.
    Rig chain;
    const ::fixy::FixedArray<TensorMeta, 2> copy_inputs{vector_at(kY), vector_at(kZ)};
    const ::fixy::FixedArray<TensorMeta, 1> result{vector_at(kY)};
    chain.work.push_op(chain.log, 0, copy_inputs.view(), result.view(), crucible::OpDepth{2});
    chain.work.push_unary(chain.log, 1, vector_at(kX), vector_at(kY), crucible::OpDepth{1});
    chain.work.push_unary(chain.log, 2, vector_at(kX), vector_at(kY), crucible::OpDepth{0});
    const BuiltRegion nested = chain.build();
    const crucible::SlotId copied = output_slot(nested, 0, 0);
    CRUCIBLE_TEST_ASSERT(kTest, output_slot(nested, 1, 0) == copied && output_slot(nested, 2, 0) == copied);
    CRUCIBLE_TEST_ASSERT(kTest, input_slot(nested, 0, 0) == copied);
    ::fixy::check::pass(kTest, "  test_return_of_the_nest_keeps_the_slot: PASSED\n");
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
    mlp.work.push_op(mlp.log, 0, copy_inputs.view(), result.view(), crucible::OpDepth{2});
    mlp.work.push_op(mlp.log, 1, addmm_inputs.view(), result.view(), crucible::OpDepth{1});
    mlp.work.push_op(mlp.log, 2, linear_inputs.view(), result.view(), crucible::OpDepth{0});
    mlp.work.push_unary(mlp.log, 3, vector_at(kY), vector_at(kW));
    const BuiltRegion layer = mlp.build();

    const crucible::SlotId made = output_slot(layer, 2, 0);
    CRUCIBLE_TEST_ASSERT(kTest,
                         !slot_of(layer, made).is_external && "the result that addmm makes is a slot of the region");
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(layer, made).birth_op == crucible::OpIndex{0}
                                    && slot_of(layer, made).death_op == crucible::OpIndex{3});
    CRUCIBLE_TEST_ASSERT(kTest, input_slot(layer, 3, 0) == made && producer(layer, 3, 0) == crucible::OpIndex{2});
    CRUCIBLE_TEST_ASSERT(kTest,
                         slot_of(layer, input_slot(layer, 0, 1)).is_external && "the bias lives before the region");
    CRUCIBLE_TEST_ASSERT(kTest,
                         slot_of(layer, input_slot(layer, 1, 1)).is_external && "the input x lives before the region");

    Rig param;
    param.work.push_unary(param.log, 0, vector_at(kX), vector_at(kX), crucible::OpDepth{1});
    param.work.push_unary(param.log, 1, vector_at(kX), vector_at(kX), crucible::OpDepth{0});
    const BuiltRegion returned = param.build();
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(returned, output_slot(returned, 1, 0)).is_external
                                    && "a storage that the parent reads lives before it");
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(returned, output_slot(returned, 1, 0)).birth_op == crucible::OpIndex{0});
    ::fixy::check::pass(kTest, "  test_result_of_a_nest_is_internal: PASSED\n");
}

// The same ops under the default policy, under the strict tier, and under
// the default policy with the host that flushes denormal floats give three
// content hashes.  Each region keeps the numerics hash that its content hash
// folds.
void test_numerics_policy_moves_the_hash() {
    const BuiltRegion ordered = build_three_ops(kZ);
    CRUCIBLE_TEST_ASSERT(kTest,
                         ordered.numerics()
                             == crucible::compute_numerics_hash(crucible::NumericsPolicy{}, crucible::EagerNumerics{}));

    crucible::NumericsPolicy strict_policy{};
    strict_policy.determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
    const crucible::EagerNumerics flushing{.bits = crucible::EagerNumerics::FLUSH_DENORMAL};
    const ::fixy::FixedArray<crucible::NumericsHash, 2> others{
        crucible::compute_numerics_hash(strict_policy, crucible::EagerNumerics{}),
        crucible::compute_numerics_hash(crucible::NumericsPolicy{}, flushing)};
    for (const crucible::NumericsHash numerics : others) {
        Rig rig;
        rig.work.push_source(rig.log, 0, vector_at(kX));
        rig.work.push_unary(rig.log, 1, vector_at(kX), vector_at(kY));
        rig.work.push_unary(rig.log, 2, vector_at(kY), vector_at(kZ));
        const BuiltRegion region = rig.build_under(numerics);
        CRUCIBLE_TEST_ASSERT(kTest, region.numerics() == numerics);
        CRUCIBLE_TEST_ASSERT(kTest, region.content_hash() != ordered.content_hash()
                                        && "the numerics of the run separate two regions");
        CRUCIBLE_TEST_ASSERT(kTest, region.content_hash() == crucible::compute_content_hash(region.ops(), numerics));
    }
    CRUCIBLE_TEST_ASSERT(kTest,
                         *others.view().get(0).expect("two hashes") != *others.view().get(1).expect("two hashes"));
    ::fixy::check::pass(kTest, "  test_numerics_policy_moves_the_hash: PASSED\n");
}

// Two ops read the external storage X, the first one at the first element
// and the second one at element 8.  The two reads have one external slot and
// no producer.
void test_reads_of_an_external_take_one_slot() {
    Rig rig;
    rig.work.push_unary(rig.log, 0, vector_at(kX), vector_at(kY));
    rig.work.push_unary(rig.log, 1, vector_at(kX, 8, 56), vector_at(kZ));
    const BuiltRegion region = rig.build();

    const crucible::SlotId external = input_slot(region, 0, 0);
    CRUCIBLE_TEST_ASSERT(kTest,
                         input_slot(region, 1, 0) == external && "each read of one external storage reads one slot");
    CRUCIBLE_TEST_ASSERT(kTest, !producer(region, 1, 0).is_valid());
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(region, external).is_external
                                    && slot_of(region, external).death_op == crucible::OpIndex{1});
    CRUCIBLE_TEST_ASSERT(kTest, region.slots().size() == 3);
    ::fixy::check::pass(kTest, "  test_reads_of_an_external_take_one_slot: PASSED\n");
}

// Op 0 writes X with the generation 1, and op 1 reads it, so X dies after op
// 1.  An op that the recorder does not see makes a new storage at X with the
// generation 3, and op 2 reads it.  The new storage lives before the region,
// so its read takes an external slot of its own, with no producer.  With no
// generation, the key of the address alone cannot tell the two storages
// apart, and the read of op 2 reads the slot that op 0 wrote.
void test_new_storage_at_a_dead_address_is_a_new_input() {
    Rig known;
    known.work.push_source(known.log, 0, vector_at(kX, 0, 64, kKnownFirst));
    known.work.push_unary(known.log, 1, vector_at(kX, 0, 64, kKnownFirst), vector_at(kY, 0, 64, kKnownSecond));
    known.work.push_unary(known.log, 2, vector_at(kX, 0, 64, kKnownThird), vector_at(kZ, 0, 64, kKnownFourth));
    const BuiltRegion fresh = known.build();
    const crucible::SlotId written = output_slot(fresh, 0, 0);
    const crucible::SlotId read = input_slot(fresh, 2, 0);
    CRUCIBLE_TEST_ASSERT(kTest, read != written && "a new storage at the address of a dead one is a new input");
    CRUCIBLE_TEST_ASSERT(kTest, !producer(fresh, 2, 0).is_valid() && "no op of the region wrote the new storage");
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(fresh, read).is_external);
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(fresh, written).death_op == crucible::OpIndex{1}
                                    && "the dead storage dies at its last read");
    CRUCIBLE_TEST_ASSERT(kTest, fresh.slots().size() == 4);

    Rig unknown;
    unknown.work.push_source(unknown.log, 0, vector_at(kX));
    unknown.work.push_unary(unknown.log, 1, vector_at(kX), vector_at(kY));
    unknown.work.push_unary(unknown.log, 2, vector_at(kX), vector_at(kZ));
    const BuiltRegion merged = unknown.build();
    CRUCIBLE_TEST_ASSERT(kTest, input_slot(merged, 2, 0) == output_slot(merged, 0, 0)
                                    && producer(merged, 2, 0) == crucible::OpIndex{0}
                                    && "with no generation, a read at the address reads the slot of the address");
    ::fixy::check::pass(kTest, "  test_new_storage_at_a_dead_address_is_a_new_input: PASSED\n");
}

// Op 0 writes X with the generation 1.  Op 1 reads Y and writes X again with
// the same generation: the same storage, which lives across op 1, as a buffer
// that an op fills in place.  The key with the generation names one storage,
// so the two outputs have one slot.  With no generation, op 1 shows no read
// of X, no view and no write in its nest, so its output at X takes a slot of
// its own.
void test_output_with_the_same_generation_keeps_the_slot() {
    Rig known;
    known.work.push_source(known.log, 0, vector_at(kX, 0, 64, kKnownFirst));
    known.work.push_unary(known.log, 1, vector_at(kY, 0, 64, kKnownSecond), vector_at(kX, 0, 64, kKnownFirst));
    const BuiltRegion same = known.build();
    CRUCIBLE_TEST_ASSERT(kTest, output_slot(same, 1, 0) == output_slot(same, 0, 0) && "one generation is one storage");
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(same, output_slot(same, 0, 0)).death_op == crucible::OpIndex{1});

    Rig unknown;
    unknown.work.push_source(unknown.log, 0, vector_at(kX));
    unknown.work.push_unary(unknown.log, 1, vector_at(kY), vector_at(kX));
    const BuiltRegion split = unknown.build();
    CRUCIBLE_TEST_ASSERT(kTest, output_slot(split, 1, 0) != output_slot(split, 0, 0)
                                    && "with no generation, an output that the op does not read is a new storage");
    ::fixy::check::pass(kTest, "  test_output_with_the_same_generation_keeps_the_slot: PASSED\n");
}

// Op 0 adds into the storage U at X in place, in the first period of the
// allocator, and op 1 reads U in that period.  While the front end records,
// its allocator gives each new storage a known generation, so the key of X in
// one period names U, and op 1 reads the slot that op 0 wrote.  A replay then
// starts the second period, frees U, and makes a new storage at X with an
// allocator that gives no generation.  Op 2 reads it in the second period.
// Its key holds the second period, so its read takes an external slot of its
// own, with no producer.  With no period in the key, the read of op 2 reads
// the slot that op 0 wrote.
void test_unknown_storage_of_another_period_is_a_new_input() {
    Rig periods;
    periods.work.push_unary(periods.log, 0, vector_at(kX, 0, 64, kUnknownInFirstPeriod),
                            vector_at(kX, 0, 64, kUnknownInFirstPeriod));
    periods.work.push_unary(periods.log, 1, vector_at(kX, 0, 64, kUnknownInFirstPeriod),
                            vector_at(kY, 0, 64, kKnownFirst));
    periods.work.push_unary(periods.log, 2, vector_at(kX, 0, 64, kUnknownInSecondPeriod),
                            vector_at(kZ, 0, 64, kKnownSecond));
    const BuiltRegion region = periods.build();

    const crucible::SlotId first = input_slot(region, 0, 0);
    CRUCIBLE_TEST_ASSERT(kTest,
                         output_slot(region, 0, 0) == first && "an in-place op writes the storage that it reads");
    CRUCIBLE_TEST_ASSERT(kTest, input_slot(region, 1, 0) == first && producer(region, 1, 0) == crucible::OpIndex{0}
                                    && "one period keeps one storage at one address");
    const crucible::SlotId later = input_slot(region, 2, 0);
    CRUCIBLE_TEST_ASSERT(kTest, later != first && "a storage of another period at the address is a new input");
    CRUCIBLE_TEST_ASSERT(kTest, !producer(region, 2, 0).is_valid() && slot_of(region, later).is_external);
    CRUCIBLE_TEST_ASSERT(kTest, slot_of(region, first).death_op == crucible::OpIndex{1}
                                    && "the storage of the first period dies at op 1");
    CRUCIBLE_TEST_ASSERT(kTest, region.slots().size() == 4);
    ::fixy::check::pass(kTest, "  test_unknown_storage_of_another_period_is_a_new_input: PASSED\n");
}

}  // namespace

int main() {
    // The kernel classification of a build reads the global tables, which
    // start() seals.  These tests build without start(), so they seal here.
    crucible::global_schema_table().seal(kTest);
    crucible::global_ckernel_table().value()->seal(kTest);
    test_moved_from_region_has_no_run();
    test_reused_address_keeps_the_hash();
    test_reused_address_gets_a_new_slot();
    test_view_at_an_offset_reads_its_base();
    test_return_of_the_nest_keeps_the_slot();
    test_result_of_a_nest_is_internal();
    test_reads_of_an_external_take_one_slot();
    test_numerics_policy_moves_the_hash();
    test_new_storage_at_a_dead_address_is_a_new_input();
    test_output_with_the_same_generation_keeps_the_slot();
    test_unknown_storage_of_another_period_is_a_new_input();
    ::fixy::check::pass(kTest, "test_build_address_reuse: ALL PASSED\n");
    return 0;
}
