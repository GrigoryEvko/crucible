#pragma once

// The kernel registration table, the classification of a schema and the name
// of each kernel identifier.  The enum CKernelId is in crucible/CKernelId.h.

#include <crucible/CKernelId.h>
#include <crucible/ForegroundCtx.h>
#include <crucible/RegistrationSeal.h>
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/ScopedView.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible {

// Headroom over the kernel count: one operation can carry several aliases, so
// the number of registrations exceeds the number of identifiers.
static constexpr uint32_t CKERNEL_TABLE_CAP = 256;

struct CKernelEntry {
    SchemaHash schema_hash;
    CKernelId id;
    // pad0, pad1 and pad2 hold the seven bytes after the identifier, so the
    // entry has no padding byte.  With -ftrivial-auto-var-init=zero, GCC
    // writes zero to each padding hole of each entry of an automatic table,
    // one store for each hole.  test/test_ckernel.cpp makes sure that the
    // entry has no padding byte.
    uint8_t pad0 = 0;
    uint16_t pad1 = 0;
    uint32_t pad2 = 0;
};

namespace detail {

// The position of the first entry whose schema hash is not below schema_hash
// in entries, which the schema hashes sort.  Complexity: O(log n).
[[nodiscard]] constexpr std::size_t kernel_position_(::fixy::View<const CKernelEntry> entries,
                                                     SchemaHash schema_hash) noexcept {
    return ::fixy::find_lower_bound_by_key(
        entries, [](const CKernelEntry& entry) noexcept { return entry.schema_hash.raw(); }, schema_hash.raw());
}

// The identifier of the entry of schema_hash in entries, which the schema
// hashes sort, or OPAQUE when no entry holds that hash.  The run-time table
// and the published table use this one lookup.  Complexity: O(log n).
[[nodiscard]] constexpr CKernelId find_kernel_id_(::fixy::View<const CKernelEntry> entries,
                                                  SchemaHash schema_hash) noexcept {
    return entries.get(kernel_position_(entries, schema_hash))
        .map([schema_hash](auto entry) noexcept {
            return entry->schema_hash == schema_hash ? entry->id : CKernelId::OPAQUE;
        })
        .value_or(CKernelId::OPAQUE);
}

}  // namespace detail

namespace ckernel_state {
struct Mutable {};
struct Sealed {};
}  // namespace ckernel_state

class CKernelTable;

// The phase gates of the two view doors of the table, found by
// argument-dependent lookup from mint_view.  They are free functions, not
// friends: the rule proof-routes refuses a friend function of a class that
// states synchronized{}, and is_sealed is public.  The definitions follow
// the class.
[[nodiscard]] inline bool view_ok(CKernelTable const& table, std::type_identity<ckernel_state::Mutable>) noexcept;
[[nodiscard]] inline bool view_ok(CKernelTable const& table, std::type_identity<ckernel_state::Sealed>) noexcept;

// The entries, the count and the phase are private.  A public field is a
// write that no view and no seal can refuse.
//
// Threads use one table at the same time: the writers, one at a time in the
// write section, the seal, and the classifying readers after the seal.
// register_op takes the write section (crucible/RegistrationSeal.h), clear
// takes the proof of a reopen, and the seal and each read are const doors.
// So the class states synchronized{} (foundation/Transfer.h).
class[[= ::foundation::transfer::synchronized{}]] CKernelTable {
public:
    CKernelTable() = default;

    CKernelTable(const CKernelTable&) = delete("table is a global registration singleton; no copies");
    CKernelTable& operator=(const CKernelTable&) = delete("table is a global registration singleton; no copies");
    CKernelTable(CKernelTable&&) = delete("table is a global registration singleton; no moves");
    CKernelTable& operator=(CKernelTable&&) = delete("table is a global registration singleton; no moves");

    // Waits for a registration that is in progress, then seals.  A reader
    // that observes the seal observes every registered entry
    // (crucible/RegistrationSeal.h gives the ordering argument).  The wait
    // has no exit while the writer stays in the section, so the context owns
    // Div.
    template <typename Ctx>
        requires CtxMaySpinForSeal<Ctx>
    void seal(Ctx const& ctx) const noexcept {
        seal_.seal(ctx);
        CRUCIBLE_POST(0, seal_.is_sealed());
    }

    // Not constexpr: the phase is an atomic, and no constant evaluation can
    // read it.  The same holds for every function below that calls it.
    [[nodiscard]] bool is_sealed() const noexcept { return seal_.is_sealed(); }

    // The two view doors below give their views on the erased identity
    // through erase_brand, so the table gives one view type for each state.
    // The brand of mint_view names the door and not the caller, so each table
    // gets the same brand, and the rule brand-escape refuses a fresh brand
    // that leaves the door.  A brand for each table waits for the brand of an
    // arena or a store.
    using MutableView = ::fixy::ScopedView<CKernelTable, ckernel_state::Mutable>;
    using SealedView = ::fixy::ScopedView<CKernelTable, ckernel_state::Sealed>;

    // A contract predicate that reads a member through `this` is skipped when
    // the compiler folds the body at compile time, so every such check in this
    // file runs from the body instead of a pre/post clause.
    // A registration writes entries that a classifying reader reads with no
    // lock once the table is sealed.  So the view is minted only before the
    // seal.  Past the seal it is empty, in every build mode.  The schemas come
    // from the ops that a Vigil records, so the view asks for the context of
    // a Vigil's producer claim.  The sealed view is for readers, and a
    // classifying reader on the background thread holds no such claim.  A
    // registration is cold, so the view also checks the calling thread at
    // run time.
    [[nodiscard]] ::fixy::Option<MutableView> mint_mutable_view(VigilFgCtx const& fg) const noexcept {
        ::foundation::effects::host::require_brand_thread(fg);
        if (is_sealed()) return ::fixy::none;
        return ::fixy::Option<MutableView>::some(::fixy::erase_brand(::fixy::mint_view<ckernel_state::Mutable>(*this)));
    }

    [[nodiscard]] SealedView mint_sealed_view() const noexcept {
        CRUCIBLE_PRE(is_sealed());
        return ::fixy::erase_brand(::fixy::mint_view<ckernel_state::Sealed>(*this));
    }

    // Takes the write section of this table, or gives no value when the
    // table is sealed (crucible/RegistrationSeal.h).  The context of the
    // writer picks the entry: one compare-exchange for the writer of a
    // Vigil's producer claim, and a spin for a context that owns Div.
    //
    // A claim door (the doors of a synchronized class of
    // foundation/Transfer.h): the compare-exchange of the seal takes the
    // phase from Open to Writing and makes the section.  The writes of the
    // entries happen only through register_op, which takes the section, so
    // only on the thread that holds it, until the section ends with the
    // release store.
    template <typename Ctx>
        requires CtxMayEnterWriteSection<Ctx>
    [[nodiscard]] ::fixy::Option<WriteSection<CKernelTable>> try_enter_write_section(Ctx const& writer) noexcept {
        return seal_.template try_enter_write_section<CKernelTable>(writer);
    }

    // Overflow aborts rather than truncating. A dropped registration would
    // leave classify() answering OPAQUE for that one schema, and that only
    // shows up much later, at replay, on whichever trace happens to use it.
    //
    // The view's type is the proof that the table was not sealed when the
    // view was minted.  The section is the proof that no seal and no other
    // writer runs until it ends, so the write finds no seal.
    //
    // The view and the section each name the table that they prove, and a
    // view or a section of another table proves nothing about this one.  The
    // check of the view runs in every build mode, and the check of the
    // section is a precondition.
    void register_op(WriteSection<CKernelTable>& section, MutableView const& view, SchemaHash schema_hash,
                     CKernelId id) {
        CRUCIBLE_PRE(section.names(seal_));
        CRUCIBLE_FATAL_INVARIANT(&view.carrier() == this);
        write_op_(schema_hash, id);
    }

    // gnu::pure lets the optimiser cache a result across calls. That is sound
    // only because registration finishes and the table is sealed before any
    // classifying reader runs. An interleaved registration would invalidate it.
    // Complexity: O(log n).
    [[nodiscard, gnu::pure]] CKernelId classify(SchemaHash schema_hash) const noexcept {
        return detail::find_kernel_id_(entries_.view(), schema_hash);
    }

    [[nodiscard]] uint32_t count() const noexcept { return static_cast<uint32_t>(entries_.size()); }

    // Empties the table and opens it again.  A sealed table that opens again
    // takes writes, so only a test that reuses one table across cases may
    // do this: it takes the proof of a reopen, which only the test context
    // builds, and code that ships cannot mint that context.  The caller
    // makes sure that no other thread uses the table.
    void clear(TableReopen<CKernelTable>&& reopen) noexcept {
        entries_.clear();
        seal_.reopen(reopen);
        CRUCIBLE_POST(0, count() == 0u);
        CRUCIBLE_POST(0, !is_sealed());
    }

private:
    // Runs inside the write section, so no seal and no other writer runs
    // at the same time.  The entries are in the order of the hashes: an entry
    // of the hash takes the id, and a new entry goes before the first entry of
    // a larger hash.  Complexity: O(n).
    void write_op_(SchemaHash schema_hash, CKernelId id) {
        const CKernelEntry written{.schema_hash = schema_hash, .id = id};
        const std::size_t position = detail::kernel_position_(entries_.view(), schema_hash);
        const bool is_held = entries_.get(position).is_some_and(
            [schema_hash](auto const& held) noexcept { return held->schema_hash == schema_hash; });
        // A full table ends the process.  A dropped registration leaves
        // classify() at OPAQUE for that one schema, and that shows only much
        // later, at replay, on the trace that uses it.  The position past a
        // full table has no index.
        const ::fixy::Index<CKERNEL_TABLE_CAP> index =
            ::fixy::Index<CKERNEL_TABLE_CAP>::check(position).expect(FULL_TABLE);
        if (is_held) {
            entries_.set(index, written);
        } else {
            entries_.insert(index, written).expect(FULL_TABLE);
        }
        CRUCIBLE_POST(0, classify(schema_hash) == id);
    }

    // The reason of the end of the process at a full table.
    static constexpr ::fixy::Fmt<> FULL_TABLE{
        "crucible: the kernel table holds CKERNEL_TABLE_CAP schemas.  Raise CKERNEL_TABLE_CAP in crucible/CKernel.h, or "
        "register fewer schemas"};

    ::fixy::FixedList<CKernelEntry, CKERNEL_TABLE_CAP> entries_{};
    // The phase of the table.  Its one word is an Atomic with acquire and
    // release steps, so two threads write it at the same time.  It is
    // mutable, so seal is a const door: the check of the statement
    // synchronized{} admits a const door that writes only parts that two
    // threads can write (foundation/Transfer.h).
    mutable RegistrationSeal seal_;
};

inline bool view_ok(CKernelTable const& table, std::type_identity<ckernel_state::Mutable>) noexcept {
    return !table.is_sealed();
}

inline bool view_ok(CKernelTable const& table, std::type_identity<ckernel_state::Sealed>) noexcept {
    return table.is_sealed();
}

using CKernelTableSingleton = ::fixy::Tagged<CKernelTable*, ::fixy::tags::source::Singleton>;

// One table for the process.  A registration in one shared library and a
// classification in another reach the same table.
CRUCIBLE_PROCESS_WIDE [[nodiscard]] inline CKernelTableSingleton global_ckernel_table() {
    static CKernelTable table;
    return ::fixy::mint_tagged<::fixy::tags::source::Singleton>(&table);
}

// False when the table is sealed, before the view or before the write, and
// then nothing is registered.
[[nodiscard]] inline bool register_schema_hash(VigilFgCtx const& fg,
                                               ::fixy::Tagged<SchemaHash, ::fixy::tags::source::External> schema_hash,
                                               CKernelId id) {
    CKernelTable& table = *global_ckernel_table().value();
    return table.mint_mutable_view(fg).match(
        [&fg, &table, schema_hash, id](CKernelTable::MutableView view) {
            return table.try_enter_write_section(fg).match(
                [&table, &view, schema_hash, id](WriteSection<CKernelTable> section) {
                    table.register_op(section, view, schema_hash.value(), id);
                    return true;
                },
                [] { return false; });
        },
        [] { return false; });
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
    ::fixy::View<const CKernelEntry> entries{};
};

// The Ref of a published table.  Its brand proves that the table has static
// storage, because only ref_of_static of foundation/core/Ref.h gives it.
using StaticCKernelTableRef = ::fixy::Ref<const StaticCKernelTable, ::fixy::StaticStorageBrand>;

namespace detail {
// Constant-initialized, so it has no dynamic initializer to order against
// the adapter that publishes into it and no guard variable to race on.  The
// empty Option is the state before the publication.
//
// One cell of one pointer for the process.  The PyTorch adapter publishes
// from libcrucible_dispatch.so, and the background thread that classifies
// runs in libcrucible_vessel.so.  The cell lives for the whole process, so
// it holds only a Ref of static storage.
CRUCIBLE_PROCESS_WIDE inline constinit ::fixy::Atomic<::fixy::Option<StaticCKernelTableRef>> static_ckernel_table_{};
}  // namespace detail

// Publishing is a release store, and classify_static reads with acquire, so a
// reader that observes the table also observes every entry the adapter
// wrote before it. The adapter publishes from its load-time initializer, so
// the table is in place before the adapter records its first operation, and
// so before the background thread classifies one.  The background thread can
// start first: Python creates the Vigil before it loads the adapter.
//
// The table must outlive the process's classifying readers.  The table is the
// template argument Table, so it is an object of static storage: a local
// table, a temporary and a thread_local table are no template argument, and
// their publication does not compile.  The table is const, so no write
// changes it under a reader.
template <auto& Table>
    requires std::is_same_v<std::remove_reference_t<decltype(Table)>, const StaticCKernelTable>
inline void publish_static_ckernel_table() noexcept {
    detail::static_ckernel_table_.store_release(
        ::fixy::Option<StaticCKernelTableRef>::some(::fixy::ref_of_static<Table>()));
}

// OPAQUE when no adapter has published, which is every build that links no
// framework adapter, and OPAQUE when the published array does not name this
// schema. Both answers are the same answer the run-time table gives on a
// miss, so the fallback below is reached in exactly those two cases.
//
// Not gnu::pure, and classify_kernel is not either.  The adapter can publish
// between two calls, and a pure call lets the compiler keep the OPAQUE of
// the first call.
[[nodiscard]] inline CKernelId classify_static(SchemaHash schema_hash) noexcept {
    return detail::static_ckernel_table_.load_acquire()
        .map([schema_hash](StaticCKernelTableRef table) noexcept {
            return detail::find_kernel_id_(table->entries, schema_hash);
        })
        .value_or(CKernelId::OPAQUE);
}

// The published array first, the run-time table second. An adapter that
// publishes a classification for a schema and also registers one for it gets
// the published answer, which is the one its own generator derived.
[[nodiscard]] inline CKernelId classify_kernel(SchemaHash schema_hash) noexcept {
    const CKernelId from_static = classify_static(schema_hash);
    if (from_static != CKernelId::OPAQUE) return from_static;
    return global_ckernel_table().value()->classify(schema_hash);
}

[[nodiscard]] constexpr ::fixy::TextView ckernel_name(CKernelId id) {
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
        default:
            // A CKernelId comes only from an enumerator or from enum_from,
            // which refuses each byte that names no id.  The switch names each
            // id, which -Wswitch-enum checks, so no value reaches this arm.
            // -Wswitch-default asks for the arm.
            ::fixy::unreachable();
    }
}

}  // namespace crucible
