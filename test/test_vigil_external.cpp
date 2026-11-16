// The data address of an external slot follows the live op of each iteration.
//
// Op 0 of each iteration reads a tensor that no op of the region writes, so
// the region gives it an external slot.  The tensor is a new batch of the
// data loader in each iteration, at a new address.  The activation binds the
// slot to the address of the recording.  Each compiled dispatch of a later
// iteration then binds it to the address of the live op, so input_ptr of op 0
// gives the address of the batch of that iteration.

#include "vigil_rig.h"

#include <crucible/Vigil.h>
#include "tensor_data.h"
#include "test_assert.h"
#include "test_harness.h"

#include <cstddef>
#include <cstdint>

namespace test_vigil {

namespace {

using divrig::K;
using divrig::NUM_OPS;

// The memory that the data addresses of the tensors point into.  The Vigil
// reads an address as a key, and gives it back through input_ptr, and never
// reads through it.
::fixy::FixedArray<std::byte, 16384> g_memory{};

// The offset of the batch of iteration iter, at an address of its own.
[[nodiscard]] uint32_t batch_offset(uint32_t iter) noexcept { return iter * 64; }

// The offset of the output of op op of iteration iter.
[[nodiscard]] uint32_t activation_offset(uint32_t iter, uint32_t op) noexcept { return 4096 + (iter * 16 + op) * 64; }

[[nodiscard]] crucible::TensorMeta meta_at(uint32_t offset) {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = ::crucible::tensor_dim(16);
    meta.strides[0] = ::crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.layout = crucible::Layout::Strided;
    meta.data_ptr = crucible::external_data_ptr(&*g_memory.view().get(offset).expect("the offset is in the memory"));
    return meta;
}

// Op op_idx of iteration iter.  Op 0 reads the batch of the iteration, and
// each other op reads the output of the op before it.
divrig::OpData op_reading_batch(uint32_t iter, uint32_t op_idx) {
    divrig::OpData made;
    made.entry.schema_hash = divrig::SCHEMA[op_idx];
    made.entry.shape_hash = divrig::SHAPE[op_idx];
    made.entry.num_inputs = 1;
    made.entry.num_outputs = 1;
    made.metas.at(::fixy::index<0>()) = meta_at(op_idx == 0 ? batch_offset(iter) : activation_offset(iter, op_idx - 1));
    made.metas.at(::fixy::index<1>()) = meta_at(activation_offset(iter, op_idx));
    made.n_metas = 2;
    return made;
}

crucible::DispatchResult dispatch(crucible::Vigil& vigil, uint32_t iter, uint32_t op_idx) {
    const divrig::OpData made = op_reading_batch(iter, op_idx);
    return crucible::test::dispatch_synthetic(vigil, made.entry, made.metas, made.n_metas);
}

}  // namespace

void test_external_follows_the_live_op() {
    using crucible::DispatchResult;
    crucible::Vigil vigil;
    const auto fg = vigil.mint_producer_context();

    // Two whole iterations and the head of a third publish a region.
    for (uint32_t iter = 0; iter < 2; ++iter) {
        for (uint32_t op = 0; op < NUM_OPS; ++op)
            assert(dispatch(vigil, iter, op).action == DispatchResult::Action::RECORD);
    }
    for (uint32_t op = 0; op < K; ++op)
        assert(dispatch(vigil, 2, op).action == DispatchResult::Action::RECORD);
    crucible::test::flush_and_wait_region_published(vigil);

    // Iteration 3 aligns on its first K ops, and then replays.
    for (uint32_t op = 0; op < K; ++op)
        assert(dispatch(vigil, 3, op).action == DispatchResult::Action::RECORD);
    assert(vigil.context().is_compiled());
    for (uint32_t op = K; op < NUM_OPS; ++op)
        assert(dispatch(vigil, 3, op).action == DispatchResult::Action::COMPILED);

    // Each later iteration reads a new batch, and the slot follows it.
    for (uint32_t iter = 4; iter < 7; ++iter) {
        assert(dispatch(vigil, iter, 0).action == DispatchResult::Action::COMPILED);
        assert(crucible::test::host_ptr(vigil.input_ptr(fg, 0))
                   == &*g_memory.view().get(batch_offset(iter)).expect("the offset is in the memory")
               && "the external slot holds the address of the batch of the live op");
        for (uint32_t op = 1; op < NUM_OPS; ++op)
            assert(dispatch(vigil, iter, op).action == DispatchResult::Action::COMPILED);
    }

    crucible::test::pass("  test_external_follows_the_live_op: PASSED\n");
}

}  // namespace test_vigil
