#pragma once

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/_Post.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <type_traits>

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
// makes the widening below total.
using ValidCKernelIdRaw = ::crucible::fixy::wrap::Refined<
    ::crucible::fixy::wrap::bounded_above<static_cast<uint8_t>(CKernelId::NUM_KERNELS) - uint8_t{1}>, uint8_t>;

[[nodiscard, gnu::const]] inline constexpr CKernelId make_ckernel_id(ValidCKernelIdRaw raw) noexcept {
    return static_cast<CKernelId>(raw.value());
}

// Headroom over the kernel count: one operation can carry several aliases, so
// the number of registrations exceeds the number of identifiers.
static constexpr uint32_t CKERNEL_TABLE_CAP = 256;

struct CKernelEntry {
    SchemaHash schema_hash;
    CKernelId id;
};

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(CKernelEntry);

namespace ckernel_state {
struct Mutable {};
struct Sealed {};
}  // namespace ckernel_state

struct CKernelTable {
    using SizeCounter = ::crucible::fixy::wrap::BoundedMonotonic<uint32_t, CKERNEL_TABLE_CAP>;

    CKernelEntry entries[CKERNEL_TABLE_CAP]{};
    SizeCounter size{0u};

    // The release store in seal() pairs with the acquire load in is_sealed():
    // a reader that observes the sealed state also observes every entry
    // registered before the seal.
    std::atomic<bool> sealed_{false};

    CKernelTable() = default;

    CKernelTable(const CKernelTable&) = delete("table is a global registration singleton; no copies");
    CKernelTable& operator=(const CKernelTable&) = delete("table is a global registration singleton; no copies");
    CKernelTable(CKernelTable&&) = delete("table is a global registration singleton; no moves");
    CKernelTable& operator=(CKernelTable&&) = delete("table is a global registration singleton; no moves");

    void seal() noexcept {
        sealed_.store(true, std::memory_order_release);
        CRUCIBLE_POST(0, sealed_.load(std::memory_order_acquire));
    }

    [[nodiscard]] bool is_sealed() const noexcept { return sealed_.load(std::memory_order_acquire); }

    using MutableView = crucible::fixy::wrap::ScopedView<CKernelTable, ckernel_state::Mutable>;
    using SealedView = crucible::fixy::wrap::ScopedView<CKernelTable, ckernel_state::Sealed>;

    // A contract predicate that reads a member through `this` is skipped when
    // the compiler folds the body at compile time, so every such check in this
    // file runs from the body instead of a pre/post clause.
    [[nodiscard]] MutableView mint_mutable_view() const noexcept {
        CRUCIBLE_PRE(!is_sealed());
        return crucible::fixy::wrap::mint_view<ckernel_state::Mutable>(*this);
    }

    [[nodiscard]] SealedView mint_sealed_view() const noexcept {
        CRUCIBLE_PRE(is_sealed());
        return crucible::fixy::wrap::mint_view<ckernel_state::Sealed>(*this);
    }

    // Found by argument-dependent lookup from mint_view.
    [[nodiscard]] friend constexpr bool view_ok(CKernelTable const& t,
                                                std::type_identity<ckernel_state::Mutable>) noexcept {
        return !t.is_sealed();
    }
    [[nodiscard]] friend constexpr bool view_ok(CKernelTable const& t,
                                                std::type_identity<ckernel_state::Sealed>) noexcept {
        return t.is_sealed();
    }

    // Overflow aborts rather than truncating. A dropped registration would
    // leave classify() answering OPAQUE for that one schema, and that only
    // shows up much later, at replay, on whichever trace happens to use it.
    void register_op(MutableView const&, SchemaHash schema_hash, CKernelId id) {
        for (uint32_t i = 0; i < size.get(); i++) {
            if (entries[i].schema_hash == schema_hash) {
                entries[i].id = id;
                CRUCIBLE_POST(0, classify(schema_hash) == id);
                return;
            }
        }
        if (size.get() >= CKERNEL_TABLE_CAP) [[unlikely]] {
            std::fprintf(stderr,
                         "crucible: CKernelTable full (%u/%u entries); bump "
                         "CKERNEL_TABLE_CAP or audit Vessel schema registrations\n",
                         size.get(), CKERNEL_TABLE_CAP);
            std::abort();
        }
        entries[size.get()] = {.schema_hash = schema_hash, .id = id};
        size.bump();
        std::ranges::sort(std::span{entries, size.get()}, {}, &CKernelEntry::schema_hash);
        CRUCIBLE_POST(0, classify(schema_hash) == id);
        CRUCIBLE_POST(0, size.get() <= CKERNEL_TABLE_CAP);
    }

    // gnu::pure lets the optimiser cache a result across calls. That is sound
    // only because registration finishes and the table is sealed before any
    // classifying reader runs. An interleaved registration would invalidate it.
    [[nodiscard, gnu::pure]] CKernelId classify(SchemaHash schema_hash) const noexcept {
        uint32_t lo = 0, hi = size.get();
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            if (entries[mid].schema_hash == schema_hash) return entries[mid].id;
            if (entries[mid].schema_hash < schema_hash)
                lo = mid + 1;
            else
                hi = mid;
        }
        return CKernelId::OPAQUE;
    }

    [[nodiscard]] uint32_t count() const noexcept { return size.get(); }

    // Rewinding the counter is the one non-monotonic mutation the type
    // otherwise forbids, so the counter is reconstructed in place rather than
    // assigned, re-establishing its bound and its ordering from a known floor.
    void clear() noexcept {
        std::construct_at(&size, SizeCounter{0u});
        sealed_.store(false, std::memory_order_release);
        CRUCIBLE_POST(0, size.get() == 0u);
        CRUCIBLE_POST(0, !is_sealed());
    }
};

static_assert(crucible::fixy::wrap::no_scoped_view_field_check<CKernelTable>());

using CKernelTableSingleton = crucible::fixy::wrap::Tagged<CKernelTable*, crucible::fixy::tags::source::Singleton>;
static_assert(sizeof(CKernelTableSingleton) == sizeof(CKernelTable*));

[[nodiscard]] inline CKernelTableSingleton global_ckernel_table() {
    static CKernelTable table;
    return CKernelTableSingleton{&table};
}

inline void
register_schema_hash(crucible::fixy::wrap::Tagged<SchemaHash, crucible::fixy::tags::source::External> schema_hash,
                     CKernelId id) {
    CKernelTable* table = global_ckernel_table().value();
    table->register_op(table->mint_mutable_view(), schema_hash.value(), id);
}

// ---------------------------------------------------------------------
// The compile-time classification a framework adapter owns.
//
// The table above is filled at run time, one register_op call per schema,
// and it is capped at CKERNEL_TABLE_CAP with an abort on overflow. That cap
// is sized for the schemas an adapter discovers as it runs. It is not sized
// for a framework's whole operator set: the PyTorch adapter's generated
// table classifies 344 operators, so registering them would abort before
// the first iteration completed.
//
// An adapter that knows its operator set at compile time therefore does not
// register it. It publishes a sorted, immutable array once, and this file
// reads that array before the run-time table. No cap, no abort, and the cap
// above keeps the meaning it was written with.
//
// The array stays with the adapter rather than moving here. A schema hash of
// an ATen operator is PyTorch's fact, and this header is the vendor-neutral
// taxonomy every adapter maps onto; carrying one framework's 344 hashes here
// would put a framework's operator set below the layer that abstracts it.

struct StaticCKernelTable {
    // Sorted by schema_hash, so classify_static below can bisect it.
    const CKernelEntry* entries = nullptr;
    uint32_t count = 0;
};

namespace detail {
// Constant-initialized, so it has no dynamic initializer to order against
// the adapter that publishes into it and no guard variable to race on.
inline constinit std::atomic<const StaticCKernelTable*> static_ckernel_table_{nullptr};
}  // namespace detail

// Publishing is a release store, and classify_static reads with acquire, so a
// reader that observes the pointer also observes every entry the adapter
// wrote before it. The adapter publishes from its load-time initializer,
// before the background thread starts draining.
//
// The array must outlive the process's classifying readers. A `constexpr`
// array with static storage duration satisfies that; a local does not.
inline void publish_static_ckernel_table(const StaticCKernelTable& table CRUCIBLE_LIFETIMEBOUND) noexcept {
    CRUCIBLE_PRE(table.entries != nullptr || table.count == 0u);
    detail::static_ckernel_table_.store(&table, std::memory_order_release);
}

// OPAQUE when no adapter has published, which is every build that links no
// framework adapter, and OPAQUE when the published array does not name this
// schema. Both answers are the same answer the run-time table gives on a
// miss, so the fallback below is reached in exactly those two cases.
[[nodiscard, gnu::pure]] inline CKernelId classify_static(SchemaHash schema_hash) noexcept {
    const StaticCKernelTable* table = detail::static_ckernel_table_.load(std::memory_order_acquire);
    if (table == nullptr) [[unlikely]]
        return CKernelId::OPAQUE;
    uint32_t lo = 0, hi = table->count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (table->entries[mid].schema_hash == schema_hash) return table->entries[mid].id;
        if (table->entries[mid].schema_hash < schema_hash)
            lo = mid + 1;
        else
            hi = mid;
    }
    return CKernelId::OPAQUE;
}

// The published array first, the run-time table second. An adapter that
// publishes a classification for a schema and also registers one for it gets
// the published answer, which is the one its own generator derived.
[[nodiscard, gnu::pure]] inline CKernelId classify_kernel(SchemaHash schema_hash) noexcept {
    const CKernelId from_static = classify_static(schema_hash);
    if (from_static != CKernelId::OPAQUE) return from_static;
    return global_ckernel_table().value()->classify(schema_hash);
}

[[nodiscard]] constexpr const char* ckernel_name(CKernelId id) {
    switch (id) {
        case CKernelId::OPAQUE:
            return "OPAQUE";

        case CKernelId::GEMM_MM:
            return "GEMM_MM";
        case CKernelId::GEMM_BMM:
            return "GEMM_BMM";
        case CKernelId::GEMM_MATMUL:
            return "GEMM_MATMUL";
        case CKernelId::GEMM_ADDMM:
            return "GEMM_ADDMM";
        case CKernelId::GEMM_LINEAR:
            return "GEMM_LINEAR";
        case CKernelId::GEMM_ADDBMM:
            return "GEMM_ADDBMM";
        case CKernelId::GEMM_BADDBMM:
            return "GEMM_BADDBMM";
        case CKernelId::GEMM_EINSUM:
            return "GEMM_EINSUM";
        case CKernelId::CONV1D:
            return "CONV1D";
        case CKernelId::CONV2D:
            return "CONV2D";
        case CKernelId::CONV3D:
            return "CONV3D";
        case CKernelId::CONV_TRANSPOSE1D:
            return "CONV_TRANSPOSE1D";
        case CKernelId::CONV_TRANSPOSE2D:
            return "CONV_TRANSPOSE2D";
        case CKernelId::CONV_TRANSPOSE3D:
            return "CONV_TRANSPOSE3D";
        case CKernelId::SDPA:
            return "SDPA";
        case CKernelId::MHA:
            return "MHA";
        case CKernelId::ROPE:
            return "ROPE";
        case CKernelId::POSITION_BIAS:
            return "POSITION_BIAS";
        case CKernelId::LAYER_NORM:
            return "LAYER_NORM";
        case CKernelId::BATCH_NORM_TRAIN:
            return "BATCH_NORM_TRAIN";
        case CKernelId::BATCH_NORM_EVAL:
            return "BATCH_NORM_EVAL";
        case CKernelId::GROUP_NORM:
            return "GROUP_NORM";
        case CKernelId::INSTANCE_NORM:
            return "INSTANCE_NORM";
        case CKernelId::RMS_NORM:
            return "RMS_NORM";
        case CKernelId::ACT_RELU:
            return "ACT_RELU";
        case CKernelId::ACT_GELU:
            return "ACT_GELU";
        case CKernelId::ACT_SILU:
            return "ACT_SILU";
        case CKernelId::ACT_SIGMOID:
            return "ACT_SIGMOID";
        case CKernelId::ACT_TANH:
            return "ACT_TANH";
        case CKernelId::ACT_HARDSWISH:
            return "ACT_HARDSWISH";
        case CKernelId::ACT_LEAKY_RELU:
            return "ACT_LEAKY_RELU";
        case CKernelId::ACT_ELU:
            return "ACT_ELU";
        case CKernelId::ACT_SOFTMAX:
            return "ACT_SOFTMAX";
        case CKernelId::ACT_LOG_SOFTMAX:
            return "ACT_LOG_SOFTMAX";
        case CKernelId::ACT_DROPOUT:
            return "ACT_DROPOUT";
        case CKernelId::ACT_CLAMP:
            return "ACT_CLAMP";
        case CKernelId::ACT_MISH:
            return "ACT_MISH";
        case CKernelId::EWISE_ADD:
            return "EWISE_ADD";
        case CKernelId::EWISE_MUL:
            return "EWISE_MUL";
        case CKernelId::EWISE_SUB:
            return "EWISE_SUB";
        case CKernelId::EWISE_DIV:
            return "EWISE_DIV";
        case CKernelId::EWISE_POW:
            return "EWISE_POW";
        case CKernelId::EWISE_MAX:
            return "EWISE_MAX";
        case CKernelId::EWISE_MIN:
            return "EWISE_MIN";
        case CKernelId::EWISE_MOD:
            return "EWISE_MOD";
        case CKernelId::EWISE_WHERE:
            return "EWISE_WHERE";
        case CKernelId::EWISE_EXP:
            return "EWISE_EXP";
        case CKernelId::EWISE_LOG:
            return "EWISE_LOG";
        case CKernelId::EWISE_SQRT:
            return "EWISE_SQRT";
        case CKernelId::EWISE_RSQRT:
            return "EWISE_RSQRT";
        case CKernelId::EWISE_ABS:
            return "EWISE_ABS";
        case CKernelId::EWISE_NEG:
            return "EWISE_NEG";
        case CKernelId::EWISE_SIGN:
            return "EWISE_SIGN";
        case CKernelId::EWISE_FLOOR:
            return "EWISE_FLOOR";
        case CKernelId::EWISE_CAST:
            return "EWISE_CAST";
        case CKernelId::EWISE_FILL:
            return "EWISE_FILL";
        case CKernelId::REDUCE_SUM:
            return "REDUCE_SUM";
        case CKernelId::REDUCE_MEAN:
            return "REDUCE_MEAN";
        case CKernelId::REDUCE_MAX:
            return "REDUCE_MAX";
        case CKernelId::REDUCE_MIN:
            return "REDUCE_MIN";
        case CKernelId::REDUCE_ARGMAX:
            return "REDUCE_ARGMAX";
        case CKernelId::REDUCE_ARGMIN:
            return "REDUCE_ARGMIN";
        case CKernelId::REDUCE_CUMSUM:
            return "REDUCE_CUMSUM";
        case CKernelId::REDUCE_TOPK:
            return "REDUCE_TOPK";
        case CKernelId::POOL_MAX1D:
            return "POOL_MAX1D";
        case CKernelId::POOL_MAX2D:
            return "POOL_MAX2D";
        case CKernelId::POOL_MAX3D:
            return "POOL_MAX3D";
        case CKernelId::POOL_AVG1D:
            return "POOL_AVG1D";
        case CKernelId::POOL_AVG2D:
            return "POOL_AVG2D";
        case CKernelId::POOL_AVG3D:
            return "POOL_AVG3D";
        case CKernelId::POOL_ADAPTIVE_MAX:
            return "POOL_ADAPTIVE_MAX";
        case CKernelId::POOL_ADAPTIVE_AVG:
            return "POOL_ADAPTIVE_AVG";
        case CKernelId::VIEW:
            return "VIEW";
        case CKernelId::RESHAPE:
            return "RESHAPE";
        case CKernelId::PERMUTE:
            return "PERMUTE";
        case CKernelId::TRANSPOSE:
            return "TRANSPOSE";
        case CKernelId::CONTIGUOUS:
            return "CONTIGUOUS";
        case CKernelId::EXPAND:
            return "EXPAND";
        case CKernelId::SQUEEZE:
            return "SQUEEZE";
        case CKernelId::SLICE:
            return "SLICE";
        case CKernelId::INDEX_SELECT:
            return "INDEX_SELECT";
        case CKernelId::INDEX:
            return "INDEX";
        case CKernelId::SCATTER:
            return "SCATTER";
        case CKernelId::MASKED_FILL:
            return "MASKED_FILL";
        case CKernelId::PAD:
            return "PAD";
        case CKernelId::CAT:
            return "CAT";
        case CKernelId::STACK:
            return "STACK";
        case CKernelId::UNFOLD:
            return "UNFOLD";
        case CKernelId::EMBEDDING:
            return "EMBEDDING";
        case CKernelId::EMBEDDING_BAG:
            return "EMBEDDING_BAG";
        case CKernelId::COPY_:
            return "COPY_";
        case CKernelId::CLONE:
            return "CLONE";
        case CKernelId::INTERPOLATE:
            return "INTERPOLATE";
        case CKernelId::GRID_SAMPLE:
            return "GRID_SAMPLE";
        case CKernelId::IM2COL:
            return "IM2COL";
        case CKernelId::FUSED_ATTENTION:
            return "FUSED_ATTENTION";
        case CKernelId::FUSED_LINEAR_ACT:
            return "FUSED_LINEAR_ACT";
        case CKernelId::FUSED_NORM_LINEAR:
            return "FUSED_NORM_LINEAR";
        case CKernelId::FUSED_SOFTMAX_DROP:
            return "FUSED_SOFTMAX_DROP";

        case CKernelId::LINALG_SVD:
            return "LINALG_SVD";
        case CKernelId::LINALG_CHOLESKY:
            return "LINALG_CHOLESKY";
        case CKernelId::LINALG_QR:
            return "LINALG_QR";
        case CKernelId::LINALG_SOLVE:
            return "LINALG_SOLVE";
        case CKernelId::LINALG_EIGH:
            return "LINALG_EIGH";
        case CKernelId::LINALG_NORM:
            return "LINALG_NORM";
        case CKernelId::LINALG_CROSS:
            return "LINALG_CROSS";
        case CKernelId::CDIST:
            return "CDIST";
        case CKernelId::FFT:
            return "FFT";
        case CKernelId::ASSOC_SCAN:
            return "ASSOC_SCAN";
        case CKernelId::SELECTIVE_SCAN:
            return "SELECTIVE_SCAN";
        case CKernelId::SSD_CHUNK:
            return "SSD_CHUNK";
        case CKernelId::WKV_RECURRENCE:
            return "WKV_RECURRENCE";
        case CKernelId::RETENTION:
            return "RETENTION";
        case CKernelId::MLSTM_RECURRENCE:
            return "MLSTM_RECURRENCE";
        case CKernelId::DEQUANT_GEMM:
            return "DEQUANT_GEMM";
        case CKernelId::MOE_ROUTE_GEMM:
            return "MOE_ROUTE_GEMM";
        case CKernelId::PAGED_ATTENTION:
            return "PAGED_ATTENTION";
        case CKernelId::FUSED_CROSS_ENTROPY:
            return "FUSED_CROSS_ENTROPY";
        case CKernelId::LINEAR_ATTN_CAUSAL:
            return "LINEAR_ATTN_CAUSAL";
        case CKernelId::RAGGED_ATTN:
            return "RAGGED_ATTN";
        case CKernelId::GAUSSIAN_RASTERIZE:
            return "GAUSSIAN_RASTERIZE";
        case CKernelId::HASH_GRID_ENCODE:
            return "HASH_GRID_ENCODE";
        case CKernelId::VOLUME_RENDER:
            return "VOLUME_RENDER";
        case CKernelId::SH_EVAL:
            return "SH_EVAL";
        case CKernelId::FFT_CONV:
            return "FFT_CONV";
        case CKernelId::MONARCH_MATMUL:
            return "MONARCH_MATMUL";
        case CKernelId::SPMM_GNN:
            return "SPMM_GNN";
        case CKernelId::SDDMM_GNN:
            return "SDDMM_GNN";
        case CKernelId::SINKHORN:
            return "SINKHORN";
        case CKernelId::COMM_ALLREDUCE:
            return "COMM_ALLREDUCE";
        case CKernelId::COMM_ALLGATHER:
            return "COMM_ALLGATHER";
        case CKernelId::COMM_REDUCE_SCATTER:
            return "COMM_REDUCE_SCATTER";
        case CKernelId::COMM_BROADCAST:
            return "COMM_BROADCAST";
        case CKernelId::COMM_ALL_TO_ALL:
            return "COMM_ALL_TO_ALL";
        case CKernelId::COMM_SEND:
            return "COMM_SEND";
        case CKernelId::COMM_RECV:
            return "COMM_RECV";
        case CKernelId::COMM_REDUCE:
            return "COMM_REDUCE";
        case CKernelId::COMM_GATHER:
            return "COMM_GATHER";
        case CKernelId::COMM_SCATTER:
            return "COMM_SCATTER";
        case CKernelId::IO_LOAD:
            return "IO_LOAD";
        case CKernelId::IO_PREFETCH:
            return "IO_PREFETCH";
        case CKernelId::IO_CHECKPOINT_SAVE:
            return "IO_CHECKPOINT_SAVE";
        case CKernelId::IO_CHECKPOINT_LOAD:
            return "IO_CHECKPOINT_LOAD";
        case CKernelId::RNG_UNIFORM:
            return "RNG_UNIFORM";
        case CKernelId::RNG_NORMAL:
            return "RNG_NORMAL";
        case CKernelId::COMM_BARRIER:
            return "COMM_BARRIER";

        case CKernelId::NUM_KERNELS:
            std::unreachable();
        default:
            return "<unknown>";
    }
}

}  // namespace crucible
