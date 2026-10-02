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

#include <crucible/Arena.h>
#include <crucible/MetaLog.h>
#include <crucible/Serialize.h>
#include <crucible/Vigil.h>
#include <foundation/effects/Effect.h>
#include "test_harness.h"
#include "test_assert.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

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
    made.metas[0] = meta_at((op_idx == 0) ? parameter_ptr() : activation_ptr(iter, op_idx - 1), 1024);
    made.metas[1] = meta_at(activation_ptr(iter, op_idx), 1024);
    made.n_metas = 2;
    return made;
}

void record(crucible::Vigil& vigil, uint32_t iter, uint32_t op_idx) {
    const divrig::OpData recorded = op_with_parameter(iter, op_idx);
    const bool was_recorded =
        vigil.record_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas, recorded.n_metas);
    assert(was_recorded);
}

// Appends records until the log is full.  The appends start at the head and
// pass the end of the buffer.  They write over each slot that the log
// released, and over no other slot.
void fill_released_slots(crucible::MetaLog& log) {
    constexpr uint32_t CHUNK = 4096;
    const std::vector<crucible::TensorMeta> chunk(CHUNK, meta_at(overwrite_ptr(), 3));
    for (uint32_t count = CHUNK; count > 0; count /= 2) {
        while (log.try_append(chunk.data(), count).is_valid()) {
        }
    }
    assert(log.size().peek() == crucible::MetaLog::CAPACITY);
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
    crucible::test::flush_and_wait_region_published(vigil);

    const crucible::RegionNode* const region = vigil.active_region();
    assert(region != nullptr && region->num_ops == NUM_OPS);

    // What the foreground recorded, read before it writes over the log.
    crucible::TensorMeta recorded[NUM_OPS][2]{};
    for (uint32_t op = 0; op < NUM_OPS; ++op) {
        std::memcpy(&recorded[op][0], region->ops[op].input_metas, sizeof(crucible::TensorMeta));
        std::memcpy(&recorded[op][1], region->ops[op].output_metas, sizeof(crucible::TensorMeta));
    }
    assert(crucible::raw_data_ptr(recorded[0][0]) == parameter_ptr());
    std::vector<uint8_t> serialized_before(std::size_t{1} << 16);
    const std::size_t bytes_before =
        crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{serialized_before});
    assert(bytes_before > 0);

    fill_released_slots(vigil.meta_log(fg));

    for (uint32_t op = 0; op < NUM_OPS; ++op) {
        assert(std::memcmp(region->ops[op].input_metas, &recorded[op][0], sizeof(crucible::TensorMeta)) == 0
               && "the region reads an input record that the foreground wrote after the release");
        assert(std::memcmp(region->ops[op].output_metas, &recorded[op][1], sizeof(crucible::TensorMeta)) == 0
               && "the region reads an output record that the foreground wrote after the release");
    }

    // The store writes the same bytes as before the overwrite, and a load of
    // them folds to the content hash of the region.
    std::vector<uint8_t> serialized_after(std::size_t{1} << 16);
    const std::size_t bytes_after =
        crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{serialized_after});
    assert(bytes_after == bytes_before);
    assert(std::memcmp(serialized_after.data(), serialized_before.data(), bytes_before) == 0);
    crucible::Arena load_arena{std::size_t{1} << 16};
    const auto loaded = crucible::deserialize_region(
        ::foundation::effects::testing::test().alloc,
        std::span<const uint8_t>{serialized_after.data(), bytes_after}, load_arena);
    assert(loaded.has_value());

    // The activation registers the data pointer of the parameter, which it
    // reads from the input record of op 0.
    for (uint32_t op = 0; op < K; ++op) {
        const divrig::OpData aligned = op_with_parameter(3, op);
        const auto result = crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
        assert(result.action == DispatchResult::Action::RECORD && "alignment ops return RECORD");
    }
    assert(vigil.context().is_compiled());
    for (uint32_t op = K; op < NUM_OPS; ++op) {
        const divrig::OpData replayed = op_with_parameter(3, op);
        const auto result =
            crucible::test::dispatch_synthetic(vigil, replayed.entry, replayed.metas, replayed.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);
    }
    const divrig::OpData next_head = op_with_parameter(4, 0);
    const auto head_result =
        crucible::test::dispatch_synthetic(vigil, next_head.entry, next_head.metas, next_head.n_metas);
    assert(head_result.action == DispatchResult::Action::COMPILED);
    assert(vigil.input_ptr(fg, 0) == parameter_ptr()
           && "the activation registered the data pointer of a record that the foreground wrote after the release");

    crucible::test::pass("  test_region_keeps_its_metadata_after_the_log_wraps: PASSED\n");
}

}  // namespace test_vigil
