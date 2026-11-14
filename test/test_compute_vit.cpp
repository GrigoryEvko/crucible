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

#include "entry_arrays.h"
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

    ::foundation::core::FixedArray<TensorSlot, N_SLOTS> slots{};

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

    slots.at(::foundation::core::index<SL_X>()) = make_ext(SL_X, SZ_BSD);
    slots.at(::foundation::core::index<SL_G1>()) = make_ext(SL_G1, SZ_D);
    slots.at(::foundation::core::index<SL_B1>()) = make_ext(SL_B1, SZ_D);
    slots.at(::foundation::core::index<SL_WQ>()) = make_ext(SL_WQ, SZ_DD);
    slots.at(::foundation::core::index<SL_WK>()) = make_ext(SL_WK, SZ_DD);
    slots.at(::foundation::core::index<SL_WV>()) = make_ext(SL_WV, SZ_DD);
    slots.at(::foundation::core::index<SL_WOUT>()) = make_ext(SL_WOUT, SZ_DD);
    slots.at(::foundation::core::index<SL_G2>()) = make_ext(SL_G2, SZ_D);
    slots.at(::foundation::core::index<SL_B2>()) = make_ext(SL_B2, SZ_D);
    slots.at(::foundation::core::index<SL_WFF1>()) = make_ext(SL_WFF1, SZ_DFF);
    slots.at(::foundation::core::index<SL_WFF2>()) = make_ext(SL_WFF2, SZ_FFD);
    slots.at(::foundation::core::index<SL_WHEAD>()) = make_ext(SL_WHEAD, SZ_DNCLS);

    slots.at(::foundation::core::index<SL_NORM1>()) = make_int(SL_NORM1, SZ_BSD, 0, 3);
    slots.at(::foundation::core::index<SL_Q>()) = make_int(SL_Q, SZ_BSD, 1, 4);
    slots.at(::foundation::core::index<SL_K>()) = make_int(SL_K, SZ_BSD, 2, 4);
    slots.at(::foundation::core::index<SL_V>()) = make_int(SL_V, SZ_BSD, 3, 4);
    slots.at(::foundation::core::index<SL_ATTN>()) = make_int(SL_ATTN, SZ_BSD, 4, 5);
    slots.at(::foundation::core::index<SL_PROJ>()) = make_int(SL_PROJ, SZ_BSD, 5, 6);
    slots.at(::foundation::core::index<SL_RES1>()) = make_int(SL_RES1, SZ_BSD, 6, 11);
    slots.at(::foundation::core::index<SL_NORM2>()) = make_int(SL_NORM2, SZ_BSD, 7, 8);
    slots.at(::foundation::core::index<SL_FF1>()) = make_int(SL_FF1, SZ_BSDFF, 8, 9);
    slots.at(::foundation::core::index<SL_RELU>()) = make_int(SL_RELU, SZ_BSDFF, 9, 10);
    slots.at(::foundation::core::index<SL_FF2>()) = make_int(SL_FF2, SZ_BSD, 10, 11);
    // The last real consumer of RES2 is op 12, but the check after the loop
    // reads it too. Declaring death at the final op stops the sweep line
    // from handing its storage to a later activation.
    slots.at(::foundation::core::index<SL_RES2>()) = make_int(SL_RES2, SZ_BSD, 11, 14);
    slots.at(::foundation::core::index<SL_CLS>()) = make_int(SL_CLS, SZ_BD, 12, 13);
    slots.at(::foundation::core::index<SL_LOGITS>()) = make_int(SL_LOGITS, SZ_BNCLS, 13, 14);
    slots.at(::foundation::core::index<SL_PROBS>()) = make_int(SL_PROBS, SZ_BNCLS, 14, 14);

    BackgroundThread bt;
    crucible::MemoryPlan plan_storage{};
    ::fixy::BumpArena sweep_arena{size_t{1} << 16};
    auto* plan = bt.compute_memory_plan(test.alloc, plan_storage, sweep_arena, slots.view());
    assert(plan != nullptr);

    std::printf("  plan: pool=%lu bytes, %u slots (%u external)\n", static_cast<unsigned long>(plan->pool_bytes),
                plan->num_slots(), plan->num_external);

    // The slot ids of each op are arrays of this arena.
    ::fixy::BumpArena runs{size_t{1} << 16};
    const auto slots_of = [&](auto... slot_numbers) noexcept {
        return crucible::test::run_of(test.alloc, runs, SlotId{slot_numbers}...);
    };

    struct OpDef {
        SchemaHash schema;
        ShapeHash shape;
        ::fixy::View<SlotId> in;
        ::fixy::View<SlotId> out;
    };
    const ::foundation::core::FixedArray<OpDef, N_OPS> defs{
        OpDef{.schema = H_LN1, .shape = S_LN1, .in = slots_of(SL_X, SL_G1, SL_B1), .out = slots_of(SL_NORM1)},
        OpDef{.schema = H_MMQ, .shape = S_MMQ, .in = slots_of(SL_NORM1, SL_WQ), .out = slots_of(SL_Q)},
        OpDef{.schema = H_MMK, .shape = S_MMK, .in = slots_of(SL_NORM1, SL_WK), .out = slots_of(SL_K)},
        OpDef{.schema = H_MMV, .shape = S_MMV, .in = slots_of(SL_NORM1, SL_WV), .out = slots_of(SL_V)},
        OpDef{.schema = H_SDPA, .shape = S_SDPA, .in = slots_of(SL_Q, SL_K, SL_V), .out = slots_of(SL_ATTN)},
        OpDef{.schema = H_MMOUT, .shape = S_MMOUT, .in = slots_of(SL_ATTN, SL_WOUT), .out = slots_of(SL_PROJ)},
        OpDef{.schema = H_ADD1, .shape = S_ADD1, .in = slots_of(SL_PROJ, SL_X), .out = slots_of(SL_RES1)},
        OpDef{.schema = H_LN2, .shape = S_LN2, .in = slots_of(SL_RES1, SL_G2, SL_B2), .out = slots_of(SL_NORM2)},
        OpDef{.schema = H_MMFF1, .shape = S_MMFF1, .in = slots_of(SL_NORM2, SL_WFF1), .out = slots_of(SL_FF1)},
        OpDef{.schema = H_RELU, .shape = S_RELU, .in = slots_of(SL_FF1), .out = slots_of(SL_RELU)},
        OpDef{.schema = H_MMFF2, .shape = S_MMFF2, .in = slots_of(SL_RELU, SL_WFF2), .out = slots_of(SL_FF2)},
        OpDef{.schema = H_ADD2, .shape = S_ADD2, .in = slots_of(SL_FF2, SL_RES1), .out = slots_of(SL_RES2)},
        OpDef{.schema = H_IDXSEL, .shape = S_IDXSEL, .in = slots_of(SL_RES2), .out = slots_of(SL_CLS)},
        OpDef{.schema = H_MMHEAD, .shape = S_MMHEAD, .in = slots_of(SL_CLS, SL_WHEAD), .out = slots_of(SL_LOGITS)},
        OpDef{.schema = H_SOFTMAX, .shape = S_SOFTMAX, .in = slots_of(SL_LOGITS), .out = slots_of(SL_PROBS)},
    };

    ::foundation::core::FixedArray<TraceEntry, N_OPS> ops{};
    for (auto [op, def] : ::fixy::zip(ops.view(), defs.view())) {
        op->schema_hash = def->schema;
        op->shape_hash = def->shape;
        crucible::test::set_input_slots(test.alloc, runs, *op, def->in);
        crucible::test::set_output_slots(test.alloc, runs, *op, def->out);
    }

    RegionNode region{};
    region.kind = TraceNodeKind::REGION;
    region.ops = ops.view();
    region.plan = plan;

    CrucibleContext ctx;
    assert(ctx.activate(region));
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
