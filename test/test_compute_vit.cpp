// End-to-end compute: Single-layer ViT forward pass with real numbers
// through PoolAllocator.
//
// Model (1 transformer block + classification head):
//   X[B,S,D] → LayerNorm → Q,K,V projections → SDPA → output projection
//   → residual add → LayerNorm → FFN(mm+relu+mm) → residual add
//   → CLS token extract → linear head → softmax → Y[B,N_cls]
//
// 15 ops per iteration, 27 tensor slots (12 external params + 15 internal).
// Exercises complex dataflow: 3-input ops (layer_norm, sdpa), residual
// connections (slots consumed by non-adjacent ops), mixed tensor sizes
// (D vs D_ff), and data movement (index_select).
//
// The sweep-line allocator handles overlapping lifetimes: Q, K, V are
// all alive simultaneously during SDPA. After attention, freed slots
// are reused for FFN activations. Final softmax output verified against
// a direct CPU reference computation.

#include "cpu_kernels.h"

#include <crucible/BackgroundThread.h>
#include <crucible/CrucibleContext.h>
#include <crucible/Effects.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>

using namespace crucible;

// ─── Model dimensions ────────────────────────────────────────────────
static constexpr int B     = 2;   // batch size
static constexpr int S     = 4;   // sequence length (patches)
static constexpr int D     = 8;   // embedding dimension
static constexpr int D_FF  = 16;  // feedforward hidden dim (2×D)
static constexpr int N_CLS = 3;   // number of classes

static constexpr uint32_t N_OPS = 15;

// Schema hashes (unique per op type)
static constexpr SchemaHash H_LN1     {0x100};
static constexpr SchemaHash H_MMQ     {0x200};
static constexpr SchemaHash H_MMK     {0x300};
static constexpr SchemaHash H_MMV     {0x400};
static constexpr SchemaHash H_SDPA    {0x500};
static constexpr SchemaHash H_MMOUT   {0x600};
static constexpr SchemaHash H_ADD1    {0x700};
static constexpr SchemaHash H_LN2     {0x800};
static constexpr SchemaHash H_MMFF1   {0x900};
static constexpr SchemaHash H_RELU    {0xA00};
static constexpr SchemaHash H_MMFF2   {0xB00};
static constexpr SchemaHash H_ADD2    {0xC00};
static constexpr SchemaHash H_IDXSEL  {0xD00};
static constexpr SchemaHash H_MMHEAD  {0xE00};
static constexpr SchemaHash H_SOFTMAX {0xF00};

// Shape hashes (unique per op)
static constexpr ShapeHash S_LN1     {0x1100};
static constexpr ShapeHash S_MMQ     {0x1200};
static constexpr ShapeHash S_MMK     {0x1300};
static constexpr ShapeHash S_MMV     {0x1400};
static constexpr ShapeHash S_SDPA    {0x1500};
static constexpr ShapeHash S_MMOUT   {0x1600};
static constexpr ShapeHash S_ADD1    {0x1700};
static constexpr ShapeHash S_LN2     {0x1800};
static constexpr ShapeHash S_MMFF1   {0x1900};
static constexpr ShapeHash S_RELU    {0x1A00};
static constexpr ShapeHash S_MMFF2   {0x1B00};
static constexpr ShapeHash S_ADD2    {0x1C00};
static constexpr ShapeHash S_IDXSEL  {0x1D00};
static constexpr ShapeHash S_MMHEAD  {0x1E00};
static constexpr ShapeHash S_SOFTMAX {0x1F00};

// ─── Slot IDs ────────────────────────────────────────────────────────
// External: 12 parameter slots (0–11)
static constexpr uint32_t SL_X      = 0;   // input [B,S,D]
static constexpr uint32_t SL_G1     = 1;   // gamma1 [D]
static constexpr uint32_t SL_B1     = 2;   // beta1 [D]
static constexpr uint32_t SL_WQ     = 3;   // W_q [D,D]
static constexpr uint32_t SL_WK     = 4;   // W_k [D,D]
static constexpr uint32_t SL_WV     = 5;   // W_v [D,D]
static constexpr uint32_t SL_WOUT   = 6;   // W_out [D,D]
static constexpr uint32_t SL_G2     = 7;   // gamma2 [D]
static constexpr uint32_t SL_B2     = 8;   // beta2 [D]
static constexpr uint32_t SL_WFF1   = 9;   // W_ff1 [D,D_ff]
static constexpr uint32_t SL_WFF2   = 10;  // W_ff2 [D_ff,D]
static constexpr uint32_t SL_WHEAD  = 11;  // W_head [D,N_cls]

// Internal: 15 activation slots (12–26)
static constexpr uint32_t SL_NORM1  = 12;  // layernorm1 out [B,S,D]
static constexpr uint32_t SL_Q      = 13;  // Q projection [B,S,D]
static constexpr uint32_t SL_K      = 14;  // K projection [B,S,D]
static constexpr uint32_t SL_V      = 15;  // V projection [B,S,D]
static constexpr uint32_t SL_ATTN   = 16;  // SDPA output [B,S,D]
static constexpr uint32_t SL_PROJ   = 17;  // output projection [B,S,D]
static constexpr uint32_t SL_RES1   = 18;  // residual 1 [B,S,D]
static constexpr uint32_t SL_NORM2  = 19;  // layernorm2 out [B,S,D]
static constexpr uint32_t SL_FF1    = 20;  // FFN layer 1 [B,S,D_ff]
static constexpr uint32_t SL_RELU   = 21;  // ReLU output [B,S,D_ff]
static constexpr uint32_t SL_FF2    = 22;  // FFN layer 2 [B,S,D]
static constexpr uint32_t SL_RES2   = 23;  // residual 2 [B,S,D]
static constexpr uint32_t SL_CLS    = 24;  // CLS token [B,D]
static constexpr uint32_t SL_LOGITS = 25;  // logits [B,N_cls]
static constexpr uint32_t SL_PROBS  = 26;  // softmax probs [B,N_cls]

static constexpr uint32_t N_SLOTS = 27;
[[maybe_unused]] static constexpr uint32_t N_EXT = 12;

// Byte sizes for float tensors
static constexpr uint64_t SZ_BSD    = B * S * D * 4;       // 256
static constexpr uint64_t SZ_D      = D * 4;               // 32
static constexpr uint64_t SZ_DD     = D * D * 4;           // 256
static constexpr uint64_t SZ_DFF    = D * D_FF * 4;        // 512
static constexpr uint64_t SZ_FFD    = D_FF * D * 4;        // 512
static constexpr uint64_t SZ_DNCLS  = D * N_CLS * 4;       // 96
static constexpr uint64_t SZ_BSDFF  = B * S * D_FF * 4;    // 512
static constexpr uint64_t SZ_BD     = B * D * 4;           // 64
static constexpr uint64_t SZ_BNCLS  = B * N_CLS * 4;       // 24

int main() {
    fx::Test test;
    std::printf("test_compute_vit:\n");

    // ── Initialize parameters with seeded random values ──────────────
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-0.3f, 0.3f);

    // External parameter arrays
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

    for (auto& v : X)      v = dist(rng);
    for (auto& v : W_q)    v = dist(rng);
    for (auto& v : W_k)    v = dist(rng);
    for (auto& v : W_v)    v = dist(rng);
    for (auto& v : W_out)  v = dist(rng);
    for (auto& v : W_ff1)  v = dist(rng);
    for (auto& v : W_ff2)  v = dist(rng);
    for (auto& v : W_head) v = dist(rng);

    // Layer norm: gamma=1+noise, beta=noise (near-identity initialization)
    for (int i = 0; i < D; i++) {
        gamma1[i] = 1.0f + dist(rng) * 0.1f;
        beta1[i]  = dist(rng) * 0.1f;
        gamma2[i] = 1.0f + dist(rng) * 0.1f;
        beta2[i]  = dist(rng) * 0.1f;
    }

    // ── Build TensorSlots ────────────────────────────────────────────
    //
    // Liveness: birth = producing op, death = last consuming op.
    //
    // Key overlaps:
    //   ops 1-3: norm1_out alive while Q,K,V are produced
    //   op 4: Q,K,V all alive simultaneously for SDPA
    //   ops 6-11: resid1 alive across entire FFN block
    //   ops 8-10: ff1/relu slots alive during FFN computation
    TensorSlot slots[N_SLOTS]{};

    auto make_ext = [](uint32_t id, uint64_t sz) -> TensorSlot {
        return {.offset_bytes = 0, .nbytes = sz,
                .birth_op = OpIndex{0}, .death_op = OpIndex{N_OPS},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = true, .pad = {}, .slot_id = SlotId{id}, .pad2 = {}};
    };
    auto make_int = [](uint32_t id, uint64_t sz, uint32_t birth, uint32_t death) -> TensorSlot {
        return {.offset_bytes = 0, .nbytes = sz,
                .birth_op = OpIndex{birth}, .death_op = OpIndex{death},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = false, .pad = {}, .slot_id = SlotId{id}, .pad2 = {}};
    };

    // External parameters (alive entire iteration)
    slots[SL_X]     = make_ext(SL_X,     SZ_BSD);
    slots[SL_G1]    = make_ext(SL_G1,    SZ_D);
    slots[SL_B1]    = make_ext(SL_B1,    SZ_D);
    slots[SL_WQ]    = make_ext(SL_WQ,    SZ_DD);
    slots[SL_WK]    = make_ext(SL_WK,    SZ_DD);
    slots[SL_WV]    = make_ext(SL_WV,    SZ_DD);
    slots[SL_WOUT]  = make_ext(SL_WOUT,  SZ_DD);
    slots[SL_G2]    = make_ext(SL_G2,    SZ_D);
    slots[SL_B2]    = make_ext(SL_B2,    SZ_D);
    slots[SL_WFF1]  = make_ext(SL_WFF1,  SZ_DFF);
    slots[SL_WFF2]  = make_ext(SL_WFF2,  SZ_FFD);
    slots[SL_WHEAD] = make_ext(SL_WHEAD, SZ_DNCLS);

    // Internal activations (sweep-line assigns offsets)
    slots[SL_NORM1]  = make_int(SL_NORM1,  SZ_BSD,   0,  3);
    slots[SL_Q]      = make_int(SL_Q,      SZ_BSD,   1,  4);
    slots[SL_K]      = make_int(SL_K,      SZ_BSD,   2,  4);
    slots[SL_V]      = make_int(SL_V,      SZ_BSD,   3,  4);
    slots[SL_ATTN]   = make_int(SL_ATTN,   SZ_BSD,   4,  5);
    slots[SL_PROJ]   = make_int(SL_PROJ,   SZ_BSD,   5,  6);
    slots[SL_RES1]   = make_int(SL_RES1,   SZ_BSD,   6, 11);
    slots[SL_NORM2]  = make_int(SL_NORM2,  SZ_BSD,   7,  8);
    slots[SL_FF1]    = make_int(SL_FF1,    SZ_BSDFF, 8,  9);
    slots[SL_RELU]   = make_int(SL_RELU,   SZ_BSDFF, 9, 10);
    slots[SL_FF2]    = make_int(SL_FF2,    SZ_BSD,  10, 11);
    // death_op=14 (last op): keep alive so post-iteration read is valid.
    // RES2 is consumed at op 12, but we verify it after the iteration.
    slots[SL_RES2]   = make_int(SL_RES2,   SZ_BSD,  11, 14);
    slots[SL_CLS]    = make_int(SL_CLS,    SZ_BD,   12, 13);
    slots[SL_LOGITS] = make_int(SL_LOGITS, SZ_BNCLS,13, 14);
    slots[SL_PROBS]  = make_int(SL_PROBS,  SZ_BNCLS,14, 14);

    // Sweep-line offset assignment
    BackgroundThread bt;
    auto* plan = bt.compute_memory_plan(test.alloc, slots, N_SLOTS);
    assert(plan != nullptr);

    std::printf("  plan: pool=%lu bytes, %u slots (%u external)\n",
                static_cast<unsigned long>(plan->pool_bytes),
                plan->num_slots, plan->num_external);

    // ── Build TraceEntry ops ─────────────────────────────────────────
    // Input/output slot ID arrays for each op (stack-allocated).

    // Op 0: layer_norm(X, gamma1, beta1) → norm1_out
    SlotId op0_in[] = {SlotId{SL_X}, SlotId{SL_G1}, SlotId{SL_B1}};
    SlotId op0_out[] = {SlotId{SL_NORM1}};

    // Op 1: mm(norm1_out, W_q) → Q
    SlotId op1_in[] = {SlotId{SL_NORM1}, SlotId{SL_WQ}};
    SlotId op1_out[] = {SlotId{SL_Q}};

    // Op 2: mm(norm1_out, W_k) → K
    SlotId op2_in[] = {SlotId{SL_NORM1}, SlotId{SL_WK}};
    SlotId op2_out[] = {SlotId{SL_K}};

    // Op 3: mm(norm1_out, W_v) → V
    SlotId op3_in[] = {SlotId{SL_NORM1}, SlotId{SL_WV}};
    SlotId op3_out[] = {SlotId{SL_V}};

    // Op 4: sdpa(Q, K, V) → attn_out
    SlotId op4_in[] = {SlotId{SL_Q}, SlotId{SL_K}, SlotId{SL_V}};
    SlotId op4_out[] = {SlotId{SL_ATTN}};

    // Op 5: mm(attn_out, W_out) → proj_out
    SlotId op5_in[] = {SlotId{SL_ATTN}, SlotId{SL_WOUT}};
    SlotId op5_out[] = {SlotId{SL_PROJ}};

    // Op 6: add(proj_out, X) → resid1
    SlotId op6_in[] = {SlotId{SL_PROJ}, SlotId{SL_X}};
    SlotId op6_out[] = {SlotId{SL_RES1}};

    // Op 7: layer_norm(resid1, gamma2, beta2) → norm2_out
    SlotId op7_in[] = {SlotId{SL_RES1}, SlotId{SL_G2}, SlotId{SL_B2}};
    SlotId op7_out[] = {SlotId{SL_NORM2}};

    // Op 8: mm(norm2_out, W_ff1) → ff1_out
    SlotId op8_in[] = {SlotId{SL_NORM2}, SlotId{SL_WFF1}};
    SlotId op8_out[] = {SlotId{SL_FF1}};

    // Op 9: relu(ff1_out) → relu_out
    SlotId op9_in[] = {SlotId{SL_FF1}};
    SlotId op9_out[] = {SlotId{SL_RELU}};

    // Op 10: mm(relu_out, W_ff2) → ff2_out
    SlotId op10_in[] = {SlotId{SL_RELU}, SlotId{SL_WFF2}};
    SlotId op10_out[] = {SlotId{SL_FF2}};

    // Op 11: add(ff2_out, resid1) → resid2
    SlotId op11_in[] = {SlotId{SL_FF2}, SlotId{SL_RES1}};
    SlotId op11_out[] = {SlotId{SL_RES2}};

    // Op 12: index_select(resid2, idx=0) → cls
    SlotId op12_in[] = {SlotId{SL_RES2}};
    SlotId op12_out[] = {SlotId{SL_CLS}};

    // Op 13: mm(cls, W_head) → logits
    SlotId op13_in[] = {SlotId{SL_CLS}, SlotId{SL_WHEAD}};
    SlotId op13_out[] = {SlotId{SL_LOGITS}};

    // Op 14: softmax(logits) → probs
    SlotId op14_in[] = {SlotId{SL_LOGITS}};
    SlotId op14_out[] = {SlotId{SL_PROBS}};

    // Populate TraceEntry array
    struct OpDef {
        SchemaHash schema; ShapeHash shape;
        SlotId* in; uint16_t n_in;
        SlotId* out; uint16_t n_out;
    };
    OpDef defs[N_OPS] = {
        {.schema = H_LN1,     .shape = S_LN1,     .in = op0_in,  .n_in = 3, .out = op0_out,  .n_out = 1},
        {.schema = H_MMQ,     .shape = S_MMQ,     .in = op1_in,  .n_in = 2, .out = op1_out,  .n_out = 1},
        {.schema = H_MMK,     .shape = S_MMK,     .in = op2_in,  .n_in = 2, .out = op2_out,  .n_out = 1},
        {.schema = H_MMV,     .shape = S_MMV,     .in = op3_in,  .n_in = 2, .out = op3_out,  .n_out = 1},
        {.schema = H_SDPA,    .shape = S_SDPA,    .in = op4_in,  .n_in = 3, .out = op4_out,  .n_out = 1},
        {.schema = H_MMOUT,   .shape = S_MMOUT,   .in = op5_in,  .n_in = 2, .out = op5_out,  .n_out = 1},
        {.schema = H_ADD1,    .shape = S_ADD1,    .in = op6_in,  .n_in = 2, .out = op6_out,  .n_out = 1},
        {.schema = H_LN2,     .shape = S_LN2,     .in = op7_in,  .n_in = 3, .out = op7_out,  .n_out = 1},
        {.schema = H_MMFF1,   .shape = S_MMFF1,   .in = op8_in,  .n_in = 2, .out = op8_out,  .n_out = 1},
        {.schema = H_RELU,    .shape = S_RELU,    .in = op9_in,  .n_in = 1, .out = op9_out,  .n_out = 1},
        {.schema = H_MMFF2,   .shape = S_MMFF2,   .in = op10_in, .n_in = 2, .out = op10_out, .n_out = 1},
        {.schema = H_ADD2,    .shape = S_ADD2,    .in = op11_in, .n_in = 2, .out = op11_out, .n_out = 1},
        {.schema = H_IDXSEL,  .shape = S_IDXSEL,  .in = op12_in, .n_in = 1, .out = op12_out, .n_out = 1},
        {.schema = H_MMHEAD,  .shape = S_MMHEAD,  .in = op13_in, .n_in = 2, .out = op13_out, .n_out = 1},
        {.schema = H_SOFTMAX, .shape = S_SOFTMAX, .in = op14_in, .n_in = 1, .out = op14_out, .n_out = 1},
    };

    TraceEntry ops[N_OPS]{};
    for (uint32_t i = 0; i < N_OPS; i++) {
        ops[i].schema_hash     = defs[i].schema;
        ops[i].shape_hash      = defs[i].shape;
        ops[i].num_inputs      = defs[i].n_in;
        ops[i].num_outputs     = defs[i].n_out;
        ops[i].input_slot_ids  = defs[i].in;
        ops[i].output_slot_ids = defs[i].out;
    }

    // ── Build RegionNode and activate CrucibleContext ────────────────
    RegionNode region{};
    region.kind = TraceNodeKind::REGION;
    region.ops = ops;
    region.num_ops = N_OPS;
    region.plan = plan;

    CrucibleContext ctx;
    assert(ctx.activate(&region));
    assert(ctx.is_compiled());

    // Register all 12 external parameter pointers
    ctx.register_external(SlotId{SL_X},     X);
    ctx.register_external(SlotId{SL_G1},    gamma1);
    ctx.register_external(SlotId{SL_B1},    beta1);
    ctx.register_external(SlotId{SL_WQ},    W_q);
    ctx.register_external(SlotId{SL_WK},    W_k);
    ctx.register_external(SlotId{SL_WV},    W_v);
    ctx.register_external(SlotId{SL_WOUT},  W_out);
    ctx.register_external(SlotId{SL_G2},    gamma2);
    ctx.register_external(SlotId{SL_B2},    beta2);
    ctx.register_external(SlotId{SL_WFF1},  W_ff1);
    ctx.register_external(SlotId{SL_WFF2},  W_ff2);
    ctx.register_external(SlotId{SL_WHEAD}, W_head);

    // ── Run 50 compiled iterations ──────────────────────────────────
    for (int iter = 0; iter < 50; iter++) {
        ReplayStatus s;

        // Op 0: layer_norm(X, gamma1, beta1) → norm1_out
        s = ctx.advance(H_LN1, S_LN1);
        assert(s == ReplayStatus::MATCH);
        cpu::layer_norm(static_cast<const float*>(ctx.input_ptr(0)),
                        static_cast<const float*>(ctx.input_ptr(1)),
                        static_cast<const float*>(ctx.input_ptr(2)),
                        static_cast<float*>(ctx.output_ptr(0)),
                        B * S, D);

        // Op 1: mm(norm1_out, W_q) → Q
        s = ctx.advance(H_MMQ, S_MMQ);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0)),
                static_cast<const float*>(ctx.input_ptr(1)),
                static_cast<float*>(ctx.output_ptr(0)),
                B * S, D, D);

        // Op 2: mm(norm1_out, W_k) → K
        s = ctx.advance(H_MMK, S_MMK);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0)),
                static_cast<const float*>(ctx.input_ptr(1)),
                static_cast<float*>(ctx.output_ptr(0)),
                B * S, D, D);

        // Op 3: mm(norm1_out, W_v) → V
        s = ctx.advance(H_MMV, S_MMV);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0)),
                static_cast<const float*>(ctx.input_ptr(1)),
                static_cast<float*>(ctx.output_ptr(0)),
                B * S, D, D);

        // Op 4: sdpa(Q, K, V) → attn_out
        s = ctx.advance(H_SDPA, S_SDPA);
        assert(s == ReplayStatus::MATCH);
        cpu::sdpa(static_cast<const float*>(ctx.input_ptr(0)),
                  static_cast<const float*>(ctx.input_ptr(1)),
                  static_cast<const float*>(ctx.input_ptr(2)),
                  static_cast<float*>(ctx.output_ptr(0)),
                  B, S, D);

        // Op 5: mm(attn_out, W_out) → proj_out
        s = ctx.advance(H_MMOUT, S_MMOUT);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0)),
                static_cast<const float*>(ctx.input_ptr(1)),
                static_cast<float*>(ctx.output_ptr(0)),
                B * S, D, D);

        // Op 6: add(proj_out, X) → resid1
        s = ctx.advance(H_ADD1, S_ADD1);
        assert(s == ReplayStatus::MATCH);
        cpu::add(static_cast<const float*>(ctx.input_ptr(0)),
                 static_cast<const float*>(ctx.input_ptr(1)),
                 static_cast<float*>(ctx.output_ptr(0)),
                 B * S * D);

        // Op 7: layer_norm(resid1, gamma2, beta2) → norm2_out
        s = ctx.advance(H_LN2, S_LN2);
        assert(s == ReplayStatus::MATCH);
        cpu::layer_norm(static_cast<const float*>(ctx.input_ptr(0)),
                        static_cast<const float*>(ctx.input_ptr(1)),
                        static_cast<const float*>(ctx.input_ptr(2)),
                        static_cast<float*>(ctx.output_ptr(0)),
                        B * S, D);

        // Op 8: mm(norm2_out, W_ff1) → ff1_out
        s = ctx.advance(H_MMFF1, S_MMFF1);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0)),
                static_cast<const float*>(ctx.input_ptr(1)),
                static_cast<float*>(ctx.output_ptr(0)),
                B * S, D_FF, D);

        // Op 9: relu(ff1_out) → relu_out
        s = ctx.advance(H_RELU, S_RELU);
        assert(s == ReplayStatus::MATCH);
        cpu::relu(static_cast<const float*>(ctx.input_ptr(0)),
                  static_cast<float*>(ctx.output_ptr(0)),
                  B * S * D_FF);

        // Op 10: mm(relu_out, W_ff2) → ff2_out
        s = ctx.advance(H_MMFF2, S_MMFF2);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0)),
                static_cast<const float*>(ctx.input_ptr(1)),
                static_cast<float*>(ctx.output_ptr(0)),
                B * S, D, D_FF);

        // Op 11: add(ff2_out, resid1) → resid2
        s = ctx.advance(H_ADD2, S_ADD2);
        assert(s == ReplayStatus::MATCH);
        cpu::add(static_cast<const float*>(ctx.input_ptr(0)),
                 static_cast<const float*>(ctx.input_ptr(1)),
                 static_cast<float*>(ctx.output_ptr(0)),
                 B * S * D);

        // Op 12: index_select(resid2, idx=0) → cls [B,D]
        s = ctx.advance(H_IDXSEL, S_IDXSEL);
        assert(s == ReplayStatus::MATCH);
        cpu::index_select(static_cast<const float*>(ctx.input_ptr(0)),
                          static_cast<float*>(ctx.output_ptr(0)),
                          B, S, D, 0);

        // Op 13: mm(cls, W_head) → logits
        s = ctx.advance(H_MMHEAD, S_MMHEAD);
        assert(s == ReplayStatus::MATCH);
        cpu::mm(static_cast<const float*>(ctx.input_ptr(0)),
                static_cast<const float*>(ctx.input_ptr(1)),
                static_cast<float*>(ctx.output_ptr(0)),
                B, N_CLS, D);

        // Op 14: softmax(logits) → probs (LAST OP → COMPLETE)
        s = ctx.advance(H_SOFTMAX, S_SOFTMAX);
        assert(s == ReplayStatus::COMPLETE);
        cpu::softmax(static_cast<const float*>(ctx.input_ptr(0)),
                     static_cast<float*>(ctx.output_ptr(0)),
                     B, N_CLS);
    }

    assert(ctx.compiled_iterations() == 50);
    assert(ctx.diverged_count() == 0);

    // ── Verify against direct CPU reference ──────────────────────────
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
    cpu::mm(ref_norm1, W_q,  ref_Q,  B * S, D, D);
    cpu::mm(ref_norm1, W_k,  ref_K,  B * S, D, D);
    cpu::mm(ref_norm1, W_v,  ref_V,  B * S, D, D);
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

    // Compare final softmax output from pool vs reference
    auto* pool_probs = static_cast<const float*>(
        ctx.pool().slot_ptr(SlotId{SL_PROBS}));

    float max_err = 0.0f;
    for (int i = 0; i < B * N_CLS; i++) {
        float err = std::abs(pool_probs[i] - ref_probs[i]);
        max_err = std::max(max_err, err);
    }

    std::printf("  max_error vs reference: %.2e\n", static_cast<double>(max_err));
    assert(max_err < 1e-5f && "ViT output diverged from reference");

    // Verify softmax properties
    for (int b = 0; b < B; b++) {
        float row_sum = 0.0f;
        for (int j = 0; j < N_CLS; j++) {
            float v = pool_probs[b * N_CLS + j];
            assert(v >= 0.0f && v <= 1.0f);
            row_sum += v;
        }
        assert(std::abs(row_sum - 1.0f) < 1e-5f && "softmax row doesn't sum to 1");
    }

    // Also verify intermediate: resid2 (residual 2, the transformer block output)
    auto* pool_res2 = static_cast<const float*>(
        ctx.pool().slot_ptr(SlotId{SL_RES2}));
    float max_res2_err = 0.0f;
    for (int i = 0; i < B * S * D; i++) {
        float err = std::abs(pool_res2[i] - ref_res2[i]);
        max_res2_err = std::max(max_res2_err, err);
    }
    std::printf("  resid2 max_error: %.2e\n", static_cast<double>(max_res2_err));
    assert(max_res2_err < 1e-5f && "residual output diverged from reference");

    // Print outputs
    std::printf("  probs[0] = [%.4f, %.4f, %.4f]\n",
                static_cast<double>(pool_probs[0]),
                static_cast<double>(pool_probs[1]),
                static_cast<double>(pool_probs[2]));
    std::printf("  probs[1] = [%.4f, %.4f, %.4f]\n",
                static_cast<double>(pool_probs[3]),
                static_cast<double>(pool_probs[4]),
                static_cast<double>(pool_probs[5]));
    std::printf("  50 iterations, %u ops/iter, compiled_iters=%u\n",
                N_OPS, ctx.compiled_iterations());

    std::printf("test_compute_vit: PASSED\n");
    return 0;
}
