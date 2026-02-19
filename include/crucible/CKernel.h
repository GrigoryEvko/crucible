#pragma once

// CKernel: Crucible abstract compute-op taxonomy.
//
// Maps Vessel op identity (schema_hash, an opaque uint64) to a Crucible-native
// CKernelId. Used by the background thread to annotate TraceEntry at build time
// so Tier 2+ replay can dispatch directly without going through the Vessel.
//
// Design: ~100 device-agnostic ops covering all transformer / vision workloads.
// Multiple Vessel dispatch names can map to one CKernelId (e.g., "softmax" and
// "_softmax" both register to ACT_SOFTMAX; all 5 SDPA backend variants → SDPA).
// Everything else → CKernelId::OPAQUE (fallback to full Vessel dispatch).
//
// Registration model (two-phase):
//   1. Vessel calls register_schema_hash() once per known op at init time,
//      computing the hash via its own hash function (e.g. std::hash<OperatorName>).
//      The standalone library ships with an empty table — all OPAQUE.
//   2. BackgroundThread calls classify_kernel() during build_trace().
//      Returns OPAQUE until the Vessel has registered ops.
//
// Thread safety: all registrations MUST complete before BackgroundThread::start()
// is called. After that the table is read-only (no locking needed).

#include <algorithm>
#include <cstdint>

namespace crucible {

// ── Compute-op taxonomy ─────────────────────────────────────────────────────
//
// Organized by compute family. OPAQUE = 0 so zero-initialized TraceEntry
// fields are correct before classification. All values fit in uint8_t (< 128).
//
// Inplace variants (add_, relu_) are folded into their out-of-place counterparts.
// Memory aliasing is tracked separately in TraceEntry flags; the kernel compute
// identity is the same.

enum class CKernelId : uint8_t {
    OPAQUE = 0,     // Unknown op — fallback to Vessel dispatch (always correct)

    // ── Linear Algebra (8) ──────────────────────────────────────────────────
    // Hot path for every linear layer. Tier 2: cuBLAS/cuBLASLt direct call.
    GEMM_MM,        // aten::mm                 — 2D × 2D, no bias
    GEMM_BMM,       // aten::bmm                — batched 3D × 3D
    GEMM_MATMUL,    // aten::matmul             — general rank + broadcast
    GEMM_ADDMM,     // aten::addmm              — bias + mat1@mat2 (Linear hot path)
    GEMM_LINEAR,    // aten::linear             — weight + optional bias wrapper
    GEMM_ADDBMM,    // aten::addbmm             — batched addmm with alpha/beta
    GEMM_BADDBMM,   // aten::baddbmm            — batched bias + batched matmul
    GEMM_EINSUM,    // aten::einsum             — general tensor contraction

    // ── Convolution (6) ─────────────────────────────────────────────────────
    // aten::convolution dispatches to the variant below based on ndim.
    CONV1D,         // aten::conv1d / aten::convolution (1D)
    CONV2D,         // aten::conv2d / aten::convolution (2D)
    CONV3D,         // aten::conv3d / aten::convolution (3D)
    CONV_TRANSPOSE1D, // aten::conv_transpose1d
    CONV_TRANSPOSE2D, // aten::conv_transpose2d
    CONV_TRANSPOSE3D, // aten::conv_transpose3d

    // ── Attention (4) ───────────────────────────────────────────────────────
    // All 5 ATen SDPA backend variants register to SDPA:
    //   scaled_dot_product_attention, _scaled_dot_product_flash_attention,
    //   _scaled_dot_product_efficient_attention, _scaled_dot_product_cudnn_attention,
    //   _flash_attention_forward
    SDPA,           // scaled_dot_product_attention (+ all backend variants)
    MHA,            // multi_head_attention_forward (pre-F.scaled_dot_product_attention)
    ROPE,           // rotary_embedding (LLaMA/Mistral — may be extension op)
    POSITION_BIAS,  // ALiBi and additive position bias variants

    // ── Normalization (6) ───────────────────────────────────────────────────
    // Vessel must register both the user-facing and the native dispatch name:
    //   layer_norm + native_layer_norm             → LAYER_NORM
    //   batch_norm (training=true)                 → BATCH_NORM_TRAIN
    //   _native_batch_norm_legit_no_training       → BATCH_NORM_EVAL
    //   group_norm + native_group_norm             → GROUP_NORM
    LAYER_NORM,     // layer_norm / native_layer_norm
    BATCH_NORM_TRAIN, // batch_norm (training=true, updates running stats)
    BATCH_NORM_EVAL,  // batch_norm inference / _native_batch_norm_legit_no_training
    GROUP_NORM,     // group_norm / native_group_norm
    INSTANCE_NORM,  // instance_norm
    RMS_NORM,       // rms_norm (LLaMA/Mistral — often a custom extension op)

    // ── Activations (13) ────────────────────────────────────────────────────
    // softmax + _softmax → ACT_SOFTMAX; log_softmax + _log_softmax → ACT_LOG_SOFTMAX.
    ACT_RELU,       // relu / relu_
    ACT_GELU,       // gelu / gelu_ (exact and tanh-approximation variants)
    ACT_SILU,       // silu / silu_ (swish)
    ACT_SIGMOID,    // sigmoid / sigmoid_
    ACT_TANH,       // tanh / tanh_
    ACT_HARDSWISH,  // hardswish / hardswish_
    ACT_LEAKY_RELU, // leaky_relu (negative slope parameter)
    ACT_ELU,        // elu / elu_ / selu / celu
    ACT_SOFTMAX,    // softmax / _softmax
    ACT_LOG_SOFTMAX,// log_softmax / _log_softmax
    ACT_DROPOUT,    // dropout (training: stochastic mask; inference: identity)
    ACT_CLAMP,      // clamp / hardtanh / relu6
    ACT_MISH,       // mish

    // ── Elementwise Binary (9) ──────────────────────────────────────────────
    // Inplace variants (add_, mul_, …) fold into the out-of-place CKernelId.
    // Memory aliasing captured separately in TraceEntry.
    EWISE_ADD,      // add.Tensor / add_.Tensor / add.Scalar
    EWISE_MUL,      // mul.Tensor / mul_.Tensor / mul.Scalar
    EWISE_SUB,      // sub.Tensor / sub_.Tensor
    EWISE_DIV,      // div.Tensor / div_.Tensor
    EWISE_POW,      // pow.Tensor_Tensor / pow.Tensor_Scalar
    EWISE_MAX,      // maximum (elementwise — not reduction)
    EWISE_MIN,      // minimum (elementwise — not reduction)
    EWISE_MOD,      // fmod / remainder
    EWISE_WHERE,    // where.self — ternary broadcast select

    // ── Elementwise Unary (10) ──────────────────────────────────────────────
    EWISE_EXP,      // exp / exp_ / exp2
    EWISE_LOG,      // log / log_ / log2 / log10
    EWISE_SQRT,     // sqrt / sqrt_
    EWISE_RSQRT,    // rsqrt (common in LLMs: 1/sqrt(var+eps))
    EWISE_ABS,      // abs / abs_
    EWISE_NEG,      // neg / neg_
    EWISE_SIGN,     // sign / sgn
    EWISE_FLOOR,    // floor / ceil / round / trunc
    EWISE_CAST,     // to(dtype) — element-type conversion
    EWISE_FILL,     // fill_ / fill_diagonal_ — broadcast scalar into storage

    // ── Reductions (8) ──────────────────────────────────────────────────────
    REDUCE_SUM,     // sum / nansum (optional keepdim, over dim list)
    REDUCE_MEAN,    // mean / nanmean
    REDUCE_MAX,     // max / amax (value only — no index)
    REDUCE_MIN,     // min / amin
    REDUCE_ARGMAX,  // argmax (returns index tensor)
    REDUCE_ARGMIN,  // argmin
    REDUCE_CUMSUM,  // cumsum / cumprod
    REDUCE_TOPK,    // topk (values + indices — used in LM sampling)

    // ── Pooling (8) ─────────────────────────────────────────────────────────
    POOL_MAX1D,     // max_pool1d
    POOL_MAX2D,     // max_pool2d / max_pool2d_with_indices
    POOL_MAX3D,     // max_pool3d
    POOL_AVG1D,     // avg_pool1d
    POOL_AVG2D,     // avg_pool2d
    POOL_AVG3D,     // avg_pool3d
    POOL_ADAPTIVE_MAX, // adaptive_max_pool{1,2,3}d
    POOL_ADAPTIVE_AVG, // adaptive_avg_pool{1,2,3}d

    // ── Data Movement / Indexing (16) ───────────────────────────────────────
    VIEW,           // view — non-copying reshape (same storage, same numel)
    RESHAPE,        // reshape — may copy if non-contiguous
    PERMUTE,        // permute / movedim — arbitrary axis reorder
    TRANSPOSE,      // transpose.int — swap exactly two axes
    CONTIGUOUS,     // contiguous — force C-contiguous layout
    EXPAND,         // expand / expand_as — broadcast via stride-0 (no copy)
    SQUEEZE,        // squeeze / unsqueeze
    SLICE,          // narrow / slice / select — contiguous subview
    INDEX_SELECT,   // index_select / gather — 1-D integer index lookup
    INDEX,          // advanced indexing via tensor indices (non-contiguous)
    SCATTER,        // scatter / scatter_add / scatter_reduce
    MASKED_FILL,    // masked_fill / masked_fill_ (attention mask application)
    PAD,            // pad / constant_pad_nd / reflection_pad_nd
    CAT,            // cat — concatenate along existing dim
    STACK,          // stack — concatenate with new leading/trailing dim
    UNFOLD,         // unfold / as_strided (sliding window patch extraction)

    // ── Embedding (2) ───────────────────────────────────────────────────────
    EMBEDDING,      // embedding (dense lookup table)
    EMBEDDING_BAG,  // embedding_bag (lookup + pooling: mean / sum / max)

    // ── Copy / I/O (2) ──────────────────────────────────────────────────────
    COPY_,          // copy_ (cross-device or cross-dtype in-place copy)
    CLONE,          // clone (always allocates new storage)

    // ── Vision (3) ──────────────────────────────────────────────────────────
    INTERPOLATE,    // upsample / interpolate (bilinear, nearest, bicubic)
    GRID_SAMPLE,    // grid_sample (spatial transformer network)
    IM2COL,         // im2col / col2im / unfold (explicit patch extraction)

    // ── Fused high-level (4) ────────────────────────────────────────────────
    // Registered by Vessel when it recognises a fused kernel being dispatched.
    // These allow Tier 2+ to treat entire transformer sub-graphs as atomic units.
    FUSED_ATTENTION,    // full MHSA block (FlashAttention-2/3 style)
    FUSED_LINEAR_ACT,   // linear + activation gate (SwiGLU, GeGLU, GLU)
    FUSED_NORM_LINEAR,  // pre-norm + linear (T5/LLaMA fused blocks)
    FUSED_SOFTMAX_DROP, // softmax + dropout (attention score path)

    NUM_KERNELS     // sentinel — must be last; value == 100
};

// ── Registration table ──────────────────────────────────────────────────────
//
// Sorted array of (schema_hash, CKernelId) pairs.
// Written once at startup (Vessel registration), then read-only forever.
//
// 256 slots: ~99 canonical ops × up to 2-3 ATen dispatch name aliases each
// (e.g. softmax + _softmax, layer_norm + native_layer_norm, 5 SDPA variants).

static constexpr uint32_t CKERNEL_TABLE_CAP = 256;

struct CKernelEntry {
    uint64_t  schema_hash;
    CKernelId id;
};

struct CKernelTable {
    CKernelEntry entries[CKERNEL_TABLE_CAP]{};
    uint32_t     size{0};

    // Register a schema_hash → CKernelId mapping. Idempotent: re-registering
    // the same hash updates the entry in-place (later registration wins).
    // Silently no-ops beyond CKERNEL_TABLE_CAP (never happens in practice).
    void register_op(uint64_t schema_hash, CKernelId id) {
        // Check for existing entry first (idempotent / alias update).
        for (uint32_t i = 0; i < size; i++) {
            if (entries[i].schema_hash == schema_hash) {
                entries[i].id = id;
                return;
            }
        }
        if (size >= CKERNEL_TABLE_CAP) return;
        entries[size++] = {schema_hash, id};
        // Keep sorted by schema_hash for O(log n) classify().
        std::sort(entries, entries + size,
                  [](const CKernelEntry& a, const CKernelEntry& b) {
                      return a.schema_hash < b.schema_hash;
                  });
    }

    // Binary search: returns CKernelId::OPAQUE if not registered.
    CKernelId classify(uint64_t schema_hash) const {
        uint32_t lo = 0, hi = size;
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            if (entries[mid].schema_hash == schema_hash) return entries[mid].id;
            if (entries[mid].schema_hash < schema_hash) lo = mid + 1;
            else                                        hi = mid;
        }
        return CKernelId::OPAQUE;
    }
};

// Process-wide singleton — magic-static init, thread-safe in C++11.
inline CKernelTable& global_ckernel_table() {
    static CKernelTable table;
    return table;
}

// Called by Vessel at startup, before BackgroundThread::start().
inline void register_schema_hash(uint64_t schema_hash, CKernelId id) {
    global_ckernel_table().register_op(schema_hash, id);
}

// Called by BackgroundThread during build_trace() for each ring entry.
inline CKernelId classify_kernel(uint64_t schema_hash) {
    return global_ckernel_table().classify(schema_hash);
}

// Human-readable name — for logging and introspection only.
inline const char* ckernel_name(CKernelId id) {
    switch (id) {
    case CKernelId::OPAQUE:             return "OPAQUE";
    // Linear Algebra
    case CKernelId::GEMM_MM:            return "GEMM_MM";
    case CKernelId::GEMM_BMM:           return "GEMM_BMM";
    case CKernelId::GEMM_MATMUL:        return "GEMM_MATMUL";
    case CKernelId::GEMM_ADDMM:         return "GEMM_ADDMM";
    case CKernelId::GEMM_LINEAR:        return "GEMM_LINEAR";
    case CKernelId::GEMM_ADDBMM:        return "GEMM_ADDBMM";
    case CKernelId::GEMM_BADDBMM:       return "GEMM_BADDBMM";
    case CKernelId::GEMM_EINSUM:        return "GEMM_EINSUM";
    // Convolution
    case CKernelId::CONV1D:             return "CONV1D";
    case CKernelId::CONV2D:             return "CONV2D";
    case CKernelId::CONV3D:             return "CONV3D";
    case CKernelId::CONV_TRANSPOSE1D:   return "CONV_TRANSPOSE1D";
    case CKernelId::CONV_TRANSPOSE2D:   return "CONV_TRANSPOSE2D";
    case CKernelId::CONV_TRANSPOSE3D:   return "CONV_TRANSPOSE3D";
    // Attention
    case CKernelId::SDPA:               return "SDPA";
    case CKernelId::MHA:                return "MHA";
    case CKernelId::ROPE:               return "ROPE";
    case CKernelId::POSITION_BIAS:      return "POSITION_BIAS";
    // Normalization
    case CKernelId::LAYER_NORM:         return "LAYER_NORM";
    case CKernelId::BATCH_NORM_TRAIN:   return "BATCH_NORM_TRAIN";
    case CKernelId::BATCH_NORM_EVAL:    return "BATCH_NORM_EVAL";
    case CKernelId::GROUP_NORM:         return "GROUP_NORM";
    case CKernelId::INSTANCE_NORM:      return "INSTANCE_NORM";
    case CKernelId::RMS_NORM:           return "RMS_NORM";
    // Activations
    case CKernelId::ACT_RELU:           return "ACT_RELU";
    case CKernelId::ACT_GELU:           return "ACT_GELU";
    case CKernelId::ACT_SILU:           return "ACT_SILU";
    case CKernelId::ACT_SIGMOID:        return "ACT_SIGMOID";
    case CKernelId::ACT_TANH:           return "ACT_TANH";
    case CKernelId::ACT_HARDSWISH:      return "ACT_HARDSWISH";
    case CKernelId::ACT_LEAKY_RELU:     return "ACT_LEAKY_RELU";
    case CKernelId::ACT_ELU:            return "ACT_ELU";
    case CKernelId::ACT_SOFTMAX:        return "ACT_SOFTMAX";
    case CKernelId::ACT_LOG_SOFTMAX:    return "ACT_LOG_SOFTMAX";
    case CKernelId::ACT_DROPOUT:        return "ACT_DROPOUT";
    case CKernelId::ACT_CLAMP:          return "ACT_CLAMP";
    case CKernelId::ACT_MISH:           return "ACT_MISH";
    // Elementwise Binary
    case CKernelId::EWISE_ADD:          return "EWISE_ADD";
    case CKernelId::EWISE_MUL:          return "EWISE_MUL";
    case CKernelId::EWISE_SUB:          return "EWISE_SUB";
    case CKernelId::EWISE_DIV:          return "EWISE_DIV";
    case CKernelId::EWISE_POW:          return "EWISE_POW";
    case CKernelId::EWISE_MAX:          return "EWISE_MAX";
    case CKernelId::EWISE_MIN:          return "EWISE_MIN";
    case CKernelId::EWISE_MOD:          return "EWISE_MOD";
    case CKernelId::EWISE_WHERE:        return "EWISE_WHERE";
    // Elementwise Unary
    case CKernelId::EWISE_EXP:          return "EWISE_EXP";
    case CKernelId::EWISE_LOG:          return "EWISE_LOG";
    case CKernelId::EWISE_SQRT:         return "EWISE_SQRT";
    case CKernelId::EWISE_RSQRT:        return "EWISE_RSQRT";
    case CKernelId::EWISE_ABS:          return "EWISE_ABS";
    case CKernelId::EWISE_NEG:          return "EWISE_NEG";
    case CKernelId::EWISE_SIGN:         return "EWISE_SIGN";
    case CKernelId::EWISE_FLOOR:        return "EWISE_FLOOR";
    case CKernelId::EWISE_CAST:         return "EWISE_CAST";
    case CKernelId::EWISE_FILL:         return "EWISE_FILL";
    // Reductions
    case CKernelId::REDUCE_SUM:         return "REDUCE_SUM";
    case CKernelId::REDUCE_MEAN:        return "REDUCE_MEAN";
    case CKernelId::REDUCE_MAX:         return "REDUCE_MAX";
    case CKernelId::REDUCE_MIN:         return "REDUCE_MIN";
    case CKernelId::REDUCE_ARGMAX:      return "REDUCE_ARGMAX";
    case CKernelId::REDUCE_ARGMIN:      return "REDUCE_ARGMIN";
    case CKernelId::REDUCE_CUMSUM:      return "REDUCE_CUMSUM";
    case CKernelId::REDUCE_TOPK:        return "REDUCE_TOPK";
    // Pooling
    case CKernelId::POOL_MAX1D:         return "POOL_MAX1D";
    case CKernelId::POOL_MAX2D:         return "POOL_MAX2D";
    case CKernelId::POOL_MAX3D:         return "POOL_MAX3D";
    case CKernelId::POOL_AVG1D:         return "POOL_AVG1D";
    case CKernelId::POOL_AVG2D:         return "POOL_AVG2D";
    case CKernelId::POOL_AVG3D:         return "POOL_AVG3D";
    case CKernelId::POOL_ADAPTIVE_MAX:  return "POOL_ADAPTIVE_MAX";
    case CKernelId::POOL_ADAPTIVE_AVG:  return "POOL_ADAPTIVE_AVG";
    // Data Movement
    case CKernelId::VIEW:               return "VIEW";
    case CKernelId::RESHAPE:            return "RESHAPE";
    case CKernelId::PERMUTE:            return "PERMUTE";
    case CKernelId::TRANSPOSE:          return "TRANSPOSE";
    case CKernelId::CONTIGUOUS:         return "CONTIGUOUS";
    case CKernelId::EXPAND:             return "EXPAND";
    case CKernelId::SQUEEZE:            return "SQUEEZE";
    case CKernelId::SLICE:              return "SLICE";
    case CKernelId::INDEX_SELECT:       return "INDEX_SELECT";
    case CKernelId::INDEX:              return "INDEX";
    case CKernelId::SCATTER:            return "SCATTER";
    case CKernelId::MASKED_FILL:        return "MASKED_FILL";
    case CKernelId::PAD:                return "PAD";
    case CKernelId::CAT:                return "CAT";
    case CKernelId::STACK:              return "STACK";
    case CKernelId::UNFOLD:             return "UNFOLD";
    // Embedding
    case CKernelId::EMBEDDING:          return "EMBEDDING";
    case CKernelId::EMBEDDING_BAG:      return "EMBEDDING_BAG";
    // Copy / I/O
    case CKernelId::COPY_:              return "COPY_";
    case CKernelId::CLONE:              return "CLONE";
    // Vision
    case CKernelId::INTERPOLATE:        return "INTERPOLATE";
    case CKernelId::GRID_SAMPLE:        return "GRID_SAMPLE";
    case CKernelId::IM2COL:             return "IM2COL";
    // Fused
    case CKernelId::FUSED_ATTENTION:    return "FUSED_ATTENTION";
    case CKernelId::FUSED_LINEAR_ACT:   return "FUSED_LINEAR_ACT";
    case CKernelId::FUSED_NORM_LINEAR:  return "FUSED_NORM_LINEAR";
    case CKernelId::FUSED_SOFTMAX_DROP: return "FUSED_SOFTMAX_DROP";
    default:                            return "<unknown>";
    }
}

} // namespace crucible
