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

#include "cpu_kernels.h"

#include <crucible/BackgroundThread.h>
#include <crucible/CrucibleContext.h>
#include <crucible/effects/_Capabilities.h>

#include "test_assert.h"
#include <cmath>
#include <cstdio>
#include <random>

using namespace crucible;

// The dimensions are small so the reference pass stays cheap, but still
// differ from each other so that equal-sized slots cannot mask an offset
// assignment bug: D and D_FF differ, and [B,S,D], [B,D] and [B,N_CLS] are
// three distinct activation footprints.
static constexpr int B = 2;
static constexpr int S = 4;
static constexpr int D = 8;
static constexpr int D_FF = 16;
static constexpr int N_CLS = 3;

static constexpr uint32_t N_OPS = 15;

static constexpr SchemaHash H_LN1{0x100};
static constexpr SchemaHash H_MMQ{0x200};
static constexpr SchemaHash H_MMK{0x300};
static constexpr SchemaHash H_MMV{0x400};
static constexpr SchemaHash H_SDPA{0x500};
static constexpr SchemaHash H_MMOUT{0x600};
static constexpr SchemaHash H_ADD1{0x700};
static constexpr SchemaHash H_LN2{0x800};
static constexpr SchemaHash H_MMFF1{0x900};
static constexpr SchemaHash H_RELU{0xA00};
static constexpr SchemaHash H_MMFF2{0xB00};
static constexpr SchemaHash H_ADD2{0xC00};
static constexpr SchemaHash H_IDXSEL{0xD00};
static constexpr SchemaHash H_MMHEAD{0xE00};
static constexpr SchemaHash H_SOFTMAX{0xF00};

static constexpr ShapeHash S_LN1{0x1100};
static constexpr ShapeHash S_MMQ{0x1200};
static constexpr ShapeHash S_MMK{0x1300};
static constexpr ShapeHash S_MMV{0x1400};
static constexpr ShapeHash S_SDPA{0x1500};
static constexpr ShapeHash S_MMOUT{0x1600};
static constexpr ShapeHash S_ADD1{0x1700};
static constexpr ShapeHash S_LN2{0x1800};
static constexpr ShapeHash S_MMFF1{0x1900};
static constexpr ShapeHash S_RELU{0x1A00};
static constexpr ShapeHash S_MMFF2{0x1B00};
static constexpr ShapeHash S_ADD2{0x1C00};
static constexpr ShapeHash S_IDXSEL{0x1D00};
static constexpr ShapeHash S_MMHEAD{0x1E00};
static constexpr ShapeHash S_SOFTMAX{0x1F00};

static constexpr uint32_t SL_X = 0;
static constexpr uint32_t SL_G1 = 1;
static constexpr uint32_t SL_B1 = 2;
static constexpr uint32_t SL_WQ = 3;
static constexpr uint32_t SL_WK = 4;
static constexpr uint32_t SL_WV = 5;
static constexpr uint32_t SL_WOUT = 6;
static constexpr uint32_t SL_G2 = 7;
static constexpr uint32_t SL_B2 = 8;
static constexpr uint32_t SL_WFF1 = 9;
static constexpr uint32_t SL_WFF2 = 10;
static constexpr uint32_t SL_WHEAD = 11;

static constexpr uint32_t SL_NORM1 = 12;
static constexpr uint32_t SL_Q = 13;
static constexpr uint32_t SL_K = 14;
static constexpr uint32_t SL_V = 15;
static constexpr uint32_t SL_ATTN = 16;
static constexpr uint32_t SL_PROJ = 17;
static constexpr uint32_t SL_RES1 = 18;
static constexpr uint32_t SL_NORM2 = 19;
static constexpr uint32_t SL_FF1 = 20;
static constexpr uint32_t SL_RELU = 21;
static constexpr uint32_t SL_FF2 = 22;
static constexpr uint32_t SL_RES2 = 23;
static constexpr uint32_t SL_CLS = 24;
static constexpr uint32_t SL_LOGITS = 25;
static constexpr uint32_t SL_PROBS = 26;

static constexpr uint32_t N_SLOTS = 27;
[[maybe_unused]] static constexpr uint32_t N_EXT = 12;

static constexpr uint64_t SZ_BSD = B * S * D * 4;
static constexpr uint64_t SZ_D = D * 4;
static constexpr uint64_t SZ_DD = D * D * 4;
static constexpr uint64_t SZ_DFF = D * D_FF * 4;
static constexpr uint64_t SZ_FFD = D_FF * D * 4;
static constexpr uint64_t SZ_DNCLS = D * N_CLS * 4;
static constexpr uint64_t SZ_BSDFF = B * S * D_FF * 4;
static constexpr uint64_t SZ_BD = B * D * 4;
static constexpr uint64_t SZ_BNCLS = B * N_CLS * 4;

int main() {
    auto test = effects::testing::test();
    std::printf("test_compute_vit:\n");

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-0.3f, 0.3f);

    alignas(64) float X[B * S * D];
    alignas(64) float gamma1[D];
    alignas(64) float beta1[D];
    alignas(64) float W_q[D * D];
    alignas(64) float W_k[D * D];
    alignas(64) float W_v[D * D];
    alignas(64) float W_out[D * D];
    alignas(64) float gamma2[D];
    alignas(64) float beta2[D];
    alignas(64) float W_ff1[D * D_FF];
    alignas(64) float W_ff2[D_FF * D];
    alignas(64) float W_head[D * N_CLS];

    for (auto& v : X)
        v = dist(rng);
    for (auto& v : W_q)
        v = dist(rng);
    for (auto& v : W_k)
        v = dist(rng);
    for (auto& v : W_v)
        v = dist(rng);
    for (auto& v : W_out)
        v = dist(rng);
    for (auto& v : W_ff1)
        v = dist(rng);
    for (auto& v : W_ff2)
        v = dist(rng);
    for (auto& v : W_head)
        v = dist(rng);

    for (int i = 0; i < D; i++) {
        gamma1[i] = 1.0f + dist(rng) * 0.1f;
        beta1[i] = dist(rng) * 0.1f;
        gamma2[i] = 1.0f + dist(rng) * 0.1f;
        beta2[i] = dist(rng) * 0.1f;
    }

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

    using crucible::safety::NonNull;
    auto cv = ctx.mint_compiled_view();

    ctx.register_external(SlotId{SL_X}, NonNull<void*>{X}, cv);
    ctx.register_external(SlotId{SL_G1}, NonNull<void*>{gamma1}, cv);
    ctx.register_external(SlotId{SL_B1}, NonNull<void*>{beta1}, cv);
    ctx.register_external(SlotId{SL_WQ}, NonNull<void*>{W_q}, cv);
    ctx.register_external(SlotId{SL_WK}, NonNull<void*>{W_k}, cv);
    ctx.register_external(SlotId{SL_WV}, NonNull<void*>{W_v}, cv);
    ctx.register_external(SlotId{SL_WOUT}, NonNull<void*>{W_out}, cv);
    ctx.register_external(SlotId{SL_G2}, NonNull<void*>{gamma2}, cv);
    ctx.register_external(SlotId{SL_B2}, NonNull<void*>{beta2}, cv);
    ctx.register_external(SlotId{SL_WFF1}, NonNull<void*>{W_ff1}, cv);
    ctx.register_external(SlotId{SL_WFF2}, NonNull<void*>{W_ff2}, cv);
    ctx.register_external(SlotId{SL_WHEAD}, NonNull<void*>{W_head}, cv);

    for (int iter = 0; iter < 50; iter++) {
        ReplayStatus s;

        s = ctx.advance(H_LN1, S_LN1, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::layer_norm(
            static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
            static_cast<const float*>(ctx.input_ptr(2, cv)), static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D);

        s = ctx.advance(H_MMQ, S_MMQ, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D, D);

        s = ctx.advance(H_MMK, S_MMK, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D, D);

        s = ctx.advance(H_MMV, S_MMV, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D, D);

        s = ctx.advance(H_SDPA, S_SDPA, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::sdpa(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                  static_cast<const float*>(ctx.input_ptr(2, cv)), static_cast<float*>(ctx.output_ptr(0, cv)), B, S, D);

        s = ctx.advance(H_MMOUT, S_MMOUT, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D, D);

        s = ctx.advance(H_ADD1, S_ADD1, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::add(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                 static_cast<float*>(ctx.output_ptr(0, cv)), B * S * D);

        s = ctx.advance(H_LN2, S_LN2, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::layer_norm(
            static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
            static_cast<const float*>(ctx.input_ptr(2, cv)), static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D);

        s = ctx.advance(H_MMFF1, S_MMFF1, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D_FF, D);

        s = ctx.advance(H_RELU, S_RELU, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::relu(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<float*>(ctx.output_ptr(0, cv)),
                  B * S * D_FF);

        s = ctx.advance(H_MMFF2, S_MMFF2, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D, D_FF);

        s = ctx.advance(H_ADD2, S_ADD2, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::add(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                 static_cast<float*>(ctx.output_ptr(0, cv)), B * S * D);

        s = ctx.advance(H_IDXSEL, S_IDXSEL, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::index_select(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<float*>(ctx.output_ptr(0, cv)),
                          B, S, D, 0);

        s = ctx.advance(H_MMHEAD, S_MMHEAD, cv);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                static_cast<float*>(ctx.output_ptr(0, cv)), B, N_CLS, D);

        s = ctx.advance(H_SOFTMAX, S_SOFTMAX, cv);
        assert(s == ReplayStatus::COMPLETE);
        cpu::softmax(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<float*>(ctx.output_ptr(0, cv)), B,
                     N_CLS);
    }

    assert(ctx.compiled_iterations() == 50);
    assert(ctx.diverged_count() == 0);

    float ref_norm1[B * S * D]{};
    float ref_Q[B * S * D]{};
    float ref_K[B * S * D]{};
    float ref_V[B * S * D]{};
    float ref_attn[B * S * D]{};
    float ref_proj[B * S * D]{};
    float ref_res1[B * S * D]{};
    float ref_norm2[B * S * D]{};
    float ref_ff1[B * S * D_FF]{};
    float ref_relu[B * S * D_FF]{};
    float ref_ff2[B * S * D]{};
    float ref_res2[B * S * D]{};
    float ref_cls[B * D]{};
    float ref_logits[B * N_CLS]{};
    float ref_probs[B * N_CLS]{};

    cpu::layer_norm(X, gamma1, beta1, ref_norm1, B * S, D);
    cpu::mm(ref_norm1, W_q, ref_Q, B * S, D, D);
    cpu::mm(ref_norm1, W_k, ref_K, B * S, D, D);
    cpu::mm(ref_norm1, W_v, ref_V, B * S, D, D);
    cpu::sdpa(ref_Q, ref_K, ref_V, ref_attn, B, S, D);
    cpu::mm(ref_attn, W_out, ref_proj, B * S, D, D);
    cpu::add(ref_proj, X, ref_res1, B * S * D);
    cpu::layer_norm(ref_res1, gamma2, beta2, ref_norm2, B * S, D);
    cpu::mm(ref_norm2, W_ff1, ref_ff1, B * S, D_FF, D);
    cpu::relu(ref_ff1, ref_relu, B * S * D_FF);
    cpu::mm(ref_relu, W_ff2, ref_ff2, B * S, D, D_FF);
    cpu::add(ref_ff2, ref_res1, ref_res2, B * S * D);
    cpu::index_select(ref_res2, ref_cls, B, S, D, 0);
    cpu::mm(ref_cls, W_head, ref_logits, B, N_CLS, D);
    cpu::softmax(ref_logits, ref_probs, B, N_CLS);

    auto pv = ctx.pool().mint_initialized_view();
    auto* pool_probs = static_cast<const float*>(ctx.pool().slot_ptr(SlotId{SL_PROBS}, pv));

    float max_err = 0.0f;
    for (int i = 0; i < B * N_CLS; i++) {
        float err = std::abs(pool_probs[i] - ref_probs[i]);
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
        float err = std::abs(pool_res2[i] - ref_res2[i]);
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
