#pragma once

// Typed helpers for the internal Vessel C-ABI handle boundary.
//
// CrucibleHandle remains `void*` and CrucibleMeta remains a plain C
// struct in vessel_api.h because both are exported to C / ctypes
// callers.  Inside C++, every value crossing the FFI boundary is
// immediately tagged with the `source::ABIBoundary` provenance tag so
// foreign-runtime provenance is visible in the type system before the pointer
// reaches Vigil / TensorMeta-consuming code.
//
// ── Helper surface ─────────────────────────────────────────────────
//
//   TypedHandle               := Tagged<Vigil*, ABIBoundary>
//   as_vigil_typed(handle)    : CrucibleHandle           -> TypedHandle
//   from_typed(typed)         : TypedHandle              -> CrucibleHandle
//
//   TypedMeta                 := Tagged<const TensorMeta*, ABIBoundary>
//   as_meta_typed(metas)      : const CrucibleMeta*      -> TypedMeta
//   metas_from_typed(typed)   : TypedMeta                -> const CrucibleMeta*
//
//   TypedDataPtr              := Tagged<void*, source::External>
//   data_ptr_typed(typed, i)  : TypedMeta, std::size_t   -> TypedDataPtr
//
//   TypedSchemaName           := SchemaTable::LookupName
//   schema_name_typed(hash)   : SchemaHash               -> TypedSchemaName
//
// ── ABI invariant ──────────────────────────────────────────────────
//
// CrucibleMeta is layout-compatible with crucible::TensorMeta by
// construction; the static_asserts below pin every byte of that
// claim.  `as_meta_typed` and `metas_from_typed` change the pointer's
// view with std::bit_cast and are the SOLE pair of such casts in the
// Vessel boundary — any other cast between the two structs is a
// review concern.
//
// ── Zero-cost guarantee ────────────────────────────────────────────
//
// `Tagged<T, Tag>` is regime-1 EBO collapse via TrustLattice<Tag>'s
// empty element_type, so `sizeof(TypedHandle) == sizeof(void*)` and
// `sizeof(TypedMeta) == sizeof(void*)` are static_assert'd below.
// The two helpers are CRUCIBLE_HOT (always-inline) and compile to
// the same machine code as the bare `static_cast` / `bit_cast` after
// EBO collapse — verified by the production binary's `objdump`
// snapshot and by the cross-bench harness.
//
// ── Usage rule (review-enforced) ───────────────────────────────────
//
// Every C-ABI thunk in vessel_api.cpp begins with `as_vigil_typed(h)`
// (and `as_meta_typed(metas)` if it accepts a meta array).  No raw
// `static_cast<Vigil*>(h)` / `bit_cast<TensorMeta*>(metas)` is
// permitted outside this header.  Grepping for `as_vigil_typed` or
// `as_meta_typed` finds every ABI-crossing site in O(1).

#include "vessel_api.h"

#include <crucible/Platform.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <crucible/Vigil.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::vessel {

// ── Vigil typed handle ────────────────────────────────────────────
//
// TypedHandle, TypedMeta and TypedDataPtr use the Tagged of the
// include/crucible tree.  Two things hold them there.  The vessel
// tests, their negative-compile fixtures and two benches name the same
// types.  And vessel_api.cpp includes this header with no substrate
// fence, so an include of ::fixy here would parse crucible/Platform.h
// with the macros of foundation/Platform.h.

using TypedHandle = fixy::wrap::Tagged<Vigil*, fixy::tags::source::ABIBoundary>;

static_assert(sizeof(TypedHandle) == sizeof(CrucibleHandle));
static_assert(alignof(TypedHandle) == alignof(CrucibleHandle));
static_assert(std::is_trivially_copy_constructible_v<TypedHandle>);

// ── TensorMeta typed view ─────────────────────────────────────────

using TypedMeta = fixy::wrap::Tagged<const TensorMeta*, fixy::tags::source::ABIBoundary>;

static_assert(sizeof(TypedMeta) == sizeof(const CrucibleMeta*));
static_assert(alignof(TypedMeta) == alignof(const CrucibleMeta*));
static_assert(std::is_trivially_copy_constructible_v<TypedMeta>);

// ── Layout-compat invariants for CrucibleMeta ↔ TensorMeta ────────
//
// These are the ABI claim that justifies the `reinterpret_cast` in
// `as_meta_typed` below: a `const CrucibleMeta*` and a
// `const TensorMeta*` point at the same byte sequence.  If any
// assertion fires, the C struct is out of sync with the C++ struct —
// at which point `reinterpret_cast` becomes UB and the FFI is
// shovelling garbage to the recording pipeline.  Pinned at the typed
// helper because this header is the single home for the layout cast.
static_assert(sizeof(CrucibleMeta) == sizeof(crucible::TensorMeta), "CrucibleMeta size must match TensorMeta");
static_assert(sizeof(CrucibleMeta) == 168);
static_assert(offsetof(CrucibleMeta, sizes) == 0);
static_assert(offsetof(CrucibleMeta, strides) == 64);
static_assert(offsetof(CrucibleMeta, data_ptr) == 128);
static_assert(offsetof(CrucibleMeta, ndim) == 136);
static_assert(offsetof(CrucibleMeta, dtype) == 137);
static_assert(offsetof(CrucibleMeta, device_type) == 138);
static_assert(offsetof(CrucibleMeta, device_idx) == 139);
static_assert(offsetof(CrucibleMeta, layout) == 140);
static_assert(offsetof(CrucibleMeta, requires_grad) == 141);
static_assert(offsetof(CrucibleMeta, flags) == 142);
static_assert(offsetof(CrucibleMeta, output_nr) == 143);
static_assert(offsetof(CrucibleMeta, storage_offset) == 144);
static_assert(offsetof(CrucibleMeta, version) == 152);
static_assert(offsetof(CrucibleMeta, storage_nbytes) == 156);
static_assert(offsetof(CrucibleMeta, grad_fn_hash) == 160);

namespace detail {

inline void assert_plausible_vigil_handle(CrucibleHandle handle) noexcept {
#ifndef NDEBUG
    const auto bits = std::bit_cast<std::uintptr_t>(handle);
    CRUCIBLE_DEBUG_ASSERT(handle != nullptr);
    CRUCIBLE_DEBUG_ASSERT((bits & (alignof(Vigil) - 1U)) == 0U);
    CRUCIBLE_DEBUG_ASSERT(bits >= 4096U);
#else
    (void)handle;
#endif
}

// CrucibleMeta is layout-compatible with TensorMeta by construction
// (asserted above).  Plausibility check guards the alignment of the
// caller-supplied array AND its non-emptiness — n_metas==0 is allowed
// with a null pointer; otherwise both must agree.  Debug-only because
// the assertions are infallible in production code paths but catch
// FFI corruption in test builds.
inline void assert_plausible_meta_array(const CrucibleMeta* metas, std::size_t n_metas) noexcept {
#ifndef NDEBUG
    if (n_metas == 0U) {
        CRUCIBLE_DEBUG_ASSERT(metas == nullptr);
        return;
    }
    CRUCIBLE_DEBUG_ASSERT(metas != nullptr);
    const auto bits = std::bit_cast<std::uintptr_t>(metas);
    CRUCIBLE_DEBUG_ASSERT((bits & (alignof(CrucibleMeta) - 1U)) == 0U);
#else
    (void)metas;
    (void)n_metas;
#endif
}

}  // namespace detail

[[nodiscard]] CRUCIBLE_HOT TypedHandle as_vigil_typed(CrucibleHandle handle) noexcept {
    detail::assert_plausible_vigil_handle(handle);
    return TypedHandle{static_cast<Vigil*>(handle)};
}

[[nodiscard]] CRUCIBLE_HOT CrucibleHandle from_typed(TypedHandle handle) noexcept {
    return static_cast<CrucibleHandle>(handle.value());
}

// ── Meta-array typed view ──────────────────────────────────────────
//
// `n_metas` is consumed only by the debug plausibility check; the
// returned typed view doesn't carry length, since callers already
// thread `n_metas` separately through the C ABI.  When n_metas==0,
// `metas` MUST be nullptr — the invariant the C ABI promises.
[[nodiscard]] CRUCIBLE_HOT TypedMeta as_meta_typed(const CrucibleMeta* metas, std::size_t n_metas = 0) noexcept {
    detail::assert_plausible_meta_array(metas, n_metas);
    // A `const CrucibleMeta*` and a `const TensorMeta*` are both object
    // pointers of one width, so bit_cast reproduces the address and the
    // view changes without the value changing.
    //
    // What makes the read through the result sound is the offsetof and
    // sizeof block above, not the spelling of the cast: the two structs
    // are layout-compatible byte for byte, and any drift fires a
    // static_assert here before a caller can reach the pipeline with a
    // mismatched struct. std::start_lifetime_as is the other candidate
    // and does not apply — TensorMeta carries member initializers, so
    // its default constructor is not trivial and the type is not an
    // implicit-lifetime type.
    return TypedMeta{std::bit_cast<const crucible::TensorMeta*>(metas)};
}

[[nodiscard]] CRUCIBLE_HOT const CrucibleMeta* metas_from_typed(TypedMeta typed) noexcept {
    // The inverse view change, sound by the same layout block.
    return std::bit_cast<const CrucibleMeta*>(typed.value());
}

// ── Per-meta data_ptr typed accessor (GAPS-096) ────────────────────
//
// Crucible's TensorMeta::data_ptr is a `void*` whose provenance is
// "data pages PyTorch handed to us — externally-owned, lifetime
// controlled by the autograd / caching-allocator graph on the Python
// side".  Wrap it as `Tagged<void*, source::External>` so downstream
// consumers (memory-plan recording, pool-shadow-handle binding, Cipher
// snapshot serializer) carry the provenance into their own type-level
// reasoning.  The wire-struct layout is unchanged — this helper is a
// pure read-and-tag operation.
//
// `i` MUST be a valid index in the typed meta array (caller is the
// authority on the array length, which travels via `n_metas` through
// the C ABI).  Debug builds tighten this with a contract.

using TypedDataPtr = fixy::wrap::Tagged<void*, fixy::tags::source::External>;

static_assert(sizeof(TypedDataPtr) == sizeof(void*));
static_assert(alignof(TypedDataPtr) == alignof(void*));
static_assert(std::is_trivially_copy_constructible_v<TypedDataPtr>);

[[nodiscard]] CRUCIBLE_HOT TypedDataPtr data_ptr_typed(TypedMeta typed, std::size_t i) noexcept {
    const TensorMeta* metas = typed.value();
    CRUCIBLE_DEBUG_ASSERT(metas != nullptr);
    return TypedDataPtr{metas[i].data_ptr};
}

// ── Schema-name typed lookup (GAPS-096) ────────────────────────────
//
// `crucible::schema_name(SchemaHash)` returns a typed borrow from the
// global SchemaTable's interned-name storage.  Names are
// SanitizedName-validated at registration (length-bounded, NUL-walked,
// stripped of "aten::" prefix as appropriate), so the returned
// borrow's provenance is `source::Sanitized` — distinct from the raw
// FFI input (`source::External`) that crucible_register_schema_name
// receives.  The Borrowed<..., SchemaTable> payload also keeps the
// owner lifetime visible to downstream consumers.
//
// Returns an empty typed borrow when the hash is unknown — callers
// branch on `.value().data() == nullptr`.  Lifetime: borrowed from the
// SchemaTable; valid for the lifetime of the program once registered.

using TypedSchemaName = crucible::SchemaTable::LookupName;

static_assert(sizeof(TypedSchemaName) == sizeof(crucible::SchemaTable::BorrowedName));
static_assert(alignof(TypedSchemaName) == alignof(crucible::SchemaTable::BorrowedName));
static_assert(std::is_trivially_copy_constructible_v<TypedSchemaName>);

[[nodiscard]] CRUCIBLE_HOT TypedSchemaName schema_name_typed(crucible::SchemaHash schema_hash) noexcept {
    return crucible::schema_name(schema_hash);
}

// ── Recording trust ladder ─────────────────────────────────────────
//
// Every Entry this adapter builds is filled from values a foreign
// runtime supplied: the C ABI hands over raw integers, the boxed
// fallback reads an ATen stack, and the unboxed kernels read their own
// typed arguments.  All three start at
// `crucible::TraceRing::FromPytorchEntryPtr`, and `Vigil::record_op`
// and `Vigil::dispatch_op` take `crucible::TraceRing::ValidatedEntryPtr`.
// The retag catalog admits exactly one transition between those two
// tags, so the checks below are the whole route from an adapter to the
// ring.  The boxed fallback and the C ABI build entries from data that
// has no compile-time bound, and they run the checks at run time
// through `mint_validated_entry`.  The unboxed kernels discharge the
// same checks at compile time: record_kernel.h asserts that
// `entry_is_well_formed` and `metas_are_well_formed` accept the
// largest entry each operator can build, and then retags without the
// runtime walk.
//
// Three adapters share one ladder deliberately.  A check that lives in
// one adapter is a check the next adapter does not have, and this file
// already holds every other rule the boundary obeys.

// Structural bounds on an Entry.  Each one names a downstream array
// the recording pipeline indexes with the field:
//
//   - schema_hash != 0, because 0 is the invalid sentinel and the
//     content hash and the region cache both key on this field
//   - num_inputs and num_outputs each at most 64, the TensorMeta
//     array cap the MetaLog appends against
//   - num_scalar_args at most 5, the width of Entry::scalar_values.
//     BackgroundThread asserts the same bound when it drains the ring,
//     so a larger count reaches a contract violation rather than the
//     pipeline; the adapter rejects the operation here instead.
[[nodiscard]] CRUCIBLE_HOT constexpr bool entry_is_well_formed(const crucible::TraceRing::Entry& entry) noexcept {
    if (entry.schema_hash.raw() == 0) return false;
    if (entry.num_inputs > 64) return false;
    if (entry.num_outputs > 64) return false;
    if (entry.num_scalar_args > decltype(crucible::TraceRing::Entry::scalar_values)::capacity) return false;
    return true;
}

// The structural invariant of TensorMeta is that sizes[] and strides[]
// are fixed arrays of kMaxTensorNDim entries.  An ndim past that makes
// every per-dimension loop in the recording pipeline read out of
// bounds.
//
// A count of zero is well formed whatever the pointer is, and the
// pointer is deliberately not required to be null there.  An operation
// carrying no tensors is ordinary, and two of the three adapters reach
// this with the address of a fixed array they never filled, which is
// not null and is never read because the count is zero.  The C ABI
// states the stronger rule that the two travel together, and
// assert_plausible_meta_array is where that rule is checked, on the one
// path that promises it.
//
// Cost is one byte compare per meta.  A typical operation carries two
// to eight metas, and the dispatch that reached here is orders of
// magnitude larger.
[[nodiscard]] CRUCIBLE_HOT constexpr bool metas_are_well_formed(const crucible::TensorMeta* metas,
                                                                uint32_t n_metas) noexcept {
    if (n_metas == 0) return true;
    if (metas == nullptr) return false;
    for (uint32_t i = 0; i < n_metas; ++i) {
        if (metas[i].ndim > crucible::kMaxTensorNDim) return false;
    }
    return true;
}

// The tag a recording entry point accepts.  TraceRing owns the pointer
// type, so the vessel reads the tag from it and names no substrate
// namespace of its own.  The two retags in the vessel use this alias.
using ValidatedEntryTag = crucible::TraceRing::ValidatedEntryPtr::tag_type;

// The runtime door from the first tag to the second.  An empty return says
// no certification exists for this operation, and the caller executes
// eagerly and leaves the ring alone.  There is no failure value that
// carries the second tag, which is what makes running the checks the
// only way an adapter reaches a recording entry point.
[[nodiscard]] CRUCIBLE_HOT constexpr std::optional<crucible::TraceRing::ValidatedEntryPtr>
mint_validated_entry(crucible::TraceRing::FromPytorchEntryPtr raw, const crucible::TensorMeta* metas,
                     uint32_t n_metas) noexcept {
    const crucible::TraceRing::Entry* entry = raw.value();
    if (entry == nullptr) return std::nullopt;
    if (!entry_is_well_formed(*entry)) return std::nullopt;
    if (!metas_are_well_formed(metas, n_metas)) return std::nullopt;
    return std::move(raw).retag<ValidatedEntryTag>();
}

}  // namespace crucible::vessel
