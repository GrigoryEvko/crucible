// The op packets of test_mlp_trace: the fake addresses of each tensor, and
// the packet that a frontend builds for each op of the table.

#include "mlp_trace.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <bit>
#include <cstdint>

using namespace crucible;

namespace test_mlp_trace {

namespace {

TensorMeta make_2d(void* ptr, int64_t r, int64_t c) {
    TensorMeta m{};
    m.ndim = 2;
    m.sizes[0] = ::crucible::tensor_dim(r);
    m.sizes[1] = ::crucible::tensor_dim(c);
    m.strides[0] = ::crucible::tensor_dim(c);
    m.strides[1] = ::crucible::tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.data_ptr = external_data_ptr(ptr);
    return m;
}

TensorMeta make_1d(void* ptr, int64_t n) {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = ::crucible::tensor_dim(n);
    m.strides[0] = ::crucible::tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.data_ptr = external_data_ptr(ptr);
    return m;
}

// The recorder tracks data flow by matching addresses, so the addresses
// have to behave the way a real allocator's would.  A parameter keeps
// one address for the whole run, because it outlives every iteration.
// An activation gets a fresh address each iteration, because a new one
// is produced each time.
void* const W1_PTR = std::bit_cast<void*>(static_cast<std::uintptr_t>(0x10000));
void* const B1_PTR = std::bit_cast<void*>(static_cast<std::uintptr_t>(0x20000));
void* const W2_PTR = std::bit_cast<void*>(static_cast<std::uintptr_t>(0x30000));
void* const B2_PTR = std::bit_cast<void*>(static_cast<std::uintptr_t>(0x40000));
void* const X_PTR = std::bit_cast<void*>(static_cast<std::uintptr_t>(0x50000));

void* act_ptr(uint32_t iter, uint32_t tensor_id) {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>((iter + 1) * 0x1000000 + (tensor_id + 1) * 0x10000));
}

enum Act : uint32_t {
    ACT_HIDDEN = 0,  // mm output          [4,4]
    ACT_BIASED = 1,  // add output         [4,4]
    ACT_ACTIVATED = 2,  // relu output        [4,4]
    ACT_LOGITS = 3,  // mm output          [4,2]
    ACT_OUTPUT = 4,  // add output         [4,2]
    ACT_GRAD_OUT = 5,  // loss_bwd output    [4,2]
    ACT_GRAD_ACT = 6,  // mm_bwd output      [4,4]
    ACT_GRAD_BIA = 7,  // relu_bwd output    [4,4]
    ACT_GRAD_W1 = 8,  // mm_bwd output      [8,4]
    ACT_W1_UPDATED = 9,  // accum_grad output  [8,4]
};

}  // namespace

OpPacket build_op(uint32_t iter, uint32_t op_idx) {
    OpPacket p;
    const auto& op = MLP_OPS[op_idx];

    p.entry.schema_hash = op.schema_hash;
    p.entry.shape_hash = op.shape_hash;
    p.entry.num_inputs = op.num_inputs;
    p.entry.num_outputs = op.num_outputs;

    uint16_t m = 0;

    switch (op_idx) {
        case 0:  // mm(input, W1) → hidden
            p.metas[m++] = make_2d(X_PTR, BATCH, IN_DIM);  // input (external)
            p.metas[m++] = make_2d(W1_PTR, IN_DIM, HIDDEN);  // weight1 (external)
            p.metas[m++] = make_2d(act_ptr(iter, ACT_HIDDEN), BATCH, HIDDEN);
            break;
        case 1:  // add(hidden, B1) → biased
            p.metas[m++] = make_2d(act_ptr(iter, ACT_HIDDEN), BATCH, HIDDEN);
            p.metas[m++] = make_1d(B1_PTR, HIDDEN);  // bias1 (external)
            p.metas[m++] = make_2d(act_ptr(iter, ACT_BIASED), BATCH, HIDDEN);
            break;
        case 2:  // relu(biased) → activated
            p.metas[m++] = make_2d(act_ptr(iter, ACT_BIASED), BATCH, HIDDEN);
            p.metas[m++] = make_2d(act_ptr(iter, ACT_ACTIVATED), BATCH, HIDDEN);
            break;
        case 3:  // mm(activated, W2) → logits
            p.metas[m++] = make_2d(act_ptr(iter, ACT_ACTIVATED), BATCH, HIDDEN);
            p.metas[m++] = make_2d(W2_PTR, HIDDEN, OUT_DIM);  // weight2 (external)
            p.metas[m++] = make_2d(act_ptr(iter, ACT_LOGITS), BATCH, OUT_DIM);
            break;
        case 4:  // add(logits, B2) → output
            p.metas[m++] = make_2d(act_ptr(iter, ACT_LOGITS), BATCH, OUT_DIM);
            p.metas[m++] = make_1d(B2_PTR, OUT_DIM);  // bias2 (external)
            p.metas[m++] = make_2d(act_ptr(iter, ACT_OUTPUT), BATCH, OUT_DIM);
            break;
        case 5:  // loss_bwd(output) → grad_out
            p.metas[m++] = make_2d(act_ptr(iter, ACT_OUTPUT), BATCH, OUT_DIM);
            p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_OUT), BATCH, OUT_DIM);
            break;
        case 6:  // mm(grad_out, W2.T) → grad_act
            p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_OUT), BATCH, OUT_DIM);
            p.metas[m++] = make_2d(W2_PTR, HIDDEN, OUT_DIM);  // weight2 (external, transposed)
            p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_ACT), BATCH, HIDDEN);
            break;
        case 7:  // relu_bwd(grad_act, biased) → grad_biased
            p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_ACT), BATCH, HIDDEN);
            p.metas[m++] = make_2d(act_ptr(iter, ACT_BIASED), BATCH, HIDDEN);  // saved for backward
            p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_BIA), BATCH, HIDDEN);
            break;
        case 8:  // mm(input.T, grad_biased) → grad_weight1
            p.metas[m++] = make_2d(X_PTR, BATCH, IN_DIM);  // input (external)
            p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_BIA), BATCH, HIDDEN);
            p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_W1), IN_DIM, HIDDEN);
            break;
        case 9:  // accum_grad(W1, grad_weight1) → W1_updated
            p.metas[m++] = make_2d(W1_PTR, IN_DIM, HIDDEN);  // weight1 (external)
            p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_W1), IN_DIM, HIDDEN);
            p.metas[m++] = make_2d(act_ptr(iter, ACT_W1_UPDATED), IN_DIM, HIDDEN);
            break;
        default:
            break;
    }

    p.n_metas = m;
    return p;
}

void feed_iteration(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto pkt = build_op(iter, i);
        bool ok = vigil.record_op(crucible::test::certify_synthetic_entry(pkt.entry), pkt.metas, pkt.n_metas);
        assert(ok && "record_op failed");
    }
}

void feed_trigger(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto pkt = build_op(iter, i);
        bool ok = vigil.record_op(crucible::test::certify_synthetic_entry(pkt.entry), pkt.metas, pkt.n_metas);
        assert(ok);
    }
}

}  // namespace test_mlp_trace
