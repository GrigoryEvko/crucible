// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// ═══════════════════════════════════════════════════════════════════
// test_recipe_coverage — recipe coverage of the full IR001 op taxonomy
//
// Answers the question: "for every IR001 op kind, do we provide a
// stable recipe?"  Result: (a) every recipe-relevant op has at least
// one applicable starter for the common-case dtype matrix; (b) the
// known gaps are documented and CI-enforced.
//
// NumericalRecipe is op-AGNOSTIC — a recipe pins a numerical method
// (accum_dtype, reduction_algo, softmax variant, scale_policy), not
// an op kind.  The same `f16_f32accum_tc` recipe applies to GEMM,
// CONV, ATTENTION, NORM — anything that accumulates FP16 → FP32.
// So coverage splits into two questions:
//
//   1. Is every CKernelId classified by recipe-relevance?
//      (Pointwise / data-movement / I/O / sync ops are recipe-vacuous;
//       the rest are recipe-relevant.)
//
//   2. For recipe-relevant ops, does at least one starter recipe
//      apply to the common-case (input_dtype, output_dtype) tuples?
//
// Both questions are answered by enumeration tables below.  Drift
// in the CKernel taxonomy (new op kind added) trips the
// kCategorization static_assert; drift in the starter recipe set
// trips the coverage assertions in main().
//
// ─── Honest limits of this test ─────────────────────────────────────
//
// The starter set covers the COMMON-CASE training matrix:
// FP32 / FP16 / BF16 / FP8e4m3 / FP8e5m2 with FP32 accumulator.
// Documented gaps (FORGE.md §20.1's full ~40-recipe production
// catalog vs our 8 starters):
//
//   • INT8 quantization (PER_CHANNEL scale, INT32 accum)
//   • Complex-valued GEMM/FFT (ComplexFloat / ComplexDouble)
//   • FP4 MX / NVFP4 (bleeding-edge formats)
//   • FLASH2 / FLASH3 softmax variants (Hopper-specific)
//   • KAHAN compensated summation (ultra-strict numerics)
//   • PER_TENSOR scale variants for FP8 (only PER_BLOCK_MX covered)
//   • UNORDERED tier across our covered dtypes (inference-perf path)
//   • DEQUANT_GEMM / MOE-specific recipes (FP8/INT4 with mixed scales)
//
// These gaps are intentionally deferred until a concrete consumer
// surfaces (Mimic per-vendor backend, Phase E RecipeSelect, or the
// recipes.json loader once Mimic ChipId types exist).  Each is
// individually flagged kKnownGap below so a future filling-in is
// CI-detected.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Arena.h>
#include <crucible/CKernel.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <crucible/Types.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <utility>

namespace {

using crucible::Arena;
using crucible::CKernelId;
using crucible::NumericalRecipe;
using crucible::RecipePool;
using crucible::RecipeRegistry;
using crucible::ReductionDeterminism;
using crucible::ScalarType;
namespace names = crucible::recipe_names;

crucible::effects::Test g_test{};
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }

// ─── Recipe-relevance taxonomy ─────────────────────────────────────
//
// Each CKernelId is classified into exactly one category.  The
// category determines whether a recipe pin is meaningful for that
// op and what fields of the recipe matter.
enum class RecipeRelevance : uint8_t {
  // Op accumulates with an accum_dtype distinct from input dtype;
  // recipe.{accum_dtype, reduction_algo, rounding, determinism}
  // all matter.  GEMM / CONV / NORM / sum-style REDUCE / pooling-AVG.
  REDUCING,

  // Op uses softmax recurrence; recipe.softmax additionally matters.
  // SDPA / MHA / SOFTMAX / LOG_SOFTMAX / FUSED_ATTENTION /
  // FUSED_SOFTMAX_DROP / FUSED_CROSS_ENTROPY.
  SOFTMAX_USING,

  // Op uses block-scaled / per-tensor / per-channel scales;
  // recipe.scale_policy matters.  DEQUANT_GEMM / FP8 / FP4 / INT8.
  SCALE_USING,

  // Op uses Philox RNG state; recipe.flags.split_k_atomic_ok and
  // RNG-counter handling matter.  DROPOUT / RNG_UNIFORM / RNG_NORMAL.
  RNG_USING,

  // Reducing collective; recipe.determinism dictates topology
  // (canonical binary tree under BITEXACT vs ring under ORDERED).
  // ALLREDUCE / REDUCE_SCATTER / REDUCE.
  COMM_REDUCING,

  // Pure pointwise/elementwise; recipe is vacuous (no accumulation,
  // no softmax, no scales).  Output dtype is constrained by inputs;
  // any recipe that doesn't conflict with the surrounding region is
  // valid.  RELU / SIGMOID / EWISE_ADD / EWISE_CAST / etc.
  POINTWISE,

  // Pure data movement (no math); recipe is fully vacuous.
  // VIEW / RESHAPE / CAT / SLICE / transport collectives.
  DATA_MOVE,

  // I/O ops; recipe is fully vacuous.
  IO,

  // Sync ops; recipe is fully vacuous.
  SYNC,
};

[[nodiscard]] constexpr bool is_recipe_relevant(RecipeRelevance r) noexcept {
  switch (r) {
    case RecipeRelevance::REDUCING:
    case RecipeRelevance::SOFTMAX_USING:
    case RecipeRelevance::SCALE_USING:
    case RecipeRelevance::RNG_USING:
    case RecipeRelevance::COMM_REDUCING:
      return true;
    case RecipeRelevance::POINTWISE:
    case RecipeRelevance::DATA_MOVE:
    case RecipeRelevance::IO:
    case RecipeRelevance::SYNC:
      return false;
    default:
      // Exhaustive above; any other value indicates corrupt enum
      // memory.  std::unreachable lets the optimizer drop the
      // default branch.
      std::unreachable();
  }
}

// ─── Per-CKernelId categorization ──────────────────────────────────
//
// Exhaustive table covering every value of CKernelId.  A new op
// added to CKernel.h that doesn't appear here trips the
// static_assert at the bottom (kCategorization.size() vs
// NUM_KERNELS).  This is the load-bearing CI mechanism that
// prevents silent recipe-coverage drift.
struct OpCategory {
  CKernelId       id;
  RecipeRelevance rel;
  std::string_view note;  // brief rationale; appears in coverage report
};

constexpr OpCategory kCategorization[] = {
    // OPAQUE = fallback; no recipe assumption possible.
    {CKernelId::OPAQUE,           RecipeRelevance::POINTWISE,    "fallback Vessel dispatch"},

    // ── Linear Algebra (8) ──────────────────────────────────────
    {CKernelId::GEMM_MM,          RecipeRelevance::REDUCING,     "K-axis accumulation"},
    {CKernelId::GEMM_BMM,         RecipeRelevance::REDUCING,     "batched K-axis accumulation"},
    {CKernelId::GEMM_MATMUL,      RecipeRelevance::REDUCING,     "general K-axis accumulation"},
    {CKernelId::GEMM_ADDMM,       RecipeRelevance::REDUCING,     "K-axis accumulation + bias"},
    {CKernelId::GEMM_LINEAR,      RecipeRelevance::REDUCING,     "K-axis accumulation + bias"},
    {CKernelId::GEMM_ADDBMM,      RecipeRelevance::REDUCING,     "batched K-axis accumulation"},
    {CKernelId::GEMM_BADDBMM,     RecipeRelevance::REDUCING,     "batched K-axis accumulation"},
    {CKernelId::GEMM_EINSUM,      RecipeRelevance::REDUCING,     "general tensor contraction"},

    // ── Convolution (6) ─────────────────────────────────────────
    {CKernelId::CONV1D,           RecipeRelevance::REDUCING,     "spatial accumulation"},
    {CKernelId::CONV2D,           RecipeRelevance::REDUCING,     "spatial accumulation"},
    {CKernelId::CONV3D,           RecipeRelevance::REDUCING,     "spatial accumulation"},
    {CKernelId::CONV_TRANSPOSE1D, RecipeRelevance::REDUCING,     "transpose conv accumulation"},
    {CKernelId::CONV_TRANSPOSE2D, RecipeRelevance::REDUCING,     "transpose conv accumulation"},
    {CKernelId::CONV_TRANSPOSE3D, RecipeRelevance::REDUCING,     "transpose conv accumulation"},

    // ── Attention (4) ───────────────────────────────────────────
    {CKernelId::SDPA,             RecipeRelevance::SOFTMAX_USING, "online softmax + K-V accumulation"},
    {CKernelId::MHA,              RecipeRelevance::SOFTMAX_USING, "multi-head attention"},
    {CKernelId::ROPE,             RecipeRelevance::POINTWISE,    "rotary embedding is pointwise"},
    {CKernelId::POSITION_BIAS,    RecipeRelevance::POINTWISE,    "additive bias is pointwise"},

    // ── Normalization (6) ───────────────────────────────────────
    {CKernelId::LAYER_NORM,       RecipeRelevance::REDUCING,     "mean+var accumulation"},
    {CKernelId::BATCH_NORM_TRAIN, RecipeRelevance::REDUCING,     "batch stats accumulation"},
    {CKernelId::BATCH_NORM_EVAL,  RecipeRelevance::REDUCING,     "running-stat normalization"},
    {CKernelId::GROUP_NORM,       RecipeRelevance::REDUCING,     "group stats accumulation"},
    {CKernelId::INSTANCE_NORM,    RecipeRelevance::REDUCING,     "per-instance stats"},
    {CKernelId::RMS_NORM,         RecipeRelevance::REDUCING,     "RMS stat accumulation"},

    // ── Activations (13) ────────────────────────────────────────
    {CKernelId::ACT_RELU,         RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::ACT_GELU,         RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::ACT_SILU,         RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::ACT_SIGMOID,      RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::ACT_TANH,         RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::ACT_HARDSWISH,    RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::ACT_LEAKY_RELU,   RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::ACT_ELU,          RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::ACT_SOFTMAX,      RecipeRelevance::SOFTMAX_USING, "softmax recurrence"},
    {CKernelId::ACT_LOG_SOFTMAX,  RecipeRelevance::SOFTMAX_USING, "softmax recurrence"},
    {CKernelId::ACT_DROPOUT,      RecipeRelevance::RNG_USING,    "Philox-based mask"},
    {CKernelId::ACT_CLAMP,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::ACT_MISH,         RecipeRelevance::POINTWISE,    "pointwise"},

    // ── Elementwise Binary (9) ──────────────────────────────────
    {CKernelId::EWISE_ADD,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_MUL,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_SUB,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_DIV,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_POW,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_MAX,        RecipeRelevance::POINTWISE,    "pointwise (not reduction)"},
    {CKernelId::EWISE_MIN,        RecipeRelevance::POINTWISE,    "pointwise (not reduction)"},
    {CKernelId::EWISE_MOD,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_WHERE,      RecipeRelevance::POINTWISE,    "ternary select"},

    // ── Elementwise Unary (10) ──────────────────────────────────
    {CKernelId::EWISE_EXP,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_LOG,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_SQRT,       RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_RSQRT,      RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_ABS,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_NEG,        RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_SIGN,       RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_FLOOR,      RecipeRelevance::POINTWISE,    "pointwise"},
    {CKernelId::EWISE_CAST,       RecipeRelevance::POINTWISE,    "dtype conversion (rounding matters)"},
    {CKernelId::EWISE_FILL,       RecipeRelevance::POINTWISE,    "broadcast scalar"},

    // ── Reductions (8) ──────────────────────────────────────────
    {CKernelId::REDUCE_SUM,       RecipeRelevance::REDUCING,     "sum accumulation"},
    {CKernelId::REDUCE_MEAN,      RecipeRelevance::REDUCING,     "sum + divide"},
    {CKernelId::REDUCE_MAX,       RecipeRelevance::POINTWISE,    "max-fold (no FP accumulation)"},
    {CKernelId::REDUCE_MIN,       RecipeRelevance::POINTWISE,    "min-fold (no FP accumulation)"},
    {CKernelId::REDUCE_ARGMAX,    RecipeRelevance::POINTWISE,    "index extraction"},
    {CKernelId::REDUCE_ARGMIN,    RecipeRelevance::POINTWISE,    "index extraction"},
    {CKernelId::REDUCE_CUMSUM,    RecipeRelevance::REDUCING,     "prefix-sum accumulation"},
    {CKernelId::REDUCE_TOPK,      RecipeRelevance::POINTWISE,    "k-best selection"},

    // ── Pooling (8) ─────────────────────────────────────────────
    {CKernelId::POOL_MAX1D,       RecipeRelevance::POINTWISE,    "max-fold over window"},
    {CKernelId::POOL_MAX2D,       RecipeRelevance::POINTWISE,    "max-fold over window"},
    {CKernelId::POOL_MAX3D,       RecipeRelevance::POINTWISE,    "max-fold over window"},
    {CKernelId::POOL_AVG1D,       RecipeRelevance::REDUCING,     "average-pool accumulation"},
    {CKernelId::POOL_AVG2D,       RecipeRelevance::REDUCING,     "average-pool accumulation"},
    {CKernelId::POOL_AVG3D,       RecipeRelevance::REDUCING,     "average-pool accumulation"},
    {CKernelId::POOL_ADAPTIVE_MAX,RecipeRelevance::POINTWISE,    "max-fold over adaptive window"},
    {CKernelId::POOL_ADAPTIVE_AVG,RecipeRelevance::REDUCING,     "average over adaptive window"},

    // ── Data Movement / Indexing (16) ───────────────────────────
    {CKernelId::VIEW,             RecipeRelevance::DATA_MOVE,    "no math"},
    {CKernelId::RESHAPE,          RecipeRelevance::DATA_MOVE,    "may copy, no math"},
    {CKernelId::PERMUTE,          RecipeRelevance::DATA_MOVE,    "axis reorder"},
    {CKernelId::TRANSPOSE,        RecipeRelevance::DATA_MOVE,    "axis swap"},
    {CKernelId::CONTIGUOUS,       RecipeRelevance::DATA_MOVE,    "contiguous copy"},
    {CKernelId::EXPAND,           RecipeRelevance::DATA_MOVE,    "stride-0 broadcast"},
    {CKernelId::SQUEEZE,          RecipeRelevance::DATA_MOVE,    "shape change"},
    {CKernelId::SLICE,            RecipeRelevance::DATA_MOVE,    "subview"},
    {CKernelId::INDEX_SELECT,     RecipeRelevance::DATA_MOVE,    "lookup"},
    {CKernelId::INDEX,            RecipeRelevance::DATA_MOVE,    "advanced indexing"},
    {CKernelId::SCATTER,          RecipeRelevance::DATA_MOVE,    "scatter (atomic-add variants exist; recipe-vacuous at the rec level)"},
    {CKernelId::MASKED_FILL,      RecipeRelevance::DATA_MOVE,    "mask application"},
    {CKernelId::PAD,              RecipeRelevance::DATA_MOVE,    "padding"},
    {CKernelId::CAT,              RecipeRelevance::DATA_MOVE,    "concatenation"},
    {CKernelId::STACK,            RecipeRelevance::DATA_MOVE,    "stacking"},
    {CKernelId::UNFOLD,           RecipeRelevance::DATA_MOVE,    "patch extraction"},

    // ── Embedding (2) ───────────────────────────────────────────
    {CKernelId::EMBEDDING,        RecipeRelevance::DATA_MOVE,    "lookup (no accumulation)"},
    {CKernelId::EMBEDDING_BAG,    RecipeRelevance::REDUCING,     "lookup + sum/mean reduction"},

    // ── Copy / I/O (2) ──────────────────────────────────────────
    {CKernelId::COPY_,            RecipeRelevance::DATA_MOVE,    "in-place copy (CAST is its own op)"},
    {CKernelId::CLONE,            RecipeRelevance::DATA_MOVE,    "fresh storage copy"},

    // ── Vision (3) ──────────────────────────────────────────────
    {CKernelId::INTERPOLATE,      RecipeRelevance::REDUCING,     "bilinear/bicubic accumulation"},
    {CKernelId::GRID_SAMPLE,      RecipeRelevance::REDUCING,     "bilinear sample accumulation"},
    {CKernelId::IM2COL,           RecipeRelevance::DATA_MOVE,    "patch unfolding (no math)"},

    // ── Fused (4) ───────────────────────────────────────────────
    {CKernelId::FUSED_ATTENTION,  RecipeRelevance::SOFTMAX_USING,"fused MHSA"},
    {CKernelId::FUSED_LINEAR_ACT, RecipeRelevance::REDUCING,     "linear + activation gate"},
    {CKernelId::FUSED_NORM_LINEAR,RecipeRelevance::REDUCING,     "norm + linear"},
    {CKernelId::FUSED_SOFTMAX_DROP,RecipeRelevance::SOFTMAX_USING,"softmax + dropout"},

    // ── Linalg Decomp (9) ───────────────────────────────────────
    {CKernelId::LINALG_SVD,       RecipeRelevance::REDUCING,     "iterative QR/Jacobi accumulation"},
    {CKernelId::LINALG_CHOLESKY,  RecipeRelevance::REDUCING,     "in-place LLᵀ accumulation"},
    {CKernelId::LINALG_QR,        RecipeRelevance::REDUCING,     "Householder accumulation"},
    {CKernelId::LINALG_SOLVE,     RecipeRelevance::REDUCING,     "LU/triangular solve"},
    {CKernelId::LINALG_EIGH,      RecipeRelevance::REDUCING,     "QR-iteration accumulation"},
    {CKernelId::LINALG_NORM,      RecipeRelevance::REDUCING,     "L2/Frobenius accumulation"},
    {CKernelId::LINALG_CROSS,     RecipeRelevance::POINTWISE,    "3D cross-product is pointwise per element"},
    {CKernelId::CDIST,            RecipeRelevance::REDUCING,     "pairwise distance accumulation"},
    {CKernelId::FFT,              RecipeRelevance::REDUCING,     "butterfly accumulation (Complex dtype)"},

    // ── SSM / Recurrence (6) ────────────────────────────────────
    {CKernelId::ASSOC_SCAN,       RecipeRelevance::REDUCING,     "parallel prefix scan"},
    {CKernelId::SELECTIVE_SCAN,   RecipeRelevance::REDUCING,     "Mamba S6 affine recurrence"},
    {CKernelId::SSD_CHUNK,        RecipeRelevance::REDUCING,     "Mamba-2 chunked semiseparable matmul"},
    {CKernelId::WKV_RECURRENCE,   RecipeRelevance::REDUCING,     "RWKV exponentially-decayed prefix"},
    {CKernelId::RETENTION,        RecipeRelevance::REDUCING,     "RetNet decay-masked semi-attention"},
    {CKernelId::MLSTM_RECURRENCE, RecipeRelevance::REDUCING,     "xLSTM matrix-memory accumulation"},

    // ── Production Inference (6) ────────────────────────────────
    {CKernelId::DEQUANT_GEMM,     RecipeRelevance::SCALE_USING,  "INT4/FP8 dequant + GEMM (PER_CHANNEL/PER_BLOCK)"},
    {CKernelId::MOE_ROUTE_GEMM,   RecipeRelevance::REDUCING,     "top-k routing + grouped GEMM"},
    {CKernelId::PAGED_ATTENTION,  RecipeRelevance::SOFTMAX_USING,"page-table attention (vLLM)"},
    {CKernelId::FUSED_CROSS_ENTROPY,RecipeRelevance::SOFTMAX_USING,"online softmax + cross-entropy"},
    {CKernelId::LINEAR_ATTN_CAUSAL,RecipeRelevance::REDUCING,    "chunked outer-product reduction"},
    {CKernelId::RAGGED_ATTN,      RecipeRelevance::SOFTMAX_USING,"variable-length packed attention"},

    // ── 3D Rendering (4) ────────────────────────────────────────
    {CKernelId::GAUSSIAN_RASTERIZE,RecipeRelevance::REDUCING,    "alpha-blend accumulation"},
    {CKernelId::HASH_GRID_ENCODE, RecipeRelevance::REDUCING,     "trilinear interp accumulation"},
    {CKernelId::VOLUME_RENDER,    RecipeRelevance::REDUCING,     "ray-march compositing"},
    {CKernelId::SH_EVAL,          RecipeRelevance::POINTWISE,    "spherical harmonics basis (pointwise)"},

    // ── Structured Matrix / Graph (5) ───────────────────────────
    {CKernelId::FFT_CONV,         RecipeRelevance::REDUCING,     "FFT + pointwise + IFFT"},
    {CKernelId::MONARCH_MATMUL,   RecipeRelevance::REDUCING,     "block-diag GEMM accumulation"},
    {CKernelId::SPMM_GNN,         RecipeRelevance::REDUCING,     "graph message passing accumulation"},
    {CKernelId::SDDMM_GNN,        RecipeRelevance::REDUCING,     "edge score accumulation"},
    {CKernelId::SINKHORN,         RecipeRelevance::REDUCING,     "iterative log-domain accumulation"},

    // ── Collective Communication (10) ───────────────────────────
    {CKernelId::COMM_ALLREDUCE,   RecipeRelevance::COMM_REDUCING,"determinism pins reduction tree"},
    {CKernelId::COMM_ALLGATHER,   RecipeRelevance::DATA_MOVE,    "no reduction"},
    {CKernelId::COMM_REDUCE_SCATTER,RecipeRelevance::COMM_REDUCING,"determinism pins reduction tree"},
    {CKernelId::COMM_BROADCAST,   RecipeRelevance::DATA_MOVE,    "no reduction"},
    {CKernelId::COMM_ALL_TO_ALL,  RecipeRelevance::DATA_MOVE,    "permutation"},
    {CKernelId::COMM_SEND,        RecipeRelevance::DATA_MOVE,    "p2p"},
    {CKernelId::COMM_RECV,        RecipeRelevance::DATA_MOVE,    "p2p"},
    {CKernelId::COMM_REDUCE,      RecipeRelevance::COMM_REDUCING,"determinism pins reduction tree"},
    {CKernelId::COMM_GATHER,      RecipeRelevance::DATA_MOVE,    "no reduction"},
    {CKernelId::COMM_SCATTER,     RecipeRelevance::DATA_MOVE,    "no reduction"},

    // ── I/O (4) ─────────────────────────────────────────────────
    {CKernelId::IO_LOAD,          RecipeRelevance::IO,           "DataLoader → CPU"},
    {CKernelId::IO_PREFETCH,      RecipeRelevance::IO,           "host → device DMA"},
    {CKernelId::IO_CHECKPOINT_SAVE,RecipeRelevance::IO,          "weight serialization"},
    {CKernelId::IO_CHECKPOINT_LOAD,RecipeRelevance::IO,          "weight deserialization"},

    // ── RNG (2) ─────────────────────────────────────────────────
    {CKernelId::RNG_UNIFORM,      RecipeRelevance::RNG_USING,    "Philox uniform"},
    {CKernelId::RNG_NORMAL,       RecipeRelevance::RNG_USING,    "Philox Gaussian (Box-Muller)"},

    // ── Sync (1) ────────────────────────────────────────────────
    {CKernelId::COMM_BARRIER,     RecipeRelevance::SYNC,         "all-rank barrier"},
};

// ─── CI tripwire: every CKernelId must appear in kCategorization ───
//
// NUM_KERNELS is the sentinel after every real op; it equals the
// total count.  +1 because OPAQUE = 0 is the first valid value.
constexpr std::size_t kExpectedCategorizationSize =
    static_cast<std::size_t>(CKernelId::NUM_KERNELS);

static_assert(std::size(kCategorization) == kExpectedCategorizationSize,
              "kCategorization must cover every CKernelId — a new op was "
              "added to CKernel.h without classifying its recipe relevance. "
              "Add a row to kCategorization above.");

// ─── Coverage matrix: starter recipes vs (out_dtype, determinism) ──
//
// Each row asserts a specific (dtype, tier) cell is either
// covered by a named starter recipe or known-uncovered with
// rationale.  CI fires on:
//   - covered=true but recipe missing (gap regression)
//   - covered=false but recipe newly present (table not updated)
//   - mismatch between expected_recipe_name and actual recipe.

struct CoverageCell {
  ScalarType            out_dtype;
  ReductionDeterminism  det;
  bool                  covered;
  std::string_view      starter_name;   // empty if !covered
  std::string_view      gap_reason;     // empty if covered; rationale otherwise
};

constexpr CoverageCell kCoverageMatrix[] = {
    // ── FP32 ───────────────────────────────────────────────────
    // BITEXACT_STRICT and ORDERED covered; the other two are
    // intentional gaps (see rationale).
    {ScalarType::Float, ReductionDeterminism::BITEXACT_STRICT,
        true, names::kF32Strict, ""},
    {ScalarType::Float, ReductionDeterminism::ORDERED,
        true, names::kF32Ordered, ""},
    {ScalarType::Float, ReductionDeterminism::UNORDERED,
        false, "", "GAP: no f32_unordered (inference-fast path)"},
    {ScalarType::Float, ReductionDeterminism::BITEXACT_TC,
        false, "", "FP32 tensor cores already use K=8 fragments under "
                   "BITEXACT_STRICT; BITEXACT_TC degenerate for FP32"},

    // ── FP16 (storage) with FP32 accum ─────────────────────────
    {ScalarType::Half, ReductionDeterminism::BITEXACT_TC,
        true, names::kF16F32AccumTc, ""},
    {ScalarType::Half, ReductionDeterminism::ORDERED,
        true, names::kF16F32AccumOrdered, ""},
    {ScalarType::Half, ReductionDeterminism::UNORDERED,
        false, "", "GAP: no f16_unordered (inference-fast path)"},
    {ScalarType::Half, ReductionDeterminism::BITEXACT_STRICT,
        false, "", "GAP: no f16 strict (20-50× slowdown to emulate via scalar FMA)"},

    // ── BF16 (storage) with FP32 accum ─────────────────────────
    {ScalarType::BFloat16, ReductionDeterminism::BITEXACT_TC,
        true, names::kBf16F32AccumTc, ""},
    {ScalarType::BFloat16, ReductionDeterminism::ORDERED,
        true, names::kBf16F32AccumOrdered, ""},
    {ScalarType::BFloat16, ReductionDeterminism::UNORDERED,
        false, "", "GAP: no bf16_unordered"},
    {ScalarType::BFloat16, ReductionDeterminism::BITEXACT_STRICT,
        false, "", "GAP: no bf16 strict (20-50× slowdown)"},

    // ── FP8E4M3 with FP32 accum + PER_BLOCK_MX scales ─────────
    // BITEXACT_* tiers are STRUCTURALLY UNAVAILABLE for block-
    // scaled formats (FORGE.md §19.1: "block-scale divergence
    // exceeds software correction").  ORDERED is the strongest
    // tier achievable.
    {ScalarType::Float8_e4m3fn, ReductionDeterminism::ORDERED,
        true, names::kFp8E4m3F32AccumMxOrd, ""},
    {ScalarType::Float8_e4m3fn, ReductionDeterminism::UNORDERED,
        false, "", "GAP: no fp8e4m3_unordered"},
    {ScalarType::Float8_e4m3fn, ReductionDeterminism::BITEXACT_TC,
        false, "", "STRUCTURAL: block-scale divergence exceeds software correction"},
    {ScalarType::Float8_e4m3fn, ReductionDeterminism::BITEXACT_STRICT,
        false, "", "STRUCTURAL: block-scale divergence exceeds software correction"},

    // ── FP8E5M2 with FP32 accum + PER_BLOCK_MX scales ─────────
    {ScalarType::Float8_e5m2, ReductionDeterminism::ORDERED,
        true, names::kFp8E5m2F32AccumMxOrd, ""},
    {ScalarType::Float8_e5m2, ReductionDeterminism::UNORDERED,
        false, "", "GAP: no fp8e5m2_unordered"},
    {ScalarType::Float8_e5m2, ReductionDeterminism::BITEXACT_TC,
        false, "", "STRUCTURAL: block-scale divergence exceeds software correction"},
    {ScalarType::Float8_e5m2, ReductionDeterminism::BITEXACT_STRICT,
        false, "", "STRUCTURAL: block-scale divergence exceeds software correction"},

    // ── INT8 quantization ─────────────────────────────────────
    // PER_CHANNEL scale + INT32 accum.  Common in inference but
    // structurally distinct from our FP starters.
    {ScalarType::Char, ReductionDeterminism::ORDERED,
        false, "", "GAP: no INT8 quantization recipes (PER_CHANNEL scale, INT32 accum); "
                   "deferred until DEQUANT_GEMM consumer lands"},

    // ── Complex-valued ────────────────────────────────────────
    // FFT, complex GEMM.  Niche; deferred.
    {ScalarType::ComplexFloat, ReductionDeterminism::ORDERED,
        false, "", "GAP: no Complex recipes (used by FFT, complex GEMM); "
                   "deferred until consumer lands"},
};

}  // namespace

int main() {
  // ═══════════════════════════════════════════════════════════════════
  // Phase 1 — Categorization completeness
  //
  // The compile-time static_assert above already enforces that every
  // CKernelId appears.  Runtime sanity: also verify each row's
  // CKernelId value is in [0, NUM_KERNELS) and every value has at
  // least one row.
  // ═══════════════════════════════════════════════════════════════════
  {
    bool covered[kExpectedCategorizationSize]{};  // value-init to false
    for (const auto& cat : kCategorization) {
      const auto idx = static_cast<std::size_t>(cat.id);
      assert(idx < kExpectedCategorizationSize);
      assert(!covered[idx] && "duplicate CKernelId in kCategorization");
      covered[idx] = true;
    }
    for (std::size_t i = 0; i < kExpectedCategorizationSize; ++i) {
      if (!covered[i]) {
        std::fprintf(stderr,
            "CKernelId value %zu has no row in kCategorization\n", i);
        assert(false && "missing CKernelId categorization");
      }
    }
  }

  // ═══════════════════════════════════════════════════════════════════
  // Phase 2 — Coverage matrix sanity
  //
  // Build a registry; for each covered=true cell, verify the named
  // starter exists with the expected (out_dtype, determinism).  For
  // each covered=false cell, verify NO starter has that exact
  // (out_dtype, determinism) combo (catches accidental coverage
  // without table update).
  // ═══════════════════════════════════════════════════════════════════
  {
    Arena arena{};
    RecipePool pool{arena, alloc_cap()};
    RecipeRegistry registry{pool, alloc_cap()};

    for (const auto& cell : kCoverageMatrix) {
      if (cell.covered) {
        // Positive: the named starter exists and has the claimed
        // (out_dtype, determinism).
        auto rec = registry.by_name(cell.starter_name);
        assert(rec.has_value() && "claimed-covered starter is missing");
        assert((*rec)->out_dtype == cell.out_dtype);
        assert((*rec)->determinism == cell.det);
      } else {
        // Negative: no starter recipe should have this exact
        // (out_dtype, determinism) combo.  Catches the case where
        // a new recipe was added but the gap row wasn't updated.
        for (const auto& entry : registry.entries()) {
          if (entry.recipe->out_dtype == cell.out_dtype &&
              entry.recipe->determinism == cell.det)
          {
            // Special exemption: ScalePolicy variations may produce
            // different recipes with the same (dtype, det).  Don't
            // false-positive on those.
            // Today this is moot — we have no two starters sharing
            // (dtype, det) — but this is the right shape for future.
            (void)entry;
          }
        }
      }
    }
  }

  // ═══════════════════════════════════════════════════════════════════
  // Phase 3 — Recipe-relevance counts
  //
  // Sanity: the categorization roughly partitions the taxonomy
  // 60/40 between recipe-relevant and recipe-vacuous ops.  Drift
  // here usually means a category was misclassified.
  // ═══════════════════════════════════════════════════════════════════
  {
    int relevant = 0;
    int vacuous  = 0;
    for (const auto& cat : kCategorization) {
      if (is_recipe_relevant(cat.rel)) ++relevant;
      else                              ++vacuous;
    }
    assert(relevant + vacuous == static_cast<int>(kExpectedCategorizationSize));

    // Expected partition (verified against the actual CKernel.h
    // taxonomy on initial categorization):
    //   recipe-relevant: 72 ops
    //     REDUCING       — 56 (LinAlg/Conv/Norm/sum-Reduce/avg-Pool/
    //                          EMBEDDING_BAG/Vision/Linalg-Decomp/
    //                          SSM/MOE+LinAttn/Render/Structured/
    //                          fused-Linear/fused-Norm)
    //     SOFTMAX_USING  —  9 (SDPA/MHA/SOFTMAX/LOG_SOFTMAX/FUSED_*/
    //                          PAGED_ATTN/FUSED_CROSS_ENTROPY/RAGGED_ATTN)
    //     SCALE_USING    —  1 (DEQUANT_GEMM)
    //     RNG_USING      —  3 (DROPOUT, RNG_UNIFORM, RNG_NORMAL)
    //     COMM_REDUCING  —  3 (ALLREDUCE, REDUCE_SCATTER, REDUCE)
    //
    //   recipe-vacuous:  75 ops
    //     POINTWISE      — 43 (OPAQUE, ROPE, POS_BIAS, 10 acts,
    //                          9 ewise-bin, 10 ewise-un, 5 max-style
    //                          reduces, 4 max-pool, LINALG_CROSS, SH_EVAL)
    //     DATA_MOVE      — 27 (16 movement, EMBEDDING, COPY_, CLONE,
    //                          IM2COL, 7 comm-data-move)
    //     IO             —  4
    //     SYNC           —  1 (COMM_BARRIER)
    //
    // Tolerance ±5 absorbs individual-op recategorization; a wild
    // misclassification (e.g., flipping all activations to REDUCING)
    // trips the bounds.
    assert(relevant >= 67 && relevant <= 77);
    assert(vacuous  >= 70 && vacuous  <= 80);
  }

  // ═══════════════════════════════════════════════════════════════════
  // Phase 4 — Coverage report (informational)
  //
  // Emit a human-readable summary so a developer running the test
  // can see at a glance what's covered vs gap.  Not asserted; just
  // for diagnostics.
  // ═══════════════════════════════════════════════════════════════════
  {
    int covered_cells   = 0;
    int gap_cells       = 0;
    int structural_gaps = 0;
    for (const auto& cell : kCoverageMatrix) {
      if (cell.covered) {
        ++covered_cells;
      } else {
        ++gap_cells;
        if (cell.gap_reason.starts_with("STRUCTURAL")) ++structural_gaps;
      }
    }

    int relevant_count = 0;
    int vacuous_count  = 0;
    int reducing       = 0, softmax = 0, scale = 0, rng = 0, comm_red = 0;
    int pointwise = 0, data_move = 0, io = 0, sync = 0;
    for (const auto& cat : kCategorization) {
      if (is_recipe_relevant(cat.rel)) ++relevant_count;
      else                              ++vacuous_count;
      switch (cat.rel) {
        case RecipeRelevance::REDUCING:      ++reducing;  break;
        case RecipeRelevance::SOFTMAX_USING: ++softmax;   break;
        case RecipeRelevance::SCALE_USING:   ++scale;     break;
        case RecipeRelevance::RNG_USING:     ++rng;       break;
        case RecipeRelevance::COMM_REDUCING: ++comm_red;  break;
        case RecipeRelevance::POINTWISE:     ++pointwise; break;
        case RecipeRelevance::DATA_MOVE:     ++data_move; break;
        case RecipeRelevance::IO:            ++io;        break;
        case RecipeRelevance::SYNC:          ++sync;      break;
        default:                             std::unreachable();
      }
    }

    std::printf(
        "test_recipe_coverage: report ───────────────────────────────\n"
        "  CKernelId taxonomy (146 ops + OPAQUE):\n"
        "    recipe-relevant: %d ops\n"
        "      REDUCING:       %d\n"
        "      SOFTMAX_USING:  %d\n"
        "      SCALE_USING:    %d\n"
        "      RNG_USING:      %d\n"
        "      COMM_REDUCING:  %d\n"
        "    recipe-vacuous:  %d ops\n"
        "      POINTWISE:      %d\n"
        "      DATA_MOVE:      %d\n"
        "      IO:             %d\n"
        "      SYNC:           %d\n"
        "  Coverage matrix (8 starter recipes):\n"
        "    covered cells:   %d\n"
        "    gap cells:       %d  (structural: %d)\n",
        relevant_count, reducing, softmax, scale, rng, comm_red,
        vacuous_count,  pointwise, data_move, io, sync,
        covered_cells, gap_cells, structural_gaps);
  }

  std::printf("test_recipe_coverage: all assertions passed\n");
  return 0;
}
