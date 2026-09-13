// A four-operation forward pass carrying real numbers through the
// planned memory pool: a matrix multiply, a rectifier, a second
// matrix multiply, and a softmax.
//
// The planner is free to give two slots the same offset when their
// lifetimes do not overlap, which is the case for the two matrix
// multiply outputs here.  The result is therefore checked against a
// reference computed straight through, which holds whether or not the
// planner reused anything.

#include "cpu_kernels.h"

#include <crucible/BackgroundThread.h>
#include <crucible/CrucibleContext.h>
#include <crucible/effects/Capabilities.h>

#include "test_assert.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

using namespace crucible;

static constexpr int BATCH = 2;
static constexpr int IN_DIM = 4;
static constexpr int HIDDEN = 8;
static constexpr int OUT_DIM = 3;

// One hash per operation kind, and one per operation, both distinct
// so a mismatch names which one drifted.
static const SchemaHash H_MM1{0x100};
static const SchemaHash H_RELU{0x200};
static const SchemaHash H_MM2{0x300};
static const SchemaHash H_SOFTMAX{0x400};

static const ShapeHash S_MM1{0x1100};
static const ShapeHash S_RELU{0x1200};
static const ShapeHash S_MM2{0x1300};
static const ShapeHash S_SOFTMAX{0x1400};

// Slot IDs — 3 external (params/input) + 4 internal (activations)
static constexpr uint32_t SL_X = 0;  // input [2,4]
static constexpr uint32_t SL_W1 = 1;  // param [4,8]
static constexpr uint32_t SL_W2 = 2;  // param [8,3]
static constexpr uint32_t SL_MM1 = 3;  // mm1 output [2,8]
static constexpr uint32_t SL_REL = 4;  // relu output [2,8]
static constexpr uint32_t SL_MM2 = 5;  // mm2 output [2,3]
static constexpr uint32_t SL_SM = 6;  // softmax output [2,3]
static constexpr uint32_t N_SLOTS = 7;
static constexpr uint32_t N_OPS = 4;

int main() {
    auto test = effects::testing::test();
    std::printf("test_compute:\n");

    // A fixed seed, so the same numbers flow through on every run and
    // the reference comparison below is reproducible.
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

    alignas(64) float X[BATCH * IN_DIM];
    alignas(64) float W1[IN_DIM * HIDDEN];
    alignas(64) float W2[HIDDEN * OUT_DIM];

    for (auto& v : X)
        v = dist(rng);
    for (auto& v : W1)
        v = dist(rng);
    for (auto& v : W2)
        v = dist(rng);

    // An external slot brings its own memory, registered separately,
    // and lives for the whole iteration.  An internal slot is given an
    // offset by the planner, and its lifetime runs from the operation
    // that produces it to the last one that reads it.
    TensorSlot slots[N_SLOTS]{};

    slots[SL_X] = {.offset_bytes = 0,
                   .nbytes = BATCH * IN_DIM * 4,
                   .birth_op = OpIndex{0},
                   .death_op = OpIndex{N_OPS},
                   .dtype = ScalarType::Float,
                   .device_type = DeviceType::CPU,
                   .device_idx = 0,
                   .layout = Layout::Strided,
                   .is_external = true,
                   .pad = {},
                   .slot_id = SlotId{SL_X},
                   .pad2 = {}};
    slots[SL_W1] = {.offset_bytes = 0,
                    .nbytes = IN_DIM * HIDDEN * 4,
                    .birth_op = OpIndex{0},
                    .death_op = OpIndex{N_OPS},
                    .dtype = ScalarType::Float,
                    .device_type = DeviceType::CPU,
                    .device_idx = 0,
                    .layout = Layout::Strided,
                    .is_external = true,
                    .pad = {},
                    .slot_id = SlotId{SL_W1},
                    .pad2 = {}};
    slots[SL_W2] = {.offset_bytes = 0,
                    .nbytes = HIDDEN * OUT_DIM * 4,
                    .birth_op = OpIndex{0},
                    .death_op = OpIndex{N_OPS},
                    .dtype = ScalarType::Float,
                    .device_type = DeviceType::CPU,
                    .device_idx = 0,
                    .layout = Layout::Strided,
                    .is_external = true,
                    .pad = {},
                    .slot_id = SlotId{SL_W2},
                    .pad2 = {}};

    slots[SL_MM1] = {.offset_bytes = 0,
                     .nbytes = BATCH * HIDDEN * 4,
                     .birth_op = OpIndex{0},
                     .death_op = OpIndex{1},
                     .dtype = ScalarType::Float,
                     .device_type = DeviceType::CPU,
                     .device_idx = 0,
                     .layout = Layout::Strided,
                     .is_external = false,
                     .pad = {},
                     .slot_id = SlotId{SL_MM1},
                     .pad2 = {}};
    slots[SL_REL] = {.offset_bytes = 0,
                     .nbytes = BATCH * HIDDEN * 4,
                     .birth_op = OpIndex{1},
                     .death_op = OpIndex{2},
                     .dtype = ScalarType::Float,
                     .device_type = DeviceType::CPU,
                     .device_idx = 0,
                     .layout = Layout::Strided,
                     .is_external = false,
                     .pad = {},
                     .slot_id = SlotId{SL_REL},
                     .pad2 = {}};
    slots[SL_MM2] = {.offset_bytes = 0,
                     .nbytes = BATCH * OUT_DIM * 4,
                     .birth_op = OpIndex{2},
                     .death_op = OpIndex{3},
                     .dtype = ScalarType::Float,
                     .device_type = DeviceType::CPU,
                     .device_idx = 0,
                     .layout = Layout::Strided,
                     .is_external = false,
                     .pad = {},
                     .slot_id = SlotId{SL_MM2},
                     .pad2 = {}};
    slots[SL_SM] = {.offset_bytes = 0,
                    .nbytes = BATCH * OUT_DIM * 4,
                    .birth_op = OpIndex{3},
                    .death_op = OpIndex{3},
                    .dtype = ScalarType::Float,
                    .device_type = DeviceType::CPU,
                    .device_idx = 0,
                    .layout = Layout::Strided,
                    .is_external = false,
                    .pad = {},
                    .slot_id = SlotId{SL_SM},
                    .pad2 = {}};

    BackgroundThread bt;
    auto* plan = bt.compute_memory_plan(test.alloc, slots, N_SLOTS);
    assert(plan != nullptr);

    std::printf("  plan: pool=%lu bytes, %u slots (%u external)\n", static_cast<unsigned long>(plan->pool_bytes),
                plan->num_slots, plan->num_external);

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
    ops[0].shape_hash = S_MM1;
    ops[0].num_inputs = 2;
    ops[0].num_outputs = 1;
    ops[0].input_slot_ids = op0_in;
    ops[0].output_slot_ids = op0_out;

    ops[1].schema_hash = H_RELU;
    ops[1].shape_hash = S_RELU;
    ops[1].num_inputs = 1;
    ops[1].num_outputs = 1;
    ops[1].input_slot_ids = op1_in;
    ops[1].output_slot_ids = op1_out;

    ops[2].schema_hash = H_MM2;
    ops[2].shape_hash = S_MM2;
    ops[2].num_inputs = 2;
    ops[2].num_outputs = 1;
    ops[2].input_slot_ids = op2_in;
    ops[2].output_slot_ids = op2_out;

    ops[3].schema_hash = H_SOFTMAX;
    ops[3].shape_hash = S_SOFTMAX;
    ops[3].num_inputs = 1;
    ops[3].num_outputs = 1;
    ops[3].input_slot_ids = op3_in;
    ops[3].output_slot_ids = op3_out;

    RegionNode region{};
    region.kind = TraceNodeKind::REGION;
    region.ops = ops;
    region.num_ops = N_OPS;
    region.plan = plan;

    CrucibleContext ctx;
    assert(ctx.activate(&region));
    assert(ctx.is_compiled());

    // The view is minted once, because the context stays compiled for
    // the whole loop.  Passing it to every call costs nothing at run
    // time and makes each call site state, in its own types, that it
    // is only reachable in compiled mode.
    auto cv = ctx.mint_compiled_view();

    ctx.register_external(SlotId{SL_X}, crucible::safety::NonNull<void*>{X}, cv);
    ctx.register_external(SlotId{SL_W1}, crucible::safety::NonNull<void*>{W1}, cv);
    ctx.register_external(SlotId{SL_W2}, crucible::safety::NonNull<void*>{W2}, cv);

    // A read-only view, so that even these checks reach the pool the
    // way the hot path does.
    auto pv = ctx.pool().mint_initialized_view();
    assert(ctx.pool().slot_ptr(SlotId{SL_X}, pv) == X);
    assert(ctx.pool().slot_ptr(SlotId{SL_W1}, pv) == W1);
    assert(ctx.pool().slot_ptr(SlotId{SL_W2}, pv) == W2);

    for (int iter = 0; iter < 100; iter++) {
        // The trailing names below say which slot each index reaches,
        // which the index alone does not.
        auto s = ctx.advance(H_MM1, S_MM1, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)),  // X
                static_cast<const float*>(ctx.input_ptr(1, cv)),  // W1
                static_cast<float*>(ctx.output_ptr(0, cv)),  // mm1_out
                BATCH, HIDDEN, IN_DIM);

        s = ctx.advance(H_RELU, S_RELU, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::relu(static_cast<const float*>(ctx.input_ptr(0, cv)),  // mm1_out
                  static_cast<float*>(ctx.output_ptr(0, cv)),  // relu_out
                  BATCH * HIDDEN);

        s = ctx.advance(H_MM2, S_MM2, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)),  // relu_out
                static_cast<const float*>(ctx.input_ptr(1, cv)),  // W2
                static_cast<float*>(ctx.output_ptr(0, cv)),  // mm2_out
                BATCH, OUT_DIM, HIDDEN);

        // The last operation of an iteration completes it rather
        // than matching.
        s = ctx.advance(H_SOFTMAX, S_SOFTMAX, cv);
        assert(s == ReplayStatus::COMPLETE);
        cpu::softmax(static_cast<const float*>(ctx.input_ptr(0, cv)),  // mm2_out
                     static_cast<float*>(ctx.output_ptr(0, cv)),  // softmax_out
                     BATCH, OUT_DIM);
    }

    assert(ctx.compiled_iterations() == 100);
    assert(ctx.diverged_count() == 0);

    float ref_mm1[BATCH * HIDDEN]{};
    float ref_relu[BATCH * HIDDEN]{};
    float ref_mm2[BATCH * OUT_DIM]{};
    float ref_sm[BATCH * OUT_DIM]{};

    cpu::mm(X, W1, ref_mm1, BATCH, HIDDEN, IN_DIM);
    cpu::relu(ref_mm1, ref_relu, BATCH * HIDDEN);
    cpu::mm(ref_relu, W2, ref_mm2, BATCH, OUT_DIM, HIDDEN);
    cpu::softmax(ref_mm2, ref_sm, BATCH, OUT_DIM);

    auto* pool_sm = static_cast<const float*>(ctx.pool().slot_ptr(SlotId{SL_SM}, pv));

    float max_err = 0.0f;
    for (int i = 0; i < BATCH * OUT_DIM; i++) {
        float err = std::abs(pool_sm[i] - ref_sm[i]);
        max_err = std::max(max_err, err);
    }

    std::printf("  max_error vs reference: %.2e\n", static_cast<double>(max_err));
    assert(max_err < 1e-5f && "softmax output diverged from reference");

    for (int b = 0; b < BATCH; b++) {
        float row_sum = 0.0f;
        for (int j = 0; j < OUT_DIM; j++) {
            float v = pool_sm[b * OUT_DIM + j];
            assert(v >= 0.0f && v <= 1.0f);
            row_sum += v;
        }
        assert(std::abs(row_sum - 1.0f) < 1e-5f && "softmax row doesn't sum to 1");
    }

    std::printf("  softmax[0] = [%.4f, %.4f, %.4f]\n", static_cast<double>(pool_sm[0]), static_cast<double>(pool_sm[1]),
                static_cast<double>(pool_sm[2]));
    std::printf("  softmax[1] = [%.4f, %.4f, %.4f]\n", static_cast<double>(pool_sm[3]), static_cast<double>(pool_sm[4]),
                static_cast<double>(pool_sm[5]));
    std::printf("  100 iterations, %u ops/iter, compiled_iters=%u\n", N_OPS, ctx.compiled_iterations());

    std::printf("test_compute: PASSED\n");
    return 0;
}
