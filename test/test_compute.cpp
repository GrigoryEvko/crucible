// End-to-end compute: MLP forward pass with real numbers through PoolAllocator.
//
// Model: X[2,4] → mm(W1[4,8]) → relu → mm(W2[8,3]) → softmax → Y[2,3]
//
// 4 ops per iteration. CrucibleContext manages the PoolAllocator and
// ReplayEngine. Each op: advance() → execute CPU kernel using
// input_ptr(j) / output_ptr(j) → next op.
//
// The sweep-line MAY reuse memory for non-overlapping slots (mm1_out
// and mm2_out have disjoint lifetimes). The final softmax output is
// verified against a direct CPU reference computation to prove the
// entire data flow chain works correctly regardless of memory reuse.

#include "cpu_kernels.h"

#include <crucible/BackgroundThread.h>
#include <crucible/CrucibleContext.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

using namespace crucible;

// ─── Model dimensions ────────────────────────────────────────────────
static constexpr int BATCH = 2;
static constexpr int IN_DIM = 4;
static constexpr int HIDDEN = 8;
static constexpr int OUT_DIM = 3;

// Schema hashes (unique per op type)
static const SchemaHash H_MM1    {0x100};
static const SchemaHash H_RELU   {0x200};
static const SchemaHash H_MM2    {0x300};
static const SchemaHash H_SOFTMAX{0x400};

// Shape hashes (unique per op)
static const ShapeHash S_MM1    {0x1100};
static const ShapeHash S_RELU   {0x1200};
static const ShapeHash S_MM2    {0x1300};
static const ShapeHash S_SOFTMAX{0x1400};

// Slot IDs — 3 external (params/input) + 4 internal (activations)
static constexpr uint32_t SL_X   = 0;  // input [2,4]
static constexpr uint32_t SL_W1  = 1;  // param [4,8]
static constexpr uint32_t SL_W2  = 2;  // param [8,3]
static constexpr uint32_t SL_MM1 = 3;  // mm1 output [2,8]
static constexpr uint32_t SL_REL = 4;  // relu output [2,8]
static constexpr uint32_t SL_MM2 = 5;  // mm2 output [2,3]
static constexpr uint32_t SL_SM  = 6;  // softmax output [2,3]
static constexpr uint32_t N_SLOTS = 7;
static constexpr uint32_t N_OPS = 4;

int main() {
    std::printf("test_compute:\n");

    // ── Initialize parameters with seeded random values ──────────────
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

    alignas(64) float X[BATCH * IN_DIM];
    alignas(64) float W1[IN_DIM * HIDDEN];
    alignas(64) float W2[HIDDEN * OUT_DIM];

    for (auto& v : X)  v = dist(rng);
    for (auto& v : W1) v = dist(rng);
    for (auto& v : W2) v = dist(rng);

    // ── Build TensorSlots ────────────────────────────────────────────
    //
    // External slots have is_external=true; their memory is registered
    // separately via register_external(). Internal slots get offsets
    // from the sweep-line allocator.
    //
    // Liveness ranges: birth = producing op, death = last consuming op.
    TensorSlot slots[N_SLOTS]{};

    // External: live for the entire iteration
    slots[SL_X]  = {0, BATCH*IN_DIM*4,  SlotId{SL_X},  OpIndex{0}, OpIndex{N_OPS},
                    ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, true, {}};
    slots[SL_W1] = {0, IN_DIM*HIDDEN*4,  SlotId{SL_W1}, OpIndex{0}, OpIndex{N_OPS},
                    ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, true, {}};
    slots[SL_W2] = {0, HIDDEN*OUT_DIM*4, SlotId{SL_W2}, OpIndex{0}, OpIndex{N_OPS},
                    ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, true, {}};

    // Internal: assigned by sweep-line
    slots[SL_MM1] = {0, BATCH*HIDDEN*4,  SlotId{SL_MM1}, OpIndex{0}, OpIndex{1},
                     ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}};
    slots[SL_REL] = {0, BATCH*HIDDEN*4,  SlotId{SL_REL}, OpIndex{1}, OpIndex{2},
                     ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}};
    slots[SL_MM2] = {0, BATCH*OUT_DIM*4, SlotId{SL_MM2}, OpIndex{2}, OpIndex{3},
                     ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}};
    slots[SL_SM]  = {0, BATCH*OUT_DIM*4, SlotId{SL_SM},  OpIndex{3}, OpIndex{3},
                     ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}};

    // Sweep-line offset assignment
    BackgroundThread bt;
    auto* plan = bt.compute_memory_plan(slots, N_SLOTS);
    assert(plan != nullptr);

    std::printf("  plan: pool=%lu bytes, %u slots (%u external)\n",
                static_cast<unsigned long>(plan->pool_bytes),
                plan->num_slots, plan->num_external);

    // ── Build TraceEntry ops with slot assignments ───────────────────
    SlotId op0_in[2] = {SlotId{SL_X}, SlotId{SL_W1}};
    SlotId op0_out[1] = {SlotId{SL_MM1}};

    SlotId op1_in[1] = {SlotId{SL_MM1}};
    SlotId op1_out[1] = {SlotId{SL_REL}};

    SlotId op2_in[2] = {SlotId{SL_REL}, SlotId{SL_W2}};
    SlotId op2_out[1] = {SlotId{SL_MM2}};

    SlotId op3_in[1] = {SlotId{SL_MM2}};
    SlotId op3_out[1] = {SlotId{SL_SM}};

    TraceEntry ops[N_OPS]{};

    ops[0].schema_hash = H_MM1;
    ops[0].shape_hash  = S_MM1;
    ops[0].num_inputs  = 2;
    ops[0].num_outputs = 1;
    ops[0].input_slot_ids  = op0_in;
    ops[0].output_slot_ids = op0_out;

    ops[1].schema_hash = H_RELU;
    ops[1].shape_hash  = S_RELU;
    ops[1].num_inputs  = 1;
    ops[1].num_outputs = 1;
    ops[1].input_slot_ids  = op1_in;
    ops[1].output_slot_ids = op1_out;

    ops[2].schema_hash = H_MM2;
    ops[2].shape_hash  = S_MM2;
    ops[2].num_inputs  = 2;
    ops[2].num_outputs = 1;
    ops[2].input_slot_ids  = op2_in;
    ops[2].output_slot_ids = op2_out;

    ops[3].schema_hash = H_SOFTMAX;
    ops[3].shape_hash  = S_SOFTMAX;
    ops[3].num_inputs  = 1;
    ops[3].num_outputs = 1;
    ops[3].input_slot_ids  = op3_in;
    ops[3].output_slot_ids = op3_out;

    // ── Build RegionNode ─────────────────────────────────────────────
    RegionNode region{};
    region.kind = TraceNodeKind::REGION;
    region.ops = ops;
    region.num_ops = N_OPS;
    region.plan = plan;

    // ── Activate CrucibleContext ─────────────────────────────────────
    CrucibleContext ctx;
    assert(ctx.activate(&region));
    assert(ctx.is_compiled());

    ctx.register_external(SlotId{SL_X},  X);
    ctx.register_external(SlotId{SL_W1}, W1);
    ctx.register_external(SlotId{SL_W2}, W2);

    // Verify externals are registered correctly
    assert(ctx.pool().slot_ptr(SlotId{SL_X})  == X);
    assert(ctx.pool().slot_ptr(SlotId{SL_W1}) == W1);
    assert(ctx.pool().slot_ptr(SlotId{SL_W2}) == W2);

    // ── Run 100 compiled iterations ──────────────────────────────────
    for (int iter = 0; iter < 100; iter++) {
        // Op 0: mm(X, W1) → mm1_out [BATCH, HIDDEN]
        auto s = ctx.advance(H_MM1, S_MM1);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0)),  // X
                static_cast<const float*>(ctx.input_ptr(1)),  // W1
                static_cast<float*>(ctx.output_ptr(0)),       // mm1_out
                BATCH, HIDDEN, IN_DIM);

        // Op 1: relu(mm1_out) → relu_out [BATCH, HIDDEN]
        s = ctx.advance(H_RELU, S_RELU);
        assert(s == ReplayStatus::MATCH);
        cpu::relu(static_cast<const float*>(ctx.input_ptr(0)),  // mm1_out
                  static_cast<float*>(ctx.output_ptr(0)),       // relu_out
                  BATCH * HIDDEN);

        // Op 2: mm(relu_out, W2) → mm2_out [BATCH, OUT_DIM]
        s = ctx.advance(H_MM2, S_MM2);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0)),  // relu_out
                static_cast<const float*>(ctx.input_ptr(1)),  // W2
                static_cast<float*>(ctx.output_ptr(0)),       // mm2_out
                BATCH, OUT_DIM, HIDDEN);

        // Op 3: softmax(mm2_out) → softmax_out [BATCH, OUT_DIM]
        s = ctx.advance(H_SOFTMAX, S_SOFTMAX);
        assert(s == ReplayStatus::COMPLETE);
        cpu::softmax(static_cast<const float*>(ctx.input_ptr(0)),  // mm2_out
                     static_cast<float*>(ctx.output_ptr(0)),       // softmax_out
                     BATCH, OUT_DIM);
    }

    assert(ctx.compiled_iterations() == 100);
    assert(ctx.diverged_count() == 0);

    // ── Verify against direct CPU reference ──────────────────────────
    float ref_mm1[BATCH * HIDDEN]{};
    float ref_relu[BATCH * HIDDEN]{};
    float ref_mm2[BATCH * OUT_DIM]{};
    float ref_sm[BATCH * OUT_DIM]{};

    cpu::mm(X, W1, ref_mm1, BATCH, HIDDEN, IN_DIM);
    cpu::relu(ref_mm1, ref_relu, BATCH * HIDDEN);
    cpu::mm(ref_relu, W2, ref_mm2, BATCH, OUT_DIM, HIDDEN);
    cpu::softmax(ref_mm2, ref_sm, BATCH, OUT_DIM);

    // Read final softmax from the pool
    auto* pool_sm = static_cast<const float*>(
        ctx.pool().slot_ptr(SlotId{SL_SM}));

    float max_err = 0.0f;
    for (int i = 0; i < BATCH * OUT_DIM; i++) {
        float err = std::abs(pool_sm[i] - ref_sm[i]);
        max_err = std::max(max_err, err);
    }

    std::printf("  max_error vs reference: %.2e\n", static_cast<double>(max_err));
    assert(max_err < 1e-5f && "softmax output diverged from reference");

    // Verify softmax properties: rows sum to 1, values in [0,1]
    for (int b = 0; b < BATCH; b++) {
        float row_sum = 0.0f;
        for (int j = 0; j < OUT_DIM; j++) {
            float v = pool_sm[b * OUT_DIM + j];
            assert(v >= 0.0f && v <= 1.0f);
            row_sum += v;
        }
        assert(std::abs(row_sum - 1.0f) < 1e-5f && "softmax row doesn't sum to 1");
    }

    // Print the actual outputs for visual inspection
    std::printf("  softmax[0] = [%.4f, %.4f, %.4f]\n",
                static_cast<double>(pool_sm[0]), static_cast<double>(pool_sm[1]),
                static_cast<double>(pool_sm[2]));
    std::printf("  softmax[1] = [%.4f, %.4f, %.4f]\n",
                static_cast<double>(pool_sm[3]), static_cast<double>(pool_sm[4]),
                static_cast<double>(pool_sm[5]));
    std::printf("  100 iterations, %u ops/iter, compiled_iters=%u\n",
                N_OPS, ctx.compiled_iterations());

    std::printf("test_compute: PASSED\n");
    return 0;
}
