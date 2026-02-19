#pragma once

// CKernel: Crucible compute-op taxonomy.
//
// Maps Vessel op identity (schema_hash, an opaque uint64) to a Crucible-native
// CKernelId. Used by the background thread to annotate TraceEntry at build time
// so Tier 2+ replay can dispatch directly without going through the Vessel.
//
// Coverage: ~40 ops that account for ~95% of transformer/vision compute.
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
// fields are already correct before classification.

enum class CKernelId : uint8_t {
    OPAQUE = 0,     // Unknown op — use Vessel dispatch (always correct)

    // ── Matrix multiply family ────────────────────────────────────────────
    // Hot path for every linear layer. Tier 2: cuBLAS GEMM direct call.
    GEMM_MM,        // aten::mm            — 2D × 2D, no bias
    GEMM_BMM,       // aten::bmm           — batched 3D × 3D
    GEMM_MATMUL,    // aten::matmul        — general rank, broadcasts
    GEMM_ADDMM,     // aten::addmm         — bias + mat1@mat2 (Linear hot path)
    GEMM_LINEAR,    // aten::linear        — weight + optional bias wrapper
    GEMM_ADDBMM,    // aten::addbmm        — batched addmm with alpha/beta
    GEMM_BADDBMM,   // aten::baddbmm       — batched bias + batched matmul

    // ── Convolution ───────────────────────────────────────────────────────
    CONV1D,         // aten::conv1d
    CONV2D,         // aten::conv2d
    CONV3D,         // aten::conv3d
    CONV_TRANSPOSE2D, // aten::conv_transpose2d

    // ── Attention ─────────────────────────────────────────────────────────
    // Tier 2: FlashAttention / cuDNN SDPA direct call.
    SDPA,           // aten::scaled_dot_product_attention

    // ── Normalization ─────────────────────────────────────────────────────
    LAYER_NORM,     // aten::layer_norm
    BATCH_NORM,     // aten::batch_norm
    GROUP_NORM,     // aten::group_norm
    RMS_NORM,       // aten::rms_norm (custom extension in some models)

    // ── Activation functions ──────────────────────────────────────────────
    ACT_RELU,       // aten::relu / aten::relu_
    ACT_GELU,       // aten::gelu
    ACT_SILU,       // aten::silu / aten::silu_
    ACT_SIGMOID,    // aten::sigmoid
    ACT_TANH,       // aten::tanh
    ACT_SOFTMAX,    // aten::softmax
    ACT_LOG_SOFTMAX,// aten::log_softmax
    ACT_DROPOUT,    // aten::dropout (training: mask; inference: identity)

    // ── Elementwise binary ────────────────────────────────────────────────
    EWISE_ADD,      // aten::add.Tensor
    EWISE_ADD_,     // aten::add_.Tensor    (in-place; important for residuals)
    EWISE_MUL,      // aten::mul.Tensor
    EWISE_MUL_,     // aten::mul_.Tensor    (in-place)
    EWISE_SUB,      // aten::sub.Tensor
    EWISE_DIV,      // aten::div.Tensor
    EWISE_EXP,      // aten::exp
    EWISE_LOG,      // aten::log
    EWISE_SQRT,     // aten::sqrt

    // ── Reduction ─────────────────────────────────────────────────────────
    REDUCE_SUM,     // aten::sum
    REDUCE_MEAN,    // aten::mean
    REDUCE_MAX,     // aten::max / aten::amax

    // ── Embedding ─────────────────────────────────────────────────────────
    EMBEDDING,      // aten::embedding

    // ── Data movement / layout ────────────────────────────────────────────
    // These are cheap to dispatch but common enough to want fast path.
    VIEW,           // aten::view
    RESHAPE,        // aten::reshape
    PERMUTE,        // aten::permute
    TRANSPOSE,      // aten::transpose.int
    CONTIGUOUS,     // aten::contiguous
    CLONE,          // aten::clone
    COPY_,          // aten::copy_  (in-place copy)
    CAT,            // aten::cat
    STACK,          // aten::stack
    MASKED_FILL_,   // aten::masked_fill_  (attention mask application)

    NUM_KERNELS     // sentinel — must be last
};

// ── Registration table ──────────────────────────────────────────────────────
//
// Sorted array of (schema_hash, CKernelId) pairs.
// Written once at startup (Vessel registration), then read-only forever.
// 64 slots is generous — we have ~45 known ops.

static constexpr uint32_t CKERNEL_TABLE_CAP = 64;

struct CKernelEntry {
    uint64_t  schema_hash;
    CKernelId id;
};

struct CKernelTable {
    CKernelEntry entries[CKERNEL_TABLE_CAP]{};
    uint32_t     size{0};

    // Register a schema_hash → CKernelId mapping.
    // Silently drops entries beyond CKERNEL_TABLE_CAP (never happens in practice).
    void register_op(uint64_t schema_hash, CKernelId id) {
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
    case CKernelId::OPAQUE:         return "OPAQUE";
    case CKernelId::GEMM_MM:        return "GEMM_MM";
    case CKernelId::GEMM_BMM:       return "GEMM_BMM";
    case CKernelId::GEMM_MATMUL:    return "GEMM_MATMUL";
    case CKernelId::GEMM_ADDMM:     return "GEMM_ADDMM";
    case CKernelId::GEMM_LINEAR:    return "GEMM_LINEAR";
    case CKernelId::GEMM_ADDBMM:    return "GEMM_ADDBMM";
    case CKernelId::GEMM_BADDBMM:   return "GEMM_BADDBMM";
    case CKernelId::CONV1D:         return "CONV1D";
    case CKernelId::CONV2D:         return "CONV2D";
    case CKernelId::CONV3D:         return "CONV3D";
    case CKernelId::CONV_TRANSPOSE2D: return "CONV_TRANSPOSE2D";
    case CKernelId::SDPA:           return "SDPA";
    case CKernelId::LAYER_NORM:     return "LAYER_NORM";
    case CKernelId::BATCH_NORM:     return "BATCH_NORM";
    case CKernelId::GROUP_NORM:     return "GROUP_NORM";
    case CKernelId::RMS_NORM:       return "RMS_NORM";
    case CKernelId::ACT_RELU:       return "ACT_RELU";
    case CKernelId::ACT_GELU:       return "ACT_GELU";
    case CKernelId::ACT_SILU:       return "ACT_SILU";
    case CKernelId::ACT_SIGMOID:    return "ACT_SIGMOID";
    case CKernelId::ACT_TANH:       return "ACT_TANH";
    case CKernelId::ACT_SOFTMAX:    return "ACT_SOFTMAX";
    case CKernelId::ACT_LOG_SOFTMAX: return "ACT_LOG_SOFTMAX";
    case CKernelId::ACT_DROPOUT:    return "ACT_DROPOUT";
    case CKernelId::EWISE_ADD:      return "EWISE_ADD";
    case CKernelId::EWISE_ADD_:     return "EWISE_ADD_";
    case CKernelId::EWISE_MUL:      return "EWISE_MUL";
    case CKernelId::EWISE_MUL_:     return "EWISE_MUL_";
    case CKernelId::EWISE_SUB:      return "EWISE_SUB";
    case CKernelId::EWISE_DIV:      return "EWISE_DIV";
    case CKernelId::EWISE_EXP:      return "EWISE_EXP";
    case CKernelId::EWISE_LOG:      return "EWISE_LOG";
    case CKernelId::EWISE_SQRT:     return "EWISE_SQRT";
    case CKernelId::REDUCE_SUM:     return "REDUCE_SUM";
    case CKernelId::REDUCE_MEAN:    return "REDUCE_MEAN";
    case CKernelId::REDUCE_MAX:     return "REDUCE_MAX";
    case CKernelId::EMBEDDING:      return "EMBEDDING";
    case CKernelId::VIEW:           return "VIEW";
    case CKernelId::RESHAPE:        return "RESHAPE";
    case CKernelId::PERMUTE:        return "PERMUTE";
    case CKernelId::TRANSPOSE:      return "TRANSPOSE";
    case CKernelId::CONTIGUOUS:     return "CONTIGUOUS";
    case CKernelId::CLONE:          return "CLONE";
    case CKernelId::COPY_:          return "COPY_";
    case CKernelId::CAT:            return "CAT";
    case CKernelId::STACK:          return "STACK";
    case CKernelId::MASKED_FILL_:   return "MASKED_FILL_";
    default:                        return "<unknown>";
    }
}

} // namespace crucible
