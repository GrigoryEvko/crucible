// The tensor metadata of a published region, after the foreground writes new
// records over the run of the metadata log that the region was built from.
//
// The publish stage releases that run when it publishes the region.  The
// region lives as long as the arena.  These readers read the metadata of its
// ops after the release:
//   - The activation, which registers the data pointer of each external slot
//   - The region cache, which activates the region again after a divergence
//   - The store, which serializes the region.
// Each one must read the records that the foreground recorded, and none of
// the records that it wrote later.

#include "vigil_rig.h"

#include <crucible/MetaLog.h>
#include <crucible/Serialize.h>
#include <crucible/Vigil.h>
#include <foundation/effects/Effect.h>
#include "test_harness.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include "tensor_data.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace test_vigil {

namespace {

using divrig::K;
using divrig::NUM_OPS;

// Op 0 of each iteration reads this parameter.  No op of the region writes
// it before that read, and the region gives it an external slot.
void* parameter_ptr() noexcept { return std::bit_cast<void*>(std::uintptr_t{0x7E0000000000}); }

void* activation_ptr(uint32_t iter, uint32_t op) noexcept {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

// The data pointer of each record that the foreground writes over the log.
void* overwrite_ptr() noexcept { return std::bit_cast<void*>(std::uintptr_t{0xBAD000}); }

crucible::TensorMeta meta_at(void* data_ptr, int64_t size) {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = ::crucible::tensor_dim(size);
    meta.strides[0] = ::crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.layout = crucible::Layout::Strided;
    meta.data_ptr = crucible::external_data_ptr(data_ptr);
    return meta;
}

// Op op_idx of iteration iter, with one input and one output.  Op 0 reads
// the parameter, and each other op reads the output of the op before it.
divrig::OpData op_with_parameter(uint32_t iter, uint32_t op_idx) {
    divrig::OpData made;
    made.entry.schema_hash = divrig::SCHEMA[op_idx];
    made.entry.shape_hash = divrig::SHAPE[op_idx];
    made.entry.num_inputs = 1;
    made.entry.num_outputs = 1;
    const crucible::TensorMeta input =
        meta_at((op_idx == 0) ? parameter_ptr() : activation_ptr(iter, op_idx - 1), 1024);
    made.metas.set(::fixy::index<0>(), input);
    const crucible::TensorMeta output = meta_at(activation_ptr(iter, op_idx), 1024);
    made.metas.set(::fixy::index<1>(), output);
    made.n_metas = 2;
    return made;
}

void record(crucible::Vigil& vigil, uint32_t iter, uint32_t op_idx) {
    const divrig::OpData recorded = op_with_parameter(iter, op_idx);
    const bool was_recorded =
        vigil.record_op(crucible::test::certify_synthetic_op(recorded.entry, recorded.metas, recorded.n_metas));
    CRUCIBLE_TEST_ASSERT(kTest, was_recorded);
}

// Records ops until the log took CAPACITY records more than it held at the
// call, so the foreground wrote over each slot of the buffer, the slots of
// the region included.  The log takes a record only into a slot that the
// publish stage released, so a flush after each batch of ops lets the stage
// release the records of the ops before it.  The ops have schema hashes that
// never recur, so no iteration claims them.  A loop of more ops than the
// bound fails the test and does not hang it.
void write_over_each_slot(crucible::Vigil& vigil) {
    constexpr uint16_t kRecords = 16;
    constexpr uint64_t kBatch = 4096;
    constexpr uint64_t kMaxOps = uint64_t{4} * crucible::MetaLog::CAPACITY / kRecords;
    const ::fixy::HeapArray<crucible::TensorMeta> chunk =
        ::fixy::mint_heap_array(::foundation::effects::testing::test().alloc, kRecords, meta_at(overwrite_ptr(), 3));
    const uint64_t first = vigil.meta_log_total_produced().raw();
    uint64_t op = 0;
    while (vigil.meta_log_total_produced().raw() - first < crucible::MetaLog::CAPACITY) {
        CRUCIBLE_TEST_ASSERT(kTest, op < kMaxOps && "the log took too few records over the ops");
        crucible::TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{0xF000000 + op};
        entry.shape_hash = crucible::ShapeHash{0xF800000 + op};
        entry.num_outputs = kRecords;
        const bool was_recorded = vigil.record_op(crucible::test::certify_synthetic_op(entry, chunk.view()));
        if (was_recorded) ++op;
        if (!was_recorded || op % kBatch == 0) CRUCIBLE_TEST_ASSERT(kTest, vigil.flush(kTest).is_ok());
    }
}

}  // namespace

void test_region_keeps_its_metadata_after_the_log_wraps() {
    using crucible::DispatchResult;

    crucible::Vigil vigil;
    const auto fg = vigil.mint_producer_context();

    // Two whole iterations and the head of a third: the shortest input that
    // makes the detector publish a region.
    for (uint32_t iter = 0; iter < 2; ++iter) {
        for (uint32_t op = 0; op < NUM_OPS; ++op)
            record(vigil, iter, op);
    }
    for (uint32_t op = 0; op < K; ++op)
        record(vigil, 2, op);
    crucible::test::flush_and_wait_region_published(kTest, vigil);

    const crucible::DagRef<const crucible::RegionNode> region =
        vigil.active_region().expect("the flush published a region");
    CRUCIBLE_TEST_ASSERT(kTest, region->num_ops() == NUM_OPS);

    // What the foreground recorded, read before it writes over the log.
    crucible::TensorMeta recorded[NUM_OPS][2]{};
    for (uint32_t op = 0; op < NUM_OPS; ++op) {
        std::memcpy(
            &recorded[op][0],
            &*region->op_at(op).input_metas().get(0).expect("a test reads an element at or past the count of its View"),
            sizeof(crucible::TensorMeta));
        std::memcpy(&recorded[op][1],
                    &*region->op_at(op).output_metas().get(0).expect(
                        "a test reads an element at or past the count of its View"),
                    sizeof(crucible::TensorMeta));
    }
    CRUCIBLE_TEST_ASSERT(kTest, recorded[0][0].data_ptr.value() == parameter_ptr());
    auto [bytes_before, serialized_before] = ::fixy::with_mut(
        ::fixy::mint_heap_array(::foundation::effects::testing::test().alloc, size_t{1} << 16, std::byte{0}),
        [&region](auto run) noexcept { return crucible::serialize_region(::fixy::to(*region), std::move(run)); });
    CRUCIBLE_TEST_ASSERT(kTest, bytes_before > 0);

    write_over_each_slot(vigil);

    for (uint32_t op = 0; op < NUM_OPS; ++op) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             std::memcmp(&*region->op_at(op).input_metas().get(0).expect(
                                             "a test reads an element at or past the count of its View"),
                                         &recorded[op][0], sizeof(crucible::TensorMeta))
                                     == 0
                                 && "the region reads an input record that the foreground wrote after the release");
        CRUCIBLE_TEST_ASSERT(kTest,
                             std::memcmp(&*region->op_at(op).output_metas().get(0).expect(
                                             "a test reads an element at or past the count of its View"),
                                         &recorded[op][1], sizeof(crucible::TensorMeta))
                                     == 0
                                 && "the region reads an output record that the foreground wrote after the release");
    }

    // The store writes the same bytes as before the overwrite, and a load of
    // them folds to the content hash of the region.
    auto [bytes_after, serialized_after] = ::fixy::with_mut(
        ::fixy::mint_heap_array(::foundation::effects::testing::test().alloc, size_t{1} << 16, std::byte{0}),
        [&region](auto run) noexcept { return crucible::serialize_region(::fixy::to(*region), std::move(run)); });
    CRUCIBLE_TEST_ASSERT(kTest, bytes_after == bytes_before);
    const ::fixy::View<const std::byte> image_after =
        serialized_after.view().window(0, bytes_after).expect("the writer writes inside its image");
    CRUCIBLE_TEST_ASSERT(
        kTest,
        ::fixy::equal(image_after,
                      serialized_before.view().window(0, bytes_before).expect("the writer writes inside its image")));
    ::fixy::BumpArena<crucible::DagArena> load_arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 16});
    const auto loaded =
        crucible::deserialize_region(::foundation::effects::testing::test().alloc, image_after, load_arena);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.is_ok());

    // The activation registers the data pointer of the parameter, which it
    // reads from the input record of op 0.
    for (uint32_t op = 0; op < K; ++op) {
        const divrig::OpData aligned = op_with_parameter(3, op);
        const auto result = crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, result.action == DispatchResult::Action::RECORD && "alignment ops return RECORD");
    }
    CRUCIBLE_TEST_ASSERT(kTest, vigil.context().is_compiled());
    for (uint32_t op = K; op < NUM_OPS; ++op) {
        const divrig::OpData replayed = op_with_parameter(3, op);
        const auto result = crucible::test::dispatch_synthetic(vigil, replayed.entry, replayed.metas, replayed.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, result.action == DispatchResult::Action::COMPILED);
    }
    const divrig::OpData next_head = op_with_parameter(4, 0);
    const auto head_result =
        crucible::test::dispatch_synthetic(vigil, next_head.entry, next_head.metas, next_head.n_metas);
    CRUCIBLE_TEST_ASSERT(kTest, head_result.action == DispatchResult::Action::COMPILED);
    CRUCIBLE_TEST_ASSERT(
        kTest,
        crucible::test::host_ptr(vigil.input_ptr(fg, crucible::InputPosition{0})) == parameter_ptr()
            && "the activation registered the data pointer of a record that the foreground wrote after the release");

    ::fixy::check::pass(kTest, "  test_region_keeps_its_metadata_after_the_log_wraps: PASSED\n");
}

}  // namespace test_vigil
