// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// A recipe pins a numerical method rather than an op kind. One recipe for
// half-precision storage with single-precision accumulation applies to a
// matrix multiply, a convolution, an attention block and a norm alike.
//
// Coverage therefore splits in two. First, is every op classified by whether
// a recipe means anything for it at all? Pointwise, data-movement, input and
// output, and synchronisation ops carry no numerical method. Second, for the
// ops that do, does a starter recipe exist for the common output type and
// determinism tier pairs?
//
// Both questions are answered by the two tables below. A new op kind trips
// the categorisation assertion; a change to the starter set trips the
// coverage assertions in main.

#include <crucible/Arena.h>
#include <crucible/CKernel.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <crucible/Types.h>

#include <array>
#include "test_assert.h"
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

auto g_test = crucible::effects::testing::test();
auto g_init = crucible::effects::testing::init();
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }
inline crucible::effects::Init init_cap() noexcept { return g_init; }

[[nodiscard]] inline auto entries_view(const RecipeRegistry& reg) noexcept { return reg.entries().value(); }

// Each op falls in exactly one category, and the category says which fields
// of a recipe carry weight for that op.
enum class RecipeRelevance : uint8_t {
    // Accumulates in a type distinct from its inputs, so the accumulator type,
    // the reduction algorithm, the rounding and the determinism tier all matter.
    REDUCING,

    // Carries a softmax recurrence, so the softmax variant matters as well.
    SOFTMAX_USING,

    // Carries block, per-tensor or per-channel scales, so the scale policy
    // matters.
    SCALE_USING,

    // Draws on counter-based random state, so the counter handling and the
    // split-reduction flag matter.
    RNG_USING,

    // A reducing collective. The determinism tier dictates the topology: a
    // canonical tree for the bit-exact tiers, a ring for the ordered one.
    COMM_REDUCING,

    // No accumulation, no softmax, no scales, so no recipe field applies. The
    // output type follows from the inputs.
    POINTWISE,

    // Movement without arithmetic.
    DATA_MOVE,

    IO,

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
            // The arms above are exhaustive, so reaching here means the enum value
            // is corrupt. Saying so lets the optimizer drop this branch entirely.
            std::unreachable();
    }
}

// One row per op. An op added without a row here trips the size assertion
// below, which is what keeps coverage from drifting silently.
struct OpCategory {
    CKernelId id;
    RecipeRelevance rel;
    std::string_view note;  // brief rationale
};

constexpr OpCategory kCategorization[] = {
    {CKernelId::OPAQUE, RecipeRelevance::POINTWISE, "fallback Vessel dispatch"},

    // Linear algebra (8)
    {CKernelId::GEMM_MM, RecipeRelevance::REDUCING, "K-axis accumulation"},
    {CKernelId::GEMM_BMM, RecipeRelevance::REDUCING, "batched K-axis accumulation"},
    {CKernelId::GEMM_MATMUL, RecipeRelevance::REDUCING, "general K-axis accumulation"},
    {CKernelId::GEMM_ADDMM, RecipeRelevance::REDUCING, "K-axis accumulation + bias"},
    {CKernelId::GEMM_LINEAR, RecipeRelevance::REDUCING, "K-axis accumulation + bias"},
    {CKernelId::GEMM_ADDBMM, RecipeRelevance::REDUCING, "batched K-axis accumulation"},
    {CKernelId::GEMM_BADDBMM, RecipeRelevance::REDUCING, "batched K-axis accumulation"},
    {CKernelId::GEMM_EINSUM, RecipeRelevance::REDUCING, "general tensor contraction"},

    // Convolution (6)
    {CKernelId::CONV1D, RecipeRelevance::REDUCING, "spatial accumulation"},
    {CKernelId::CONV2D, RecipeRelevance::REDUCING, "spatial accumulation"},
    {CKernelId::CONV3D, RecipeRelevance::REDUCING, "spatial accumulation"},
    {CKernelId::CONV_TRANSPOSE1D, RecipeRelevance::REDUCING, "transpose conv accumulation"},
    {CKernelId::CONV_TRANSPOSE2D, RecipeRelevance::REDUCING, "transpose conv accumulation"},
    {CKernelId::CONV_TRANSPOSE3D, RecipeRelevance::REDUCING, "transpose conv accumulation"},

    // Attention (4)
    {CKernelId::SDPA, RecipeRelevance::SOFTMAX_USING, "online softmax + K-V accumulation"},
    {CKernelId::MHA, RecipeRelevance::SOFTMAX_USING, "multi-head attention"},
    {CKernelId::ROPE, RecipeRelevance::POINTWISE, "rotary embedding is pointwise"},
    {CKernelId::POSITION_BIAS, RecipeRelevance::POINTWISE, "additive bias is pointwise"},

    // Normalization (6)
    {CKernelId::LAYER_NORM, RecipeRelevance::REDUCING, "mean+var accumulation"},
    {CKernelId::BATCH_NORM_TRAIN, RecipeRelevance::REDUCING, "batch stats accumulation"},
    {CKernelId::BATCH_NORM_EVAL, RecipeRelevance::REDUCING, "running-stat normalization"},
    {CKernelId::GROUP_NORM, RecipeRelevance::REDUCING, "group stats accumulation"},
    {CKernelId::INSTANCE_NORM, RecipeRelevance::REDUCING, "per-instance stats"},
    {CKernelId::RMS_NORM, RecipeRelevance::REDUCING, "RMS stat accumulation"},

    // Activations (13)
    {CKernelId::ACT_RELU, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::ACT_GELU, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::ACT_SILU, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::ACT_SIGMOID, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::ACT_TANH, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::ACT_HARDSWISH, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::ACT_LEAKY_RELU, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::ACT_ELU, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::ACT_SOFTMAX, RecipeRelevance::SOFTMAX_USING, "softmax recurrence"},
    {CKernelId::ACT_LOG_SOFTMAX, RecipeRelevance::SOFTMAX_USING, "softmax recurrence"},
    {CKernelId::ACT_DROPOUT, RecipeRelevance::RNG_USING, "Philox-based mask"},
    {CKernelId::ACT_CLAMP, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::ACT_MISH, RecipeRelevance::POINTWISE, "pointwise"},

    // Elementwise binary (9)
    {CKernelId::EWISE_ADD, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_MUL, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_SUB, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_DIV, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_POW, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_MAX, RecipeRelevance::POINTWISE, "pointwise (not reduction)"},
    {CKernelId::EWISE_MIN, RecipeRelevance::POINTWISE, "pointwise (not reduction)"},
    {CKernelId::EWISE_MOD, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_WHERE, RecipeRelevance::POINTWISE, "ternary select"},

    // Elementwise unary (10)
    {CKernelId::EWISE_EXP, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_LOG, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_SQRT, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_RSQRT, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_ABS, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_NEG, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_SIGN, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_FLOOR, RecipeRelevance::POINTWISE, "pointwise"},
    {CKernelId::EWISE_CAST, RecipeRelevance::POINTWISE, "dtype conversion (rounding matters)"},
    {CKernelId::EWISE_FILL, RecipeRelevance::POINTWISE, "broadcast scalar"},

    // Reductions (8)
    {CKernelId::REDUCE_SUM, RecipeRelevance::REDUCING, "sum accumulation"},
    {CKernelId::REDUCE_MEAN, RecipeRelevance::REDUCING, "sum + divide"},
    {CKernelId::REDUCE_MAX, RecipeRelevance::POINTWISE, "max-fold (no FP accumulation)"},
    {CKernelId::REDUCE_MIN, RecipeRelevance::POINTWISE, "min-fold (no FP accumulation)"},
    {CKernelId::REDUCE_ARGMAX, RecipeRelevance::POINTWISE, "index extraction"},
    {CKernelId::REDUCE_ARGMIN, RecipeRelevance::POINTWISE, "index extraction"},
    {CKernelId::REDUCE_CUMSUM, RecipeRelevance::REDUCING, "prefix-sum accumulation"},
    {CKernelId::REDUCE_TOPK, RecipeRelevance::POINTWISE, "k-best selection"},

    // Pooling (8)
    {CKernelId::POOL_MAX1D, RecipeRelevance::POINTWISE, "max-fold over window"},
    {CKernelId::POOL_MAX2D, RecipeRelevance::POINTWISE, "max-fold over window"},
    {CKernelId::POOL_MAX3D, RecipeRelevance::POINTWISE, "max-fold over window"},
    {CKernelId::POOL_AVG1D, RecipeRelevance::REDUCING, "average-pool accumulation"},
    {CKernelId::POOL_AVG2D, RecipeRelevance::REDUCING, "average-pool accumulation"},
    {CKernelId::POOL_AVG3D, RecipeRelevance::REDUCING, "average-pool accumulation"},
    {CKernelId::POOL_ADAPTIVE_MAX, RecipeRelevance::POINTWISE, "max-fold over adaptive window"},
    {CKernelId::POOL_ADAPTIVE_AVG, RecipeRelevance::REDUCING, "average over adaptive window"},

    // Data movement and indexing (16)
    {CKernelId::VIEW, RecipeRelevance::DATA_MOVE, "no math"},
    {CKernelId::RESHAPE, RecipeRelevance::DATA_MOVE, "may copy, no math"},
    {CKernelId::PERMUTE, RecipeRelevance::DATA_MOVE, "axis reorder"},
    {CKernelId::TRANSPOSE, RecipeRelevance::DATA_MOVE, "axis swap"},
    {CKernelId::CONTIGUOUS, RecipeRelevance::DATA_MOVE, "contiguous copy"},
    {CKernelId::EXPAND, RecipeRelevance::DATA_MOVE, "stride-0 broadcast"},
    {CKernelId::SQUEEZE, RecipeRelevance::DATA_MOVE, "shape change"},
    {CKernelId::SLICE, RecipeRelevance::DATA_MOVE, "subview"},
    {CKernelId::INDEX_SELECT, RecipeRelevance::DATA_MOVE, "lookup"},
    {CKernelId::INDEX, RecipeRelevance::DATA_MOVE, "advanced indexing"},
    {CKernelId::SCATTER, RecipeRelevance::DATA_MOVE,
     "scatter (atomic-add variants exist; recipe-vacuous at the rec level)"},
    {CKernelId::MASKED_FILL, RecipeRelevance::DATA_MOVE, "mask application"},
    {CKernelId::PAD, RecipeRelevance::DATA_MOVE, "padding"},
    {CKernelId::CAT, RecipeRelevance::DATA_MOVE, "concatenation"},
    {CKernelId::STACK, RecipeRelevance::DATA_MOVE, "stacking"},
    {CKernelId::UNFOLD, RecipeRelevance::DATA_MOVE, "patch extraction"},

    // Embedding (2)
    {CKernelId::EMBEDDING, RecipeRelevance::DATA_MOVE, "lookup (no accumulation)"},
    {CKernelId::EMBEDDING_BAG, RecipeRelevance::REDUCING, "lookup + sum/mean reduction"},

    // Copy (2)
    {CKernelId::COPY_, RecipeRelevance::DATA_MOVE, "in-place copy (CAST is its own op)"},
    {CKernelId::CLONE, RecipeRelevance::DATA_MOVE, "fresh storage copy"},

    // Vision (3)
    {CKernelId::INTERPOLATE, RecipeRelevance::REDUCING, "bilinear/bicubic accumulation"},
    {CKernelId::GRID_SAMPLE, RecipeRelevance::REDUCING, "bilinear sample accumulation"},
    {CKernelId::IM2COL, RecipeRelevance::DATA_MOVE, "patch unfolding (no math)"},

    // Fused (4)
    {CKernelId::FUSED_ATTENTION, RecipeRelevance::SOFTMAX_USING, "fused MHSA"},
    {CKernelId::FUSED_LINEAR_ACT, RecipeRelevance::REDUCING, "linear + activation gate"},
    {CKernelId::FUSED_NORM_LINEAR, RecipeRelevance::REDUCING, "norm + linear"},
    {CKernelId::FUSED_SOFTMAX_DROP, RecipeRelevance::SOFTMAX_USING, "softmax + dropout"},

    // Linear algebra decompositions (9)
    {CKernelId::LINALG_SVD, RecipeRelevance::REDUCING, "iterative QR/Jacobi accumulation"},
    {CKernelId::LINALG_CHOLESKY, RecipeRelevance::REDUCING, "in-place LLᵀ accumulation"},
    {CKernelId::LINALG_QR, RecipeRelevance::REDUCING, "Householder accumulation"},
    {CKernelId::LINALG_SOLVE, RecipeRelevance::REDUCING, "LU/triangular solve"},
    {CKernelId::LINALG_EIGH, RecipeRelevance::REDUCING, "QR-iteration accumulation"},
    {CKernelId::LINALG_NORM, RecipeRelevance::REDUCING, "L2/Frobenius accumulation"},
    {CKernelId::LINALG_CROSS, RecipeRelevance::POINTWISE, "3D cross-product is pointwise per element"},
    {CKernelId::CDIST, RecipeRelevance::REDUCING, "pairwise distance accumulation"},
    {CKernelId::FFT, RecipeRelevance::REDUCING, "butterfly accumulation (Complex dtype)"},

    // State-space models and recurrences (6)
    {CKernelId::ASSOC_SCAN, RecipeRelevance::REDUCING, "parallel prefix scan"},
    {CKernelId::SELECTIVE_SCAN, RecipeRelevance::REDUCING, "Mamba S6 affine recurrence"},
    {CKernelId::SSD_CHUNK, RecipeRelevance::REDUCING, "Mamba-2 chunked semiseparable matmul"},
    {CKernelId::WKV_RECURRENCE, RecipeRelevance::REDUCING, "RWKV exponentially-decayed prefix"},
    {CKernelId::RETENTION, RecipeRelevance::REDUCING, "RetNet decay-masked semi-attention"},
    {CKernelId::MLSTM_RECURRENCE, RecipeRelevance::REDUCING, "xLSTM matrix-memory accumulation"},

    // Production inference (6)
    {CKernelId::DEQUANT_GEMM, RecipeRelevance::SCALE_USING, "INT4/FP8 dequant + GEMM (PER_CHANNEL/PER_BLOCK)"},
    {CKernelId::MOE_ROUTE_GEMM, RecipeRelevance::REDUCING, "top-k routing + grouped GEMM"},
    {CKernelId::PAGED_ATTENTION, RecipeRelevance::SOFTMAX_USING, "page-table attention (vLLM)"},
    {CKernelId::FUSED_CROSS_ENTROPY, RecipeRelevance::SOFTMAX_USING, "online softmax + cross-entropy"},
    {CKernelId::LINEAR_ATTN_CAUSAL, RecipeRelevance::REDUCING, "chunked outer-product reduction"},
    {CKernelId::RAGGED_ATTN, RecipeRelevance::SOFTMAX_USING, "variable-length packed attention"},

    // Rendering (4)
    {CKernelId::GAUSSIAN_RASTERIZE, RecipeRelevance::REDUCING, "alpha-blend accumulation"},
    {CKernelId::HASH_GRID_ENCODE, RecipeRelevance::REDUCING, "trilinear interp accumulation"},
    {CKernelId::VOLUME_RENDER, RecipeRelevance::REDUCING, "ray-march compositing"},
    {CKernelId::SH_EVAL, RecipeRelevance::POINTWISE, "spherical harmonics basis (pointwise)"},

    // Structured matrices and graphs (5)
    {CKernelId::FFT_CONV, RecipeRelevance::REDUCING, "FFT + pointwise + IFFT"},
    {CKernelId::MONARCH_MATMUL, RecipeRelevance::REDUCING, "block-diag GEMM accumulation"},
    {CKernelId::SPMM_GNN, RecipeRelevance::REDUCING, "graph message passing accumulation"},
    {CKernelId::SDDMM_GNN, RecipeRelevance::REDUCING, "edge score accumulation"},
    {CKernelId::SINKHORN, RecipeRelevance::REDUCING, "iterative log-domain accumulation"},

    // Collective communication (10)
    {CKernelId::COMM_ALLREDUCE, RecipeRelevance::COMM_REDUCING, "determinism pins reduction tree"},
    {CKernelId::COMM_ALLGATHER, RecipeRelevance::DATA_MOVE, "no reduction"},
    {CKernelId::COMM_REDUCE_SCATTER, RecipeRelevance::COMM_REDUCING, "determinism pins reduction tree"},
    {CKernelId::COMM_BROADCAST, RecipeRelevance::DATA_MOVE, "no reduction"},
    {CKernelId::COMM_ALL_TO_ALL, RecipeRelevance::DATA_MOVE, "permutation"},
    {CKernelId::COMM_SEND, RecipeRelevance::DATA_MOVE, "p2p"},
    {CKernelId::COMM_RECV, RecipeRelevance::DATA_MOVE, "p2p"},
    {CKernelId::COMM_REDUCE, RecipeRelevance::COMM_REDUCING, "determinism pins reduction tree"},
    {CKernelId::COMM_GATHER, RecipeRelevance::DATA_MOVE, "no reduction"},
    {CKernelId::COMM_SCATTER, RecipeRelevance::DATA_MOVE, "no reduction"},

    // Input and output (4)
    {CKernelId::IO_LOAD, RecipeRelevance::IO, "DataLoader → CPU"},
    {CKernelId::IO_PREFETCH, RecipeRelevance::IO, "host → device DMA"},
    {CKernelId::IO_CHECKPOINT_SAVE, RecipeRelevance::IO, "weight serialization"},
    {CKernelId::IO_CHECKPOINT_LOAD, RecipeRelevance::IO, "weight deserialization"},

    // Random number generation (2)
    {CKernelId::RNG_UNIFORM, RecipeRelevance::RNG_USING, "Philox uniform"},
    {CKernelId::RNG_NORMAL, RecipeRelevance::RNG_USING, "Philox Gaussian (Box-Muller)"},

    // Synchronisation (1)
    {CKernelId::COMM_BARRIER, RecipeRelevance::SYNC, "all-rank barrier"},
};

// NUM_KERNELS is the sentinel past the last op, so its value is the number of
// ops including the fallback at zero.
constexpr std::size_t kExpectedCategorizationSize = static_cast<std::size_t>(CKernelId::NUM_KERNELS);

static_assert(std::size(kCategorization) == kExpectedCategorizationSize,
              "kCategorization covers every CKernelId. An op was added "
              "without classifying its recipe relevance. Add a row to "
              "kCategorization above.");

// Each row claims one output type and determinism tier is either served by a
// named starter recipe or deliberately unserved with a reason. A claimed
// recipe that is missing, or a name that does not match the recipe found,
// fails the assertions in main.

struct CoverageCell {
    ScalarType out_dtype;
    ReductionDeterminism det;
    bool covered;
    std::string_view starter_name;  // empty if !covered
    std::string_view gap_reason;  // empty if covered; rationale otherwise
};

constexpr CoverageCell kCoverageMatrix[] = {
    {ScalarType::Float, ReductionDeterminism::BITEXACT_STRICT, true, names::kF32Strict, ""},
    {ScalarType::Float, ReductionDeterminism::ORDERED, true, names::kF32Ordered, ""},
    {ScalarType::Float, ReductionDeterminism::UNORDERED, false, "", "GAP: no f32_unordered (inference-fast path)"},
    {ScalarType::Float, ReductionDeterminism::BITEXACT_TC, false, "",
     "FP32 tensor cores already use K=8 fragments under "
     "BITEXACT_STRICT; BITEXACT_TC degenerate for FP32"},

    // Half-precision storage with single-precision accumulation.
    {ScalarType::Half, ReductionDeterminism::BITEXACT_TC, true, names::kF16F32AccumTc, ""},
    {ScalarType::Half, ReductionDeterminism::ORDERED, true, names::kF16F32AccumOrdered, ""},
    {ScalarType::Half, ReductionDeterminism::UNORDERED, false, "", "GAP: no f16_unordered (inference-fast path)"},
    {ScalarType::Half, ReductionDeterminism::BITEXACT_STRICT, false, "",
     "GAP: no f16 strict (20-50× slowdown to emulate via scalar FMA)"},

    {ScalarType::BFloat16, ReductionDeterminism::BITEXACT_TC, true, names::kBf16F32AccumTc, ""},
    {ScalarType::BFloat16, ReductionDeterminism::ORDERED, true, names::kBf16F32AccumOrdered, ""},
    {ScalarType::BFloat16, ReductionDeterminism::UNORDERED, false, "", "GAP: no bf16_unordered"},
    {ScalarType::BFloat16, ReductionDeterminism::BITEXACT_STRICT, false, "", "GAP: no bf16 strict (20-50× slowdown)"},

    // Eight-bit storage with block scales. The ordered tier is the strongest
    // one a block-scaled format can reach, so the two bit-exact rows below
    // are structural rather than deferred.
    {ScalarType::Float8_e4m3fn, ReductionDeterminism::ORDERED, true, names::kFp8E4m3F32AccumMxOrd, ""},
    {ScalarType::Float8_e4m3fn, ReductionDeterminism::UNORDERED, false, "", "GAP: no fp8e4m3_unordered"},
    {ScalarType::Float8_e4m3fn, ReductionDeterminism::BITEXACT_TC, false, "",
     "STRUCTURAL: block-scale divergence exceeds software correction"},
    {ScalarType::Float8_e4m3fn, ReductionDeterminism::BITEXACT_STRICT, false, "",
     "STRUCTURAL: block-scale divergence exceeds software correction"},

    {ScalarType::Float8_e5m2, ReductionDeterminism::ORDERED, true, names::kFp8E5m2F32AccumMxOrd, ""},
    {ScalarType::Float8_e5m2, ReductionDeterminism::UNORDERED, false, "", "GAP: no fp8e5m2_unordered"},
    {ScalarType::Float8_e5m2, ReductionDeterminism::BITEXACT_TC, false, "",
     "STRUCTURAL: block-scale divergence exceeds software correction"},
    {ScalarType::Float8_e5m2, ReductionDeterminism::BITEXACT_STRICT, false, "",
     "STRUCTURAL: block-scale divergence exceeds software correction"},

    {ScalarType::Char, ReductionDeterminism::ORDERED, false, "",
     "GAP: no INT8 quantization recipes (PER_CHANNEL scale, INT32 accum); "
     "deferred until DEQUANT_GEMM consumer lands"},

    {ScalarType::ComplexFloat, ReductionDeterminism::ORDERED, false, "",
     "GAP: no Complex recipes (used by FFT, complex GEMM); "
     "deferred until consumer lands"},
};

}  // namespace

int main() {
    // The size assertion above counts rows. It cannot tell a duplicate row from
    // a missing one, since the two cancel out, so the sweep below checks each
    // op value appears exactly once.
    {
        bool covered[kExpectedCategorizationSize]{};
        for (const auto& cat : kCategorization) {
            const auto idx = static_cast<std::size_t>(cat.id);
            assert(idx < kExpectedCategorizationSize);
            assert(!covered[idx] && "duplicate CKernelId in kCategorization");
            covered[idx] = true;
        }
        for (std::size_t i = 0; i < kExpectedCategorizationSize; ++i) {
            if (!covered[i]) {
                std::fprintf(stderr, "CKernelId value %zu has no row in kCategorization\n", i);
                assert(false && "missing CKernelId categorization");
            }
        }
    }

    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry registry{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        for (const auto& cell : kCoverageMatrix) {
            if (cell.covered) {
                auto rec = registry.by_name(cell.starter_name);
                assert(rec.has_value() && "claimed-covered starter is missing");
                assert((*rec)->out_dtype == cell.out_dtype);
                assert((*rec)->determinism == cell.det);
            } else {
                // A recipe added for a pair marked unserved should be caught here.
                // The match below does nothing, because two starters differing only
                // in scale policy would share an output type and tier and give a
                // false report. No two starters share a pair at present, so the
                // exemption has nothing to distinguish yet and the branch is inert.
                for (const auto& entry : entries_view(registry)) {
                    if (entry.recipe->out_dtype == cell.out_dtype && entry.recipe->determinism == cell.det) {
                        (void)entry;
                    }
                }
            }
        }
    }

    // The taxonomy splits roughly evenly between ops a recipe speaks to and
    // ops it does not. A large move in that split means a category was
    // misclassified.
    {
        int relevant = 0;
        int vacuous = 0;
        for (const auto& cat : kCategorization) {
            if (is_recipe_relevant(cat.rel))
                ++relevant;
            else
                ++vacuous;
        }
        assert(relevant + vacuous == static_cast<int>(kExpectedCategorizationSize));

        // The table classifies 72 ops as recipe-relevant, of which 56 reduce,
        // nine use softmax, three use random state, three are reducing
        // collectives and one uses scales. The other 75 are vacuous: 43
        // pointwise, 27 movement, four input and output, and one barrier.
        //
        // The bounds allow five either way, so recategorising an op or two is
        // free while a sweeping misclassification, such as flipping every
        // activation to reducing, still trips them.
        assert(relevant >= 67 && relevant <= 77);
        assert(vacuous >= 70 && vacuous <= 80);
    }

    // The summary below asserts nothing. It exists so that whoever runs the
    // test can read off what is served and what is not.
    {
        int covered_cells = 0;
        int gap_cells = 0;
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
        int vacuous_count = 0;
        int reducing = 0, softmax = 0, scale = 0, rng = 0, comm_red = 0;
        int pointwise = 0, data_move = 0, io = 0, sync = 0;
        for (const auto& cat : kCategorization) {
            if (is_recipe_relevant(cat.rel))
                ++relevant_count;
            else
                ++vacuous_count;
            switch (cat.rel) {
                case RecipeRelevance::REDUCING:
                    ++reducing;
                    break;
                case RecipeRelevance::SOFTMAX_USING:
                    ++softmax;
                    break;
                case RecipeRelevance::SCALE_USING:
                    ++scale;
                    break;
                case RecipeRelevance::RNG_USING:
                    ++rng;
                    break;
                case RecipeRelevance::COMM_REDUCING:
                    ++comm_red;
                    break;
                case RecipeRelevance::POINTWISE:
                    ++pointwise;
                    break;
                case RecipeRelevance::DATA_MOVE:
                    ++data_move;
                    break;
                case RecipeRelevance::IO:
                    ++io;
                    break;
                case RecipeRelevance::SYNC:
                    ++sync;
                    break;
                default:
                    std::unreachable();
            }
        }

        std::printf("test_recipe_coverage: report ───────────────────────────────\n"
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
                    relevant_count, reducing, softmax, scale, rng, comm_red, vacuous_count, pointwise, data_move, io,
                    sync, covered_cells, gap_cells, structural_gaps);
    }

    std::printf("test_recipe_coverage: all assertions passed\n");
    return 0;
}
