#pragma once

#include <crucible/Platform.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible {

// ── ElementBytes — strong type for "bytes per element of a dtype" (#129)
//
// Previously `element_size(ScalarType)` returned a raw `uint8_t`,
// letting callers accidentally use the per-element byte size as a
// SlotId count, a MetaLog index, or a running byte total — all of
// which happen to fit in 8 bits but have completely different
// semantics.  `ElementBytes` is a phantom-typed newtype whose value
// range is tied to the per-element-size semantic, preventing
// silent conflation at TypeSafe call sites.
//
// Value domain: 0 (ScalarType::Undefined) plus {1, 2, 4, 8, 16}
// (real dtypes).  Underlying storage is `uint8_t` — 16 is the
// maximum real element size (ComplexDouble).
//
// The type is layout-identical to uint8_t under `[[no_unique_address]]`
// + the member struct has a single uint8_t field → `sizeof(ElementBytes)
// == sizeof(uint8_t) == 1`.
struct ElementBytes {
    uint8_t value_ = 0;

    constexpr ElementBytes() noexcept = default;
    explicit constexpr ElementBytes(uint8_t v) noexcept : value_{v} {}

    [[nodiscard]] constexpr uint8_t raw()     const noexcept { return value_; }
    [[nodiscard]] constexpr bool    is_zero() const noexcept { return value_ == 0; }

    // Comparison is defined between ElementBytes values only — raw
    // integer literals DON'T implicitly convert (the ctor is
    // `explicit`).  Tests and call sites write `ElementBytes{4}` when
    // they want to compare against a literal, surfacing the unit.
    auto operator<=>(const ElementBytes&) const = default;

    // Common operation: multiply by a count to obtain total bytes.
    // Returns `std::size_t` so the result is wide enough for arena /
    // region / memory-plan totals without truncation.  Overflow is
    // NOT checked here — callers needing safety wrap via Checked.h's
    // `safe_mul<std::size_t, ...>` or the `safe_array_bytes<T, N>`
    // helper family (#134).
    [[nodiscard]] constexpr std::size_t times(std::size_t n) const noexcept {
        return std::size_t{value_} * n;
    }
};
static_assert(sizeof(ElementBytes) == sizeof(uint8_t),
    "ElementBytes must be layout-identical to uint8_t (#129).");

// Mirror c10::ScalarType ordinals exactly so int8_t casts are compatible
// between the standalone library and the PyTorch Vessel adapter.
enum class ScalarType : int8_t {
  Byte = 0,
  Char = 1,
  Short = 2,
  Int = 3,
  Long = 4,
  Half = 5,
  Float = 6,
  Double = 7,
  ComplexHalf = 8,
  ComplexFloat = 9,
  ComplexDouble = 10,
  Bool = 11,
  BFloat16 = 15,
  Float8_e5m2 = 23,
  Float8_e4m3fn = 24,
  Float8_e5m2fnuz = 25,
  Float8_e4m3fnuz = 26,
  Undefined = -1,
};

// Byte size per dtype.  gnu::const: takes one value, no memory access.
//
// Returns an `ElementBytes` strong type (#129) — prevents conflation
// of per-element byte sizes with SlotId counts, MetaLog indices, or
// running byte totals at TypeSafe call sites.  `.raw()` extracts the
// underlying uint8_t; `.times(n)` produces std::size_t for totals.
//
// Postcondition: result is non-zero for every ScalarType EXCEPT
// Undefined (which returns ElementBytes{0}).  The post() contract
// propagates as [[assume]] at call sites where t is known
// non-Undefined, eliminating divide-by-zero branches in size-math
// chains.  Callers that intend to accept Undefined must handle the
// `.is_zero()` case explicitly.
CRUCIBLE_CONST constexpr ElementBytes element_size(ScalarType const t) noexcept
    post (r: t == ScalarType::Undefined || !r.is_zero())
{
  switch (t) {
    case ScalarType::Bool:
    case ScalarType::Byte:
    case ScalarType::Char:
    case ScalarType::Float8_e5m2:
    case ScalarType::Float8_e4m3fn:
    case ScalarType::Float8_e5m2fnuz:
    case ScalarType::Float8_e4m3fnuz:
      return ElementBytes{1};
    case ScalarType::Short:
    case ScalarType::Half:
    case ScalarType::BFloat16:
      return ElementBytes{2};
    case ScalarType::Int:
    case ScalarType::Float:
    case ScalarType::ComplexHalf:
      return ElementBytes{4};
    case ScalarType::Long:
    case ScalarType::Double:
    case ScalarType::ComplexFloat:
      return ElementBytes{8};
    case ScalarType::ComplexDouble:
      return ElementBytes{16};
    case ScalarType::Undefined:
      return ElementBytes{0};
    default:
      std::unreachable();
  }
}

// Mirror c10::DeviceType ordinals exactly (c10/core/DeviceType.h).
enum class DeviceType : int8_t {
  CPU = 0,
  CUDA = 1,
  MKLDNN = 2,
  HIP = 6,      // AMD HIP — was incorrectly 20 (PrivateUse1's ordinal)
  XLA = 9,
  MPS = 13,
  Meta = 14,
  PrivateUse1 = 20,
};

// Mirror c10::Layout ordinals.
enum class Layout : int8_t {
  Strided = 0,
  Sparse = 1,
  SparseCsr = 2,
  SparseCsc = 3,
  SparseBsr = 4,
  SparseBsc = 5,
};

// ═══════════════════════════════════════════════════════════════════
// Strong ID types — Rust-style newtypes over uint32_t
//
// Prevents mixing op indices, slot IDs, node IDs, symbol IDs at
// compile time. Same codegen as raw uint32_t (single register).
//
// Design:
//   - explicit ctor: no implicit conversion from uint32_t
//   - .raw(): explicit unwrap (like Rust's .0 or .into())
//   - .none(): named sentinel replacing bare UINT32_MAX
//   - .is_valid(): sentinel check
//   - operator<=>: full comparison support
//   - No arithmetic: must unwrap, compute, rewrap (intentional)
// ═══════════════════════════════════════════════════════════════════

#define CRUCIBLE_STRONG_ID(Name)                                           \
  struct Name {                                                            \
  private:                                                                 \
    /* #133: v is private.  External read goes through .raw(); external */\
    /* construction goes through the explicit ctor OR from_raw().  This */\
    /* closes the direct-field-access hole where `id.v = 999` could */    \
    /* bypass the explicit-ctor discipline and mutate a strong-ID in */   \
    /* place. */                                                           \
    uint32_t v;                                                            \
  public:                                                                  \
    constexpr Name() noexcept : v(UINT32_MAX) {}                           \
    constexpr explicit Name(uint32_t val) noexcept : v(val) {}             \
    /* #133: named factory for cross-kind bridges.  When converting */    \
    /* from ANOTHER strong-ID's raw (the silent-bug pattern), prefer */   \
    /* `Name::from_raw(other.raw())` — the explicit factory is */         \
    /* greppable for code review.  Audit: `grep "::from_raw"` finds */    \
    /* every bridge site mechanically. */                                 \
    [[nodiscard]] static constexpr Name from_raw(uint32_t val) noexcept {  \
      return Name{val};                                                    \
    }                                                                      \
    [[nodiscard]] static constexpr Name none() noexcept {                  \
      return Name{UINT32_MAX};                                             \
    }                                                                      \
    [[nodiscard]] constexpr bool is_valid() const noexcept {               \
      return v != UINT32_MAX;                                              \
    }                                                                      \
    [[nodiscard]] constexpr explicit operator bool() const noexcept {      \
      return is_valid();                                                   \
    }                                                                      \
    [[nodiscard]] constexpr uint32_t raw() const noexcept { return v; }    \
    constexpr auto operator<=>(const Name&) const noexcept = default;      \
  };                                                                       \
  static_assert(sizeof(Name) == sizeof(uint32_t))

CRUCIBLE_STRONG_ID(OpIndex);    // index into TraceEntry[] ops array
CRUCIBLE_STRONG_ID(SlotId);     // index into TensorSlot[] slots array
CRUCIBLE_STRONG_ID(NodeId);     // index into Graph node array
CRUCIBLE_STRONG_ID(SymbolId);   // index into SymbolTable entries
CRUCIBLE_STRONG_ID(MetaIndex);  // index into MetaLog buffer

#undef CRUCIBLE_STRONG_ID

// ═══════════════════════════════════════════════════════════════════
// Strong hash types — Rust-style newtypes over uint64_t
//
// Prevents mixing schema hashes, shape hashes, scope hashes, and
// callsite hashes at compile time. Same codegen as raw uint64_t.
//
// Design:
//   - explicit ctor: no implicit conversion from uint64_t
//   - .raw(): explicit unwrap for hashing/indexing
//   - Default: 0 (no hash / unset)
//   - operator bool: true if non-zero
//   - operator<=>: full comparison support
//   - No arithmetic: must unwrap, compute, rewrap (intentional)
// ═══════════════════════════════════════════════════════════════════

#define CRUCIBLE_STRONG_HASH(Name)                                         \
  struct Name {                                                            \
  private:                                                                 \
    /* #133: v is private; external access is via .raw() only. */         \
    uint64_t v;                                                            \
  public:                                                                  \
    constexpr Name() noexcept : v(0) {}                                    \
    constexpr explicit Name(uint64_t val) noexcept : v(val) {}             \
    /* #133: named factory for cross-kind bridges — `grep "::from_raw"` */\
    /* locates every cross-kind-hash conversion site. */                  \
    [[nodiscard]] static constexpr Name from_raw(uint64_t val) noexcept {  \
      return Name{val};                                                    \
    }                                                                      \
    [[nodiscard]] constexpr uint64_t raw() const noexcept { return v; }    \
    [[nodiscard]] constexpr explicit operator bool() const noexcept {      \
      return v != 0;                                                       \
    }                                                                      \
    /* Sentinel: impossible value used as end-of-region marker.            \
     * No real hash can be UINT64_MAX — hash functions produce             \
     * uniformly distributed values, and we reserve this one. */           \
    [[nodiscard]] static constexpr Name sentinel() noexcept {              \
      return Name{UINT64_MAX};                                             \
    }                                                                      \
    [[nodiscard]] constexpr bool is_sentinel() const noexcept {            \
      return v == UINT64_MAX;                                              \
    }                                                                      \
    constexpr auto operator<=>(const Name&) const noexcept = default;      \
  };                                                                       \
  static_assert(sizeof(Name) == sizeof(uint64_t))

CRUCIBLE_STRONG_HASH(SchemaHash);    // op identity (OperatorHandle schema)
CRUCIBLE_STRONG_HASH(ShapeHash);     // quick hash of input tensor shapes
CRUCIBLE_STRONG_HASH(ScopeHash);     // module hierarchy path hash
CRUCIBLE_STRONG_HASH(CallsiteHash);  // Python source location identity
CRUCIBLE_STRONG_HASH(ContentHash);   // region content identity (kernel cache key)
CRUCIBLE_STRONG_HASH(MerkleHash);    // subtree identity (includes all descendants)
CRUCIBLE_STRONG_HASH(RecipeHash);    // NumericalRecipe identity (FORGE.md §19, §20)
CRUCIBLE_STRONG_HASH(RowHash);       // effect-row content identity — fmix64 fold
                                     // over the wrapper-nesting order, see
                                     // FOUND-I02 (#761) for the canonical
                                     // algorithm and FOUND-I05/06/07 for the
                                     // L1/L2/L3 KernelCache lookup wiring.

#undef CRUCIBLE_STRONG_HASH

// ═══════════════════════════════════════════════════════════════════
// Hash taxonomy — TWO DISJOINT FAMILIES with different persistence
// semantics.  The type system does not yet enforce the split (future
// work — see the `InternHash` note below); until it does, this
// comment block is the contract every new hash site must honor.
//
// ── Family A: PERSISTENT hashes ────────────────────────────────────
//
// Must be byte-stable across processes, platforms, and Crucible
// versions within a compile_version window.  Family A values:
//   - flow into Cipher entry keys (L1/L2/L3 KernelCache, RegionNode
//     object store, BranchNode arm targets, TrainingCheckpoint
//     manifest; CRUCIBLE.md §9)
//   - identify federation-shareable artifacts (IR002 snapshots,
//     FORGE.md §23.2)
//   - drive cross-vendor replay determinism (CRUCIBLE.md §10)
//
// A drift in any Family-A hash — even one bit — is a wire-format
// break and requires a CDAG_VERSION bump.  Goldens for Family A are
// correct AS LONG AS they are computed via serialize→hash-bytes
// (the bytes are the ground truth; hashing in-memory structs is
// fragile).
//
// Members (by type):
//   ContentHash      — RegionNode structural identity
//   MerkleHash       — subtree identity incl. descendants
//   SchemaHash       — ATen op identity
//   ShapeHash        — tensor geometry
//   ScopeHash        — module hierarchy path
//   CallsiteHash     — Python source-location identity
//   RecipeHash       — NumericalRecipe identity (FORGE.md §19, §20)
//   RowHash          — effect-row content identity (Met(X) wrapper stack)
//   (future) KernelContentHash / PlanHash — FORGE.md §23
//
// Members (by function):
//   compute_content_hash(TensorMeta...)      MerkleDag.h
//   compute_merkle_hash(node)                MerkleDag.h
//   Guard::hash()                             MerkleDag.h  (reflect_hash)
//   feedback_signature(edges)                 MerkleDag.h
//   loopterm_hash(LoopNode)                   MerkleDag.h
//   branched_content_hash(BranchNode)         MerkleDag.h
//
// ── Family B: PROCESS-LOCAL hashes ─────────────────────────────────
//
// Intentionally NOT byte-stable across processes.  Family B values
// embed arena pointer entropy (ASLR-randomized per process) to get
// O(1) intern-table probing at zero-cost-beyond-pointer-compare.
// The SAME structural input produces DIFFERENT bits in different
// processes.
//
// Family B values MUST NOT:
//   - be written to Cipher under any circumstance
//   - flow into any Family-A hash computation
//   - be pinned in a golden test file
//   - be assumed stable across program restarts
//
// Members:
//   Expr::hash                   — ExprPool Swiss-table intern key.
//                                  Computed by detail::expr_hash
//                                  mixing arg-pointer bits (Expr.h,
//                                  ExprPool.h lines ~28-67).
//   Graph::cse_hash_()           — GraphNode CSE probe hash; mixes
//                                  canonical[] pointer entropy
//                                  (Graph.h ~line 964).
//   KernelCache slot probe       — the open-addressing probe order
//                                  is process-local; the stored
//                                  content_hash key IS Family A.
//
// If you ever need a cross-process stable identity for an Expr
// (e.g., ComputeBody fragment hashing for FORGE federation per
// FORGE.md §18.6), compute a new `Expr::content_hash()` that walks
// the tree structurally without consulting `args[i]` pointers — do
// NOT reuse `Expr::hash`.  Cache the structural hash on the Expr at
// construction time (Exprs are const post-#113).
//
// ── Future: type-level distinction ─────────────────────────────────
//
// The clean fix is to replace `uint64_t Expr::hash` with a strong
// type `InternHash` distinct from `ContentHash` / `MerkleHash`, so
// that mixing them is a compile error.  Deferred — requires an
// audit of ExprPool, Graph, KernelCache, and tests.  Document first,
// enforce later.
// ═══════════════════════════════════════════════════════════════════

// Trivial relocatability: all strong ID/hash types are trivially copyable
// PODs — safe for Arena memcpy, vector reallocation, flat array storage.
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(OpIndex);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SlotId);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(NodeId);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SymbolId);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(MetaIndex);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SchemaHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(ShapeHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(ScopeHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(CallsiteHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(ContentHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(MerkleHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(RecipeHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(RowHash);

// ═══════════════════════════════════════════════════════════════════
// KernelCacheKey — composite identity for the federation-shared
// KernelCache (CRUCIBLE.md §10, FORGE.md §23.2, Phase 5 of the
// development plan).  FOUND-I01.
//
// Two orthogonal axes — neither sufficient alone:
//
//   • content_hash : ContentHash
//       Region structural identity.  What the kernel *does* (op
//       sequence, shapes, strides, dtypes, scalar args).  Frozen by
//       compute_content_hash(...) at trace-build time.
//
//   • row_hash : RowHash
//       Met(X) effect-row content identity.  What capabilities /
//       guarantees the kernel *promises* (Pure ⊑ Tot ⊑ Div ⊑ ST ⊑ All
//       and the cap stack: Alloc / IO / Block / DetSafe / Hot /
//       Wait / Mem / Numerical / Recipe / ResidencyHeat / Vendor /
//       Crash / ...).  Computed by the FOUND-I02 fmix64-fold over the
//       canonical wrapper-nesting order (FOUND-I03).
//
// Two regions with byte-identical content_hash but different
// row_hash represent the *same computation under different effect
// regimes* — they MUST cache to distinct slots.  A Pure-row kernel
// is trivially federatable; an IO-row kernel is not; sharing a slot
// between them silently breaks the federation contract.
//
// Layout:  16 bytes — content_hash (8B) + row_hash (8B).  Both axes
// are Family-A persistent hashes (see taxonomy above), so the entire
// key is byte-stable across processes within a CDAG_VERSION window.
//
// API surface intentionally minimal at FOUND-I01: NSDMI defaults
// (zero — meaning "unset"), `<=>` comparison, `is_zero()`,
// `sentinel()` factory.  Combination into a single 64-bit slot probe
// (the actual KernelCache::lookup signature change) is FOUND-I05/06/
// 07 — the right fold strategy depends on which cache level (L1
// IR002 / L2 IR003* / L3 compiled bytes) is doing the lookup.
struct KernelCacheKey {
    ContentHash content_hash{};   // NSDMI: zero — Family-A persistent
    RowHash     row_hash{};       // NSDMI: zero — Family-A persistent

    // Default ctor is constexpr-implicit via NSDMI.  Defaulted <=>
    // forms a strict weak order on (content_hash, row_hash) — content
    // axis is the major key, row axis breaks ties.  Sufficient for
    // flat_map / sorted-array storage.
    auto operator<=>(const KernelCacheKey&) const noexcept = default;

    // is_zero(): both axes at default — typically means "unset", not
    // "matches a real region".  Real region hashes are extremely
    // unlikely to collide with zero by accident (FNV/fmix64 avalanche
    // properties), but the sentinel() / is_sentinel() pair below is
    // the *guaranteed* unused value for end-of-region markers.
    [[nodiscard]] constexpr bool is_zero() const noexcept {
        return !content_hash && !row_hash;
    }

    // sentinel(): both axes at UINT64_MAX — guaranteed never produced
    // by any real hash function.  Used as the EMPTY-slot marker in
    // the open-addressing probe table (KernelCache Entry layout —
    // MerkleDag.h §979).
    [[nodiscard]] static constexpr KernelCacheKey sentinel() noexcept {
        return KernelCacheKey{ContentHash::sentinel(), RowHash::sentinel()};
    }

    [[nodiscard]] constexpr bool is_sentinel() const noexcept {
        return content_hash.is_sentinel() && row_hash.is_sentinel();
    }
};

// Layout invariant — the key is exactly two 64-bit hashes, no
// padding.  KernelCache slot probes assume this for AoS / SoA
// flexibility (FOUND-I05/06/07 will choose per cache level).
static_assert(sizeof(KernelCacheKey) == 16,
              "KernelCacheKey must be exactly 16 bytes "
              "(ContentHash + RowHash, no padding).");
static_assert(alignof(KernelCacheKey) == 8,
              "KernelCacheKey must be 8-byte aligned for atomic-pair "
              "compatibility on x86-64 / aarch64.");

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(KernelCacheKey);

} // namespace crucible
