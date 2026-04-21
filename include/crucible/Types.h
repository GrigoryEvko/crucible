#pragma once

#include <crucible/Platform.h>

#include <cstdint>
#include <utility>

namespace crucible {

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
CRUCIBLE_CONST constexpr uint8_t element_size(ScalarType t) noexcept {
  switch (t) {
    case ScalarType::Bool:
    case ScalarType::Byte:
    case ScalarType::Char:
    case ScalarType::Float8_e5m2:
    case ScalarType::Float8_e4m3fn:
    case ScalarType::Float8_e5m2fnuz:
    case ScalarType::Float8_e4m3fnuz:
      return 1;
    case ScalarType::Short:
    case ScalarType::Half:
    case ScalarType::BFloat16:
      return 2;
    case ScalarType::Int:
    case ScalarType::Float:
    case ScalarType::ComplexHalf:
      return 4;
    case ScalarType::Long:
    case ScalarType::Double:
    case ScalarType::ComplexFloat:
      return 8;
    case ScalarType::ComplexDouble:
      return 16;
    case ScalarType::Undefined:
      return 0;
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
    uint32_t v;                                                            \
    constexpr explicit Name(uint32_t val) noexcept : v(val) {}             \
    constexpr Name() noexcept : v(UINT32_MAX) {}                           \
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
    uint64_t v;                                                            \
    constexpr explicit Name(uint64_t val) noexcept : v(val) {}             \
    constexpr Name() noexcept : v(0) {}                                    \
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
//   (future) RecipeHash / KernelContentHash / PlanHash — FORGE.md §19, §23
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

} // namespace crucible
