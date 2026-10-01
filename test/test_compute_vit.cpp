// One transformer block plus a classification head, computed through the
// pool allocator and checked against a direct CPU reference:
//
//   X[B,S,D] → LayerNorm → Q,K,V projections → SDPA → output projection
//   → residual add → LayerNorm → FFN(mm+relu+mm) → residual add
//   → CLS token extract → linear head → softmax → Y[B,N_CLS]
//
// The op sequence is picked to stress sweep-line offset assignment rather
// than the arithmetic: three-input ops, residual edges whose producer and
// consumer are not adjacent, two activation widths, and a gather. Q, K and
// V are all alive at once during SDPA, and the slots freed after attention
// have to be reused for the FFN activations.
//
// The test is several source files of one executable, so that no
// translation unit holds the whole run:
//
//   compute_vit.h                 the shared part
//   this file                     the plan, the replay loop, the checks
//                                 and main
//   ..._reference.cpp             the weights and the CPU reference pass
//   ..._attention.cpp             ops 0 to 6 of one compiled iteration
//   ..._head.cpp                  ops 7 to 14 of one compiled iteration

#include "compute_vit.h"

#include <crucible/BackgroundThread.h>
#include <crucible/CrucibleContext.h>
#include <foundation/effects/Effect.h>

#include "test_assert.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace crucible;
using namespace test_compute_vit;

// The views of the replay chain are minted on the thread that holds a
// Vigil's producer claim.  This test drives the context without a Vigil,
// so it takes that context from the test door.
static constexpr VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<Vigil>();

int main() {
    auto test = ::foundation::effects::testing::test();
    std::printf("test_compute_vit:\n");

    Weights weights;
    fill_weights(weights);

    TensorSlot slots[N_SLOTS]{};

    auto make_ext = [](uint32_t id, uint64_t sz) -> TensorSlot {
        return {.offset_bytes = 0,
                .nbytes = sz,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{N_OPS},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = true,
                .pad = {},
                .slot_id = SlotId{id},
                .pad2 = {}};
    };
    auto make_int = [](uint32_t id, uint64_t sz, uint32_t birth, uint32_t death) -> TensorSlot {
        return {.offset_bytes = 0,
                .nbytes = sz,
                .birth_op = OpIndex{birth},
                .death_op = OpIndex{death},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{id},
                .pad2 = {}};
    };

    slots[SL_X] = make_ext(SL_X, SZ_BSD);
    slots[SL_G1] = make_ext(SL_G1, SZ_D);
    slots[SL_B1] = make_ext(SL_B1, SZ_D);
    slots[SL_WQ] = make_ext(SL_WQ, SZ_DD);
    slots[SL_WK] = make_ext(SL_WK, SZ_DD);
    slots[SL_WV] = make_ext(SL_WV, SZ_DD);
    slots[SL_WOUT] = make_ext(SL_WOUT, SZ_DD);
    slots[SL_G2] = make_ext(SL_G2, SZ_D);
    slots[SL_B2] = make_ext(SL_B2, SZ_D);
    slots[SL_WFF1] = make_ext(SL_WFF1, SZ_DFF);
    slots[SL_WFF2] = make_ext(SL_WFF2, SZ_FFD);
    slots[SL_WHEAD] = make_ext(SL_WHEAD, SZ_DNCLS);

    slots[SL_NORM1] = make_int(SL_NORM1, SZ_BSD, 0, 3);
    slots[SL_Q] = make_int(SL_Q, SZ_BSD, 1, 4);
    slots[SL_K] = make_int(SL_K, SZ_BSD, 2, 4);
    slots[SL_V] = make_int(SL_V, SZ_BSD, 3, 4);
    slots[SL_ATTN] = make_int(SL_ATTN, SZ_BSD, 4, 5);
    slots[SL_PROJ] = make_int(SL_PROJ, SZ_BSD, 5, 6);
    slots[SL_RES1] = make_int(SL_RES1, SZ_BSD, 6, 11);
    slots[SL_NORM2] = make_int(SL_NORM2, SZ_BSD, 7, 8);
    slots[SL_FF1] = make_int(SL_FF1, SZ_BSDFF, 8, 9);
    slots[SL_RELU] = make_int(SL_RELU, SZ_BSDFF, 9, 10);
    slots[SL_FF2] = make_int(SL_FF2, SZ_BSD, 10, 11);
    // The last real consumer of RES2 is op 12, but the check after the loop
    // reads it too. Declaring death at the final op stops the sweep line
    // from handing its storage to a later activation.
    slots[SL_RES2] = make_int(SL_RES2, SZ_BSD, 11, 14);
    slots[SL_CLS] = make_int(SL_CLS, SZ_BD, 12, 13);
    slots[SL_LOGITS] = make_int(SL_LOGITS, SZ_BNCLS, 13, 14);
    slots[SL_PROBS] = make_int(SL_PROBS, SZ_BNCLS, 14, 14);

    BackgroundThread bt;
    auto* plan = bt.compute_memory_plan(test.alloc, slots, N_SLOTS);
    assert(plan != nullptr);

    std::printf("  plan: pool=%lu bytes, %u slots (%u external)\n", static_cast<unsigned long>(plan->pool_bytes),
                plan->num_slots, plan->num_external);

    SlotId op0_in[] = {SlotId{SL_X}, SlotId{SL_G1}, SlotId{SL_B1}};
    SlotId op0_out[] = {SlotId{SL_NORM1}};

    SlotId op1_in[] = {SlotId{SL_NORM1}, SlotId{SL_WQ}};
    SlotId op1_out[] = {SlotId{SL_Q}};

    SlotId op2_in[] = {SlotId{SL_NORM1}, SlotId{SL_WK}};
    SlotId op2_out[] = {SlotId{SL_K}};

    SlotId op3_in[] = {SlotId{SL_NORM1}, SlotId{SL_WV}};
    SlotId op3_out[] = {SlotId{SL_V}};

    SlotId op4_in[] = {SlotId{SL_Q}, SlotId{SL_K}, SlotId{SL_V}};
    SlotId op4_out[] = {SlotId{SL_ATTN}};

    SlotId op5_in[] = {SlotId{SL_ATTN}, SlotId{SL_WOUT}};
    SlotId op5_out[] = {SlotId{SL_PROJ}};

    SlotId op6_in[] = {SlotId{SL_PROJ}, SlotId{SL_X}};
    SlotId op6_out[] = {SlotId{SL_RES1}};

    SlotId op7_in[] = {SlotId{SL_RES1}, SlotId{SL_G2}, SlotId{SL_B2}};
    SlotId op7_out[] = {SlotId{SL_NORM2}};

    SlotId op8_in[] = {SlotId{SL_NORM2}, SlotId{SL_WFF1}};
    SlotId op8_out[] = {SlotId{SL_FF1}};

    SlotId op9_in[] = {SlotId{SL_FF1}};
    SlotId op9_out[] = {SlotId{SL_RELU}};

    SlotId op10_in[] = {SlotId{SL_RELU}, SlotId{SL_WFF2}};
    SlotId op10_out[] = {SlotId{SL_FF2}};

    SlotId op11_in[] = {SlotId{SL_FF2}, SlotId{SL_RES1}};
    SlotId op11_out[] = {SlotId{SL_RES2}};

    SlotId op12_in[] = {SlotId{SL_RES2}};
    SlotId op12_out[] = {SlotId{SL_CLS}};

    SlotId op13_in[] = {SlotId{SL_CLS}, SlotId{SL_WHEAD}};
    SlotId op13_out[] = {SlotId{SL_LOGITS}};

    SlotId op14_in[] = {SlotId{SL_LOGITS}};
    SlotId op14_out[] = {SlotId{SL_PROBS}};

    struct OpDef {
        SchemaHash schema;
        ShapeHash shape;
        SlotId* in;
        uint16_t n_in;
        SlotId* out;
        uint16_t n_out;
    };
    OpDef defs[N_OPS] = {
        {.schema = H_LN1, .shape = S_LN1, .in = op0_in, .n_in = 3, .out = op0_out, .n_out = 1},
        {.schema = H_MMQ, .shape = S_MMQ, .in = op1_in, .n_in = 2, .out = op1_out, .n_out = 1},
        {.schema = H_MMK, .shape = S_MMK, .in = op2_in, .n_in = 2, .out = op2_out, .n_out = 1},
        {.schema = H_MMV, .shape = S_MMV, .in = op3_in, .n_in = 2, .out = op3_out, .n_out = 1},
        {.schema = H_SDPA, .shape = S_SDPA, .in = op4_in, .n_in = 3, .out = op4_out, .n_out = 1},
        {.schema = H_MMOUT, .shape = S_MMOUT, .in = op5_in, .n_in = 2, .out = op5_out, .n_out = 1},
        {.schema = H_ADD1, .shape = S_ADD1, .in = op6_in, .n_in = 2, .out = op6_out, .n_out = 1},
        {.schema = H_LN2, .shape = S_LN2, .in = op7_in, .n_in = 3, .out = op7_out, .n_out = 1},
        {.schema = H_MMFF1, .shape = S_MMFF1, .in = op8_in, .n_in = 2, .out = op8_out, .n_out = 1},
        {.schema = H_RELU, .shape = S_RELU, .in = op9_in, .n_in = 1, .out = op9_out, .n_out = 1},
        {.schema = H_MMFF2, .shape = S_MMFF2, .in = op10_in, .n_in = 2, .out = op10_out, .n_out = 1},
        {.schema = H_ADD2, .shape = S_ADD2, .in = op11_in, .n_in = 2, .out = op11_out, .n_out = 1},
        {.schema = H_IDXSEL, .shape = S_IDXSEL, .in = op12_in, .n_in = 1, .out = op12_out, .n_out = 1},
        {.schema = H_MMHEAD, .shape = S_MMHEAD, .in = op13_in, .n_in = 2, .out = op13_out, .n_out = 1},
        {.schema = H_SOFTMAX, .shape = S_SOFTMAX, .in = op14_in, .n_in = 1, .out = op14_out, .n_out = 1},
    };

    TraceEntry ops[N_OPS]{};
    for (uint32_t i = 0; i < N_OPS; i++) {
        ops[i].schema_hash = defs[i].schema;
        ops[i].shape_hash = defs[i].shape;
        ops[i].num_inputs = defs[i].n_in;
        ops[i].num_outputs = defs[i].n_out;
        ops[i].input_slot_ids = defs[i].in;
        ops[i].output_slot_ids = defs[i].out;
    }

    RegionNode region{};
    region.kind = TraceNodeKind::REGION;
    region.ops = ops;
    region.num_ops = N_OPS;
    region.plan = plan;

    CrucibleContext ctx;
    assert(ctx.activate(&region));
    assert(ctx.is_compiled());

    auto cv = ctx.mint_compiled_view(kVigilForeground);
    const auto bind = [&](uint32_t slot, void* ptr) {
        ctx.register_external(SlotId{slot}, ::fixy::mint_refined<::fixy::non_null>(ptr), cv);
    };

    bind(SL_X, weights.X);
    bind(SL_G1, weights.gamma1);
    bind(SL_B1, weights.beta1);
    bind(SL_WQ, weights.W_q);
    bind(SL_WK, weights.W_k);
    bind(SL_WV, weights.W_v);
    bind(SL_WOUT, weights.W_out);
    bind(SL_G2, weights.gamma2);
    bind(SL_B2, weights.beta2);
    bind(SL_WFF1, weights.W_ff1);
    bind(SL_WFF2, weights.W_ff2);
    bind(SL_WHEAD, weights.W_head);

    for (int iter = 0; iter < 50; iter++) {
        run_attention_ops(ctx, cv);
        run_ffn_and_head_ops(ctx, cv);
    }

    assert(ctx.compiled_iterations() == 50);
    assert(ctx.diverged_count() == 0);

    Reference reference;
    compute_reference(weights, reference);

    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
    auto* pool_probs = static_cast<const float*>(ctx.pool().slot_ptr(SlotId{SL_PROBS}, pv));

    float max_err = 0.0f;
    for (int i = 0; i < B * N_CLS; i++) {
        float err = std::abs(pool_probs[i] - reference.probs[i]);
        max_err = std::max(max_err, err);
    }

    std::printf("  max_error vs reference: %.2e\n", static_cast<double>(max_err));
    assert(max_err < 1e-5f && "ViT output diverged from reference");

    for (int b = 0; b < B; b++) {
        float row_sum = 0.0f;
        for (int j = 0; j < N_CLS; j++) {
            float v = pool_probs[b * N_CLS + j];
            assert(v >= 0.0f && v <= 1.0f);
            row_sum += v;
        }
        assert(std::abs(row_sum - 1.0f) < 1e-5f && "softmax row does not sum to 1");
    }

    auto* pool_res2 = static_cast<const float*>(ctx.pool().slot_ptr(SlotId{SL_RES2}, pv));
    float max_res2_err = 0.0f;
    for (int i = 0; i < B * S * D; i++) {
        float err = std::abs(pool_res2[i] - reference.res2[i]);
        max_res2_err = std::max(max_res2_err, err);
    }
    std::printf("  resid2 max_error: %.2e\n", static_cast<double>(max_res2_err));
    assert(max_res2_err < 1e-5f && "residual output diverged from reference");

    std::printf("  probs[0] = [%.4f, %.4f, %.4f]\n", static_cast<double>(pool_probs[0]),
                static_cast<double>(pool_probs[1]), static_cast<double>(pool_probs[2]));
    std::printf("  probs[1] = [%.4f, %.4f, %.4f]\n", static_cast<double>(pool_probs[3]),
                static_cast<double>(pool_probs[4]), static_cast<double>(pool_probs[5]));
    std::printf("  50 iterations, %u ops/iter, compiled_iters=%u\n", N_OPS, ctx.compiled_iterations());

    std::printf("test_compute_vit: PASSED\n");
    return 0;
}
