#pragma once

// The kernel taxonomy as an enum, and the gate that widens an untrusted
// byte into it.
//
// A trace entry stores one CKernelId, so every includer of the trace types
// needs the enum.  The registration table, the classification and the names
// of the identifiers are in crucible/CKernel.h, which includes this header.

#include <fixy/Refined.h>

#include <cstdint>

namespace crucible {

// Enumerator values are a persisted format. They are written into trace files
// and recovered from untrusted bytes, so an existing enumerator is never
// renumbered or removed, and a new one is appended immediately before
// NUM_KERNELS. OPAQUE is 0 so a zero-initialised classification field already
// holds the safe fallback.
//
// Aliases and backend variants of one mathematical operation share a single
// identifier. An in-place variant folds into its out-of-place identifier. The
// aliasing itself is recorded in the trace entry flags, not here.

enum class CKernelId : uint8_t {
    OPAQUE = 0,

    GEMM_MM,
    GEMM_BMM,
    GEMM_MATMUL,
    GEMM_ADDMM,
    GEMM_LINEAR,
    GEMM_ADDBMM,
    GEMM_BADDBMM,
    GEMM_EINSUM,

    CONV1D,
    CONV2D,
    CONV3D,
    CONV_TRANSPOSE1D,
    CONV_TRANSPOSE2D,
    CONV_TRANSPOSE3D,

    SDPA,
    MHA,
    ROPE,
    POSITION_BIAS,

    LAYER_NORM,
    BATCH_NORM_TRAIN,
    BATCH_NORM_EVAL,
    GROUP_NORM,
    INSTANCE_NORM,
    RMS_NORM,

    ACT_RELU,
    ACT_GELU,
    ACT_SILU,
    ACT_SIGMOID,
    ACT_TANH,
    ACT_HARDSWISH,
    ACT_LEAKY_RELU,
    ACT_ELU,
    ACT_SOFTMAX,
    ACT_LOG_SOFTMAX,
    ACT_DROPOUT,
    ACT_CLAMP,
    ACT_MISH,

    EWISE_ADD,
    EWISE_MUL,
    EWISE_SUB,
    EWISE_DIV,
    EWISE_POW,
    EWISE_MAX,
    EWISE_MIN,
    EWISE_MOD,
    EWISE_WHERE,

    EWISE_EXP,
    EWISE_LOG,
    EWISE_SQRT,
    EWISE_RSQRT,
    EWISE_ABS,
    EWISE_NEG,
    EWISE_SIGN,
    EWISE_FLOOR,
    EWISE_CAST,
    EWISE_FILL,

    REDUCE_SUM,
    REDUCE_MEAN,
    REDUCE_MAX,
    REDUCE_MIN,
    REDUCE_ARGMAX,
    REDUCE_ARGMIN,
    REDUCE_CUMSUM,
    REDUCE_TOPK,

    POOL_MAX1D,
    POOL_MAX2D,
    POOL_MAX3D,
    POOL_AVG1D,
    POOL_AVG2D,
    POOL_AVG3D,
    POOL_ADAPTIVE_MAX,
    POOL_ADAPTIVE_AVG,

    VIEW,
    RESHAPE,
    PERMUTE,
    TRANSPOSE,
    CONTIGUOUS,
    EXPAND,
    SQUEEZE,
    SLICE,
    INDEX_SELECT,
    INDEX,
    SCATTER,
    MASKED_FILL,
    PAD,
    CAT,
    STACK,
    UNFOLD,

    EMBEDDING,
    EMBEDDING_BAG,

    COPY_,
    CLONE,

    INTERPOLATE,
    GRID_SAMPLE,
    IM2COL,

    FUSED_ATTENTION,
    FUSED_LINEAR_ACT,
    FUSED_NORM_LINEAR,
    FUSED_SOFTMAX_DROP,

    LINALG_SVD,
    LINALG_CHOLESKY,
    LINALG_QR,
    LINALG_SOLVE,
    LINALG_EIGH,
    LINALG_NORM,
    LINALG_CROSS,
    CDIST,
    FFT,

    ASSOC_SCAN,
    SELECTIVE_SCAN,
    SSD_CHUNK,
    WKV_RECURRENCE,
    RETENTION,
    MLSTM_RECURRENCE,

    DEQUANT_GEMM,
    MOE_ROUTE_GEMM,
    PAGED_ATTENTION,
    FUSED_CROSS_ENTROPY,
    LINEAR_ATTN_CAUSAL,
    RAGGED_ATTN,

    GAUSSIAN_RASTERIZE,
    HASH_GRID_ENCODE,
    VOLUME_RENDER,
    SH_EVAL,

    FFT_CONV,
    MONARCH_MATMUL,
    SPMM_GNN,
    SDDMM_GNN,
    SINKHORN,

    COMM_ALLREDUCE,
    COMM_ALLGATHER,
    COMM_REDUCE_SCATTER,
    COMM_BROADCAST,
    COMM_ALL_TO_ALL,
    COMM_SEND,
    COMM_RECV,
    COMM_REDUCE,
    COMM_GATHER,
    COMM_SCATTER,

    IO_LOAD,
    IO_PREFETCH,
    IO_CHECKPOINT_SAVE,
    IO_CHECKPOINT_LOAD,

    RNG_UNIFORM,
    RNG_NORMAL,

    COMM_BARRIER,

    NUM_KERNELS
};

// The gate for widening an untrusted byte back into CKernelId. A byte
// recovered from a trace file or across a foreign-runtime boundary can be out
// of range after version skew or corruption, and a bare static_cast would turn
// it into an enum value no switch handles. Admitting only [0, NUM_KERNELS)
// makes the widening below total.  The one door into the type is
// ::fixy::mint_refined<kValidCKernelIdBound>(byte).
inline constexpr auto kValidCKernelIdBound =
    ::fixy::bounded_above<static_cast<uint8_t>(CKernelId::NUM_KERNELS) - uint8_t{1}>;

using ValidCKernelIdRaw = ::fixy::Refined<kValidCKernelIdBound, uint8_t>;

[[nodiscard, gnu::const]] inline constexpr CKernelId make_ckernel_id(ValidCKernelIdRaw raw) noexcept {
    return static_cast<CKernelId>(raw.value());
}

}  // namespace crucible
