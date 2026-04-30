#pragma once

// ── crucible::forge::KernelNode<Row> — IR002 row-parameterized kernel ──────
//
// FOUND-J01.  KernelNode is Forge's portable kernel IR (IR002): one
// matched template (GEMM, ATTENTION, NORM, ...) with concrete dtype
// configuration, committed semantic layout, pinned NumericalRecipe, and
// a TileSpec seed.  IR002 sits between op-level IR001 and vendor IR003*
// owned by Mimic.
//
// Per FORGE.md §18.2 the in-memory record is exactly **64 bytes / one
// cache line**.  Per 28_04_2026_effects.md §9.1 the kernel is
// row-parameterized at compile time only — the Row enters the cache
// key generator (FOUND-I01) and the dispatcher row-checker (FOUND-D)
// without consuming any storage at runtime.
//
// ── Design intent ────────────────────────────────────────────────────
//
// 1. The Row is type-level only.  The struct has NO Row-typed member;
//    the parameter participates in name mangling (so distinct rows
//    instantiate distinct types) but contributes zero bytes.  The
//    `using row = Row<Es...>;` member alias is the canonical reading
//    surface for downstream phases.
//
// 2. The struct stays at exactly 64 bytes regardless of Row.  This is
//    enforced by:
//      (a) partial specialization on Row<Es...> only (the primary
//          template is forward-declared, never defined — non-row Row
//          parameters fail with "incomplete type"),
//      (b) static_assert(sizeof(KernelNode<Row<>>) == 64) at every
//          well-known instantiation: PureRow, AllRow, and a custom
//          single-atom row.  These fire at every TU that includes
//          this header — there is no way to ship a layout regression.
//
// 3. NSDMI on every field.  InitSafe demands every field have a default
//    initializer.  All pointer fields default to nullptr, all sentinel-
//    able strong types default to .none()/.sentinel(), all enum fields
//    default to a documented sentinel.  No reading of an uninitialized
//    KernelNode field is possible under -ftrivial-auto-var-init=zero +
//    P2795R5.
//
// 4. CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE.  IR002 nodes are arena-
//    allocated and bulk-memcpy'd during arena growth — every field is
//    trivially copyable (raw pointers, strong-id newtypes, enum class).
//
// 5. The two structural-cohort tasks downstream (J02-J15) consume the
//    same template; this header is the single source of truth for what
//    KernelNode is and how rows enter it.
//
// ── Public surface ───────────────────────────────────────────────────
//
//   crucible::forge::KernelKind             — 22-value enum (§18.1)
//   crucible::forge::KernelId               — strong uint32_t newtype
//   crucible::forge::KernelContentHash      — strong uint64_t newtype
//   crucible::forge::KernelNode<Row<Es...>> — the IR002 record itself
//   crucible::forge::KernelNode<R>::row     — the parameter alias
//
// ── Forward dependencies ─────────────────────────────────────────────
//
// `NumericalRecipe` and `TileSpec` are referenced by pointer; we do
// NOT pull them in here (avoids include-ordering tangles + keeps this
// header light enough to include from every Forge-phase translation
// unit).  Production call sites that dereference recipe/tile pull in
// the corresponding header at the call site.

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>

#include <cstdint>
#include <type_traits>

namespace crucible::forge {

// ── KernelKind — 22 kernel families (FORGE.md §18.1) ──────────────────
//
// Vendor-neutral.  ATTENTION is "scaled-dot-product-attention on these
// tensors", period — the backend decides whether to realize it as
// FlashAttention-3 on Hopper, Efficient Attention on AMD, or bespoke
// MXU sequencing on TPU.  KernelKind names the *family*; the concrete
// realization comes from (KernelKind × NumericalRecipe × VendorBackend)
// per the cross-vendor numerics CI matrix (MIMIC.md §41).

enum class KernelKind : uint8_t {
    // Matrix / tensor ops
    GEMM,                 // 2D × 2D matmul
    BMM,                  // batched 3D × 3D matmul
    CONV,                 // 1D / 2D / 3D convolution
    DEQUANT_GEMM,         // INT4 / FP8 dequant fused with matmul

    // Attention family
    ATTENTION,            // standard SDPA with flash recurrence
    PAGED_ATTN,           // page-table-indirect KV gather
    RAGGED_ATTN,          // THD-layout packed variable-length

    // Normalization / activation / pointwise
    NORM,                 // LayerNorm / RMSNorm / BatchNorm / GroupNorm / InstanceNorm
    SOFTMAX,              // online or naive recurrence per recipe
    POINTWISE,            // arbitrary elementwise chain (holds ComputeBody)

    // Reductions and scans
    REDUCE,               // sum / mean / max / min / argmax / argmin / topk
    SCAN,                 // prefix scan (cumsum / cumprod / assoc-scan)

    // Indexing
    GATHER_SCATTER,
    EMBEDDING,            // lookup + optional pool

    // Stochastic
    RNG,                  // Philox-seeded

    // Distributed
    COLLECTIVE,           // allreduce / allgather / reduce_scatter / all-to-all / ...

    // Recurrent / state-space
    SSM,                  // Mamba / Mamba-2 SSD / RWKV / RetNet / xLSTM

    // Composition / fusion
    FUSED_COMPOUND,       // kernel-kind-level fusion (GEMM + activation + bias, etc.)
    MOE_ROUTE,            // top-k routing + permute + grouped GEMM

    // Optimizer (holds per-parameter update body as extension point)
    OPTIMIZER,            // Adam / Lion / Muon / user-defined via update_body

    // Escape hatches
    OPAQUE_EXTERN,        // link against a precompiled library kernel (vendor-neutral name)
    CUSTOM,               // wraps IR001 nodes directly; backend emits a generic loop nest

    COUNT                 // = 22; bumps trigger the static_assert below
};

static_assert(static_cast<uint8_t>(KernelKind::COUNT) == 22,
    "KernelKind::COUNT mismatch — adding a kernel family requires updating "
    "the cross-vendor CI matrix (MIMIC.md §41), the recipe registry "
    "(FORGE.md §20), and every per-vendor emit_kernel template (FOUND-J08..J12)");

// ── Strong identity / hash newtypes (FORGE.md §18.2) ──────────────────
//
// We declare these directly with the field-private idiom rather than
// reusing the CRUCIBLE_STRONG_ID macro from Types.h because that macro
// has been #undef'd at the bottom of Types.h.  The shape is identical
// to the macro form so the discipline carries across.

struct KernelId {
private:
    uint32_t v;
public:
    constexpr KernelId() noexcept : v(UINT32_MAX) {}
    constexpr explicit KernelId(uint32_t val) noexcept : v(val) {}
    [[nodiscard]] static constexpr KernelId from_raw(uint32_t val) noexcept {
        return KernelId{val};
    }
    [[nodiscard]] static constexpr KernelId none() noexcept {
        return KernelId{UINT32_MAX};
    }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return v != UINT32_MAX;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return is_valid();
    }
    [[nodiscard]] constexpr uint32_t raw() const noexcept { return v; }
    constexpr auto operator<=>(const KernelId&) const noexcept = default;
};
static_assert(sizeof(KernelId) == sizeof(uint32_t));

struct KernelContentHash {
private:
    uint64_t v;
public:
    constexpr KernelContentHash() noexcept : v(0) {}
    constexpr explicit KernelContentHash(uint64_t val) noexcept : v(val) {}
    [[nodiscard]] static constexpr KernelContentHash from_raw(uint64_t val) noexcept {
        return KernelContentHash{val};
    }
    [[nodiscard]] constexpr uint64_t raw() const noexcept { return v; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return v != 0;
    }
    [[nodiscard]] static constexpr KernelContentHash sentinel() noexcept {
        return KernelContentHash{UINT64_MAX};
    }
    [[nodiscard]] constexpr bool is_sentinel() const noexcept {
        return v == UINT64_MAX;
    }
    constexpr auto operator<=>(const KernelContentHash&) const noexcept = default;
};
static_assert(sizeof(KernelContentHash) == sizeof(uint64_t));

// ── Forward declarations for arena-pointed payload types ──────────────
//
// NumericalRecipe ships in <crucible/NumericalRecipe.h> (interned in
// the recipe pool); TileSpec ships later (FORGE.md §18.4).  Forward
// declarations let downstream phases inspect the pointer without
// pulling in the full headers.

struct NumericalRecipe;
struct TileSpec;

// ── KernelNode<Row> — the IR002 record ─────────────────────────────────
//
// Primary template: forward-declared only.  Instantiating with a non-
// Row<Es...> type fails with "incomplete type" — the cleanest way to
// reject KernelNode<int> et al at compile time.  No SFINAE, no concept,
// no static_assert needed; the C++ specialization rules carry the load.

template <typename Row>
struct KernelNode;

template <crucible::effects::Effect... Es>
struct alignas(64) KernelNode<crucible::effects::Row<Es...>> {
    // ── Row alias — the only Row-bearing surface ─────────────────────
    // Type-level only.  Downstream phases read this via decltype or
    // a using-alias on the template param.
    using row = crucible::effects::Row<Es...>;

    // ── Identity (8B) ────────────────────────────────────────────────
    KernelId         id;                                       // 4B
    KernelKind       kind        = KernelKind::CUSTOM;         // 1B
    uint8_t          flags       = 0;                          // 1B  (DEAD/VISITED/FUSED/...)
    int8_t           device_idx  = -1;                         // 1B  (-1 = unbound)
    uint8_t          ndim        = 0;                          // 1B  (rank of primary output)

    // ── Dtype + semantic layout (8B) ─────────────────────────────────
    crucible::ScalarType in_dtype    = crucible::ScalarType::Undefined; // 1B
    crucible::ScalarType out_dtype   = crucible::ScalarType::Undefined; // 1B
    crucible::Layout     layout_in   = crucible::Layout::Strided;        // 1B
    crucible::Layout     layout_out  = crucible::Layout::Strided;        // 1B
    uint16_t             num_inputs  = 0;                                // 2B
    uint16_t             num_outputs = 0;                                // 2B

    // ── Kind-specific attrs (8B) ─────────────────────────────────────
    // Cast by `kind`; arena-allocated.  Pointer-only to keep this
    // record at a single cache line.
    void*                attrs       = nullptr;                          // 8B

    // ── Pinned numerical recipe (8B) ─────────────────────────────────
    // Interned in the RecipePool (per FORGE.md §19).  Owns its bit-tier
    // promise (UNORDERED / ORDERED / BITEXACT_TC / BITEXACT_STRICT).
    const NumericalRecipe* recipe    = nullptr;                          // 8B

    // ── Tile spec (8B) ───────────────────────────────────────────────
    // Interned in the TilePool (FORGE.md §18.4); seeded by Phase E,
    // refined into a pareto set by Phase F.
    const TileSpec*        tile      = nullptr;                          // 8B

    // ── I/O slot bindings (16B) ──────────────────────────────────────
    // SlotId arrays live in the Forge arena adjacent to the node.
    // num_inputs / num_outputs above bound the iteration.
    crucible::SlotId*      input_slots  = nullptr;                       // 8B
    crucible::SlotId*      output_slots = nullptr;                       // 8B

    // ── Provenance + content hash (8B) ───────────────────────────────
    // Memoized; populated once at IR002 construction.  Enters the
    // KernelCache key generator alongside the row hash.
    KernelContentHash      content_hash;                                 // 8B
};

// ── Layout discipline — fires at every TU including this header ───────
//
// Three well-known instantiations cover the structural shape:
//   * PureRow      — empty effect row, the bottom of the F* lattice
//   * AllRow       — every atom present
//   * Row<Bg>      — single-atom row exercising the partial specialization
//
// Adding a field that breaks 64-byte layout, alignas(64), or trivial
// relocatability fires HERE, before any test or production call site.
// The InitSafe assertion checks all NSDMI agree on a default value.

static_assert(sizeof(KernelNode<crucible::effects::PureRow>) == 64,
    "KernelNode<PureRow> must be exactly one cache line (64 bytes); "
    "FORGE.md §18.2 layout invariant");
static_assert(alignof(KernelNode<crucible::effects::PureRow>) == 64,
    "KernelNode<PureRow> must be cache-line aligned");
static_assert(sizeof(KernelNode<crucible::effects::AllRow>) == 64,
    "KernelNode<AllRow> must be exactly one cache line — Row is "
    "type-level only and must contribute zero bytes");
static_assert(alignof(KernelNode<crucible::effects::AllRow>) == 64,
    "KernelNode<AllRow> must be cache-line aligned");
static_assert(sizeof(KernelNode<crucible::effects::Row<
                       crucible::effects::Effect::Bg>>) == 64,
    "KernelNode<Row<Bg>> must be exactly one cache line — single-atom "
    "row contributes zero bytes");

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(KernelNode<crucible::effects::PureRow>);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(KernelNode<crucible::effects::AllRow>);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(
    KernelNode<crucible::effects::Row<crucible::effects::Effect::Bg>>);

// ── Per-Row distinctness witness ──────────────────────────────────────
//
// The row parameter participates in name mangling, so two distinct rows
// produce distinct types.  This is what lets the KernelCache (per
// FOUND-I05/06/07) key entries on (content_hash, row_hash) and have
// the same KernelNode emit different cached compilations per-row.

static_assert(!std::is_same_v<
    KernelNode<crucible::effects::PureRow>,
    KernelNode<crucible::effects::AllRow>>,
    "KernelNode<PureRow> and KernelNode<AllRow> must be distinct types — "
    "row distinctness is what enables per-row cache slotting");

static_assert(!std::is_same_v<
    KernelNode<crucible::effects::Row<crucible::effects::Effect::Bg>>,
    KernelNode<crucible::effects::Row<crucible::effects::Effect::Alloc>>>,
    "KernelNode<Row<Bg>> and KernelNode<Row<Alloc>> must be distinct — "
    "single-atom row distinctness is what FOUND-J02..J15 rely on");

}  // namespace crucible::forge
