#pragma once

// The shared part of test_compute_vit: the dimensions, the hashes and slots
// of the fifteen ops, the weights, and the steps that each other source file
// of the test holds.  The plan, the replay loop and main are in
// test_compute_vit.cpp.

#include <crucible/CrucibleContext.h>

#include <cstdint>

namespace test_compute_vit {

using crucible::SchemaHash;
using crucible::ShapeHash;

// The dimensions are small so the reference pass stays cheap, but still
// differ from each other so that equal-sized slots cannot mask an offset
// assignment bug: D and D_FF differ, and [B,S,D], [B,D] and [B,N_CLS] are
// three distinct activation footprints.
inline constexpr int B = 2;
inline constexpr int S = 4;
inline constexpr int D = 8;
inline constexpr int D_FF = 16;
inline constexpr int N_CLS = 3;

inline constexpr uint32_t N_OPS = 15;

inline constexpr SchemaHash H_LN1{0x100};
inline constexpr SchemaHash H_MMQ{0x200};
inline constexpr SchemaHash H_MMK{0x300};
inline constexpr SchemaHash H_MMV{0x400};
inline constexpr SchemaHash H_SDPA{0x500};
inline constexpr SchemaHash H_MMOUT{0x600};
inline constexpr SchemaHash H_ADD1{0x700};
inline constexpr SchemaHash H_LN2{0x800};
inline constexpr SchemaHash H_MMFF1{0x900};
inline constexpr SchemaHash H_RELU{0xA00};
inline constexpr SchemaHash H_MMFF2{0xB00};
inline constexpr SchemaHash H_ADD2{0xC00};
inline constexpr SchemaHash H_IDXSEL{0xD00};
inline constexpr SchemaHash H_MMHEAD{0xE00};
inline constexpr SchemaHash H_SOFTMAX{0xF00};

inline constexpr ShapeHash S_LN1{0x1100};
inline constexpr ShapeHash S_MMQ{0x1200};
inline constexpr ShapeHash S_MMK{0x1300};
inline constexpr ShapeHash S_MMV{0x1400};
inline constexpr ShapeHash S_SDPA{0x1500};
inline constexpr ShapeHash S_MMOUT{0x1600};
inline constexpr ShapeHash S_ADD1{0x1700};
inline constexpr ShapeHash S_LN2{0x1800};
inline constexpr ShapeHash S_MMFF1{0x1900};
inline constexpr ShapeHash S_RELU{0x1A00};
inline constexpr ShapeHash S_MMFF2{0x1B00};
inline constexpr ShapeHash S_ADD2{0x1C00};
inline constexpr ShapeHash S_IDXSEL{0x1D00};
inline constexpr ShapeHash S_MMHEAD{0x1E00};
inline constexpr ShapeHash S_SOFTMAX{0x1F00};

inline constexpr uint32_t SL_X = 0;
inline constexpr uint32_t SL_G1 = 1;
inline constexpr uint32_t SL_B1 = 2;
inline constexpr uint32_t SL_WQ = 3;
inline constexpr uint32_t SL_WK = 4;
inline constexpr uint32_t SL_WV = 5;
inline constexpr uint32_t SL_WOUT = 6;
inline constexpr uint32_t SL_G2 = 7;
inline constexpr uint32_t SL_B2 = 8;
inline constexpr uint32_t SL_WFF1 = 9;
inline constexpr uint32_t SL_WFF2 = 10;
inline constexpr uint32_t SL_WHEAD = 11;

inline constexpr uint32_t SL_NORM1 = 12;
inline constexpr uint32_t SL_Q = 13;
inline constexpr uint32_t SL_K = 14;
inline constexpr uint32_t SL_V = 15;
inline constexpr uint32_t SL_ATTN = 16;
inline constexpr uint32_t SL_PROJ = 17;
inline constexpr uint32_t SL_RES1 = 18;
inline constexpr uint32_t SL_NORM2 = 19;
inline constexpr uint32_t SL_FF1 = 20;
inline constexpr uint32_t SL_RELU = 21;
inline constexpr uint32_t SL_FF2 = 22;
inline constexpr uint32_t SL_RES2 = 23;
inline constexpr uint32_t SL_CLS = 24;
inline constexpr uint32_t SL_LOGITS = 25;
inline constexpr uint32_t SL_PROBS = 26;

inline constexpr uint32_t N_SLOTS = 27;
[[maybe_unused]] inline constexpr uint32_t N_EXT = 12;

inline constexpr uint64_t SZ_BSD = B * S * D * 4;
inline constexpr uint64_t SZ_D = D * 4;
inline constexpr uint64_t SZ_DD = D * D * 4;
inline constexpr uint64_t SZ_DFF = D * D_FF * 4;
inline constexpr uint64_t SZ_FFD = D_FF * D * 4;
inline constexpr uint64_t SZ_DNCLS = D * N_CLS * 4;
inline constexpr uint64_t SZ_BSDFF = B * S * D_FF * 4;
inline constexpr uint64_t SZ_BD = B * D * 4;
inline constexpr uint64_t SZ_BNCLS = B * N_CLS * 4;

// The input and the parameters of the block.
struct Weights {
    alignas(64) float X[B * S * D]{};
    alignas(64) float gamma1[D]{};
    alignas(64) float beta1[D]{};
    alignas(64) float W_q[D * D]{};
    alignas(64) float W_k[D * D]{};
    alignas(64) float W_v[D * D]{};
    alignas(64) float W_out[D * D]{};
    alignas(64) float gamma2[D]{};
    alignas(64) float beta2[D]{};
    alignas(64) float W_ff1[D * D_FF]{};
    alignas(64) float W_ff2[D_FF * D]{};
    alignas(64) float W_head[D * N_CLS]{};
};

// The two outputs of the direct CPU reference pass that the test compares
// with the pool.
struct Reference {
    float res2[B * S * D]{};
    float probs[B * N_CLS]{};
};

// The fixed-seed weights and the direct CPU reference pass
// (test_compute_vit_reference.cpp).
void fill_weights(Weights& weights);
void compute_reference(const Weights& weights, Reference& reference);

// One compiled iteration is these two steps in sequence.  The first does
// ops 0 to 6, the attention block (test_compute_vit_attention.cpp).  The
// second does ops 7 to 14, the feed-forward block and the head
// (test_compute_vit_head.cpp).
void run_attention_ops(crucible::CrucibleContext& ctx, const crucible::CrucibleContext::CompiledView& cv);
void run_ffn_and_head_ops(crucible::CrucibleContext& ctx, const crucible::CrucibleContext::CompiledView& cv);

}  // namespace test_compute_vit
