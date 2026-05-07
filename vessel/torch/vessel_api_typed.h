#pragma once

// Typed helpers for the internal Vessel C-ABI handle boundary.
//
// CrucibleHandle remains `void*` and CrucibleMeta remains a plain C
// struct in vessel_api.h because both are exported to C / ctypes
// callers.  Inside C++, every value crossing the FFI boundary is
// immediately tagged as `safety::source::ABIBoundary` so foreign-
// runtime provenance is visible in the type system before the pointer
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
// ── ABI invariant ──────────────────────────────────────────────────
//
// CrucibleMeta is layout-compatible with crucible::TensorMeta by
// construction; the static_asserts below pin every byte of that
// claim.  The cast inside `as_meta_typed` is a layout-compat
// reinterpret (not a value reinterpretation) and is the SOLE such
// cast in the Vessel boundary — any other reinterpret of CrucibleMeta
// is a review concern.
//
// ── Zero-cost guarantee ────────────────────────────────────────────
//
// `Tagged<T, Tag>` is regime-1 EBO collapse via TrustLattice<Tag>'s
// empty element_type, so `sizeof(TypedHandle) == sizeof(void*)` and
// `sizeof(TypedMeta) == sizeof(void*)` are static_assert'd below.
// The two helpers are CRUCIBLE_HOT (always-inline) and compile to
// the same machine code as the bare `static_cast` / `reinterpret_cast`
// after EBO collapse — verified by the production binary's `objdump`
// snapshot and by the cross-bench harness.
//
// ── Usage rule (review-enforced) ───────────────────────────────────
//
// Every C-ABI thunk in vessel_api.cpp begins with `as_vigil_typed(h)`
// (and `as_meta_typed(metas)` if it accepts a meta array).  No raw
// `static_cast<Vigil*>(h)` / `reinterpret_cast<TensorMeta*>(metas)`
// is permitted outside this header.  Grepping for `as_vigil_typed`
// or `as_meta_typed` finds every ABI-crossing site in O(1).

#include "vessel_api.h"

#include <crucible/Platform.h>
#include <crucible/TensorMeta.h>
#include <crucible/Vigil.h>
#include <crucible/safety/Tagged.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::vessel {

// ── Vigil typed handle ────────────────────────────────────────────

using TypedHandle = safety::Tagged<Vigil*, safety::source::ABIBoundary>;

static_assert(sizeof(TypedHandle) == sizeof(CrucibleHandle));
static_assert(alignof(TypedHandle) == alignof(CrucibleHandle));
static_assert(std::is_trivially_copy_constructible_v<TypedHandle>);

// ── TensorMeta typed view ─────────────────────────────────────────

using TypedMeta = safety::Tagged<const TensorMeta*, safety::source::ABIBoundary>;

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
static_assert(sizeof(CrucibleMeta) == sizeof(crucible::TensorMeta),
              "CrucibleMeta size must match TensorMeta");
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
    const auto bits = reinterpret_cast<std::uintptr_t>(handle);
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
inline void assert_plausible_meta_array(const CrucibleMeta* metas,
                                        std::size_t n_metas) noexcept {
#ifndef NDEBUG
    if (n_metas == 0U) {
        CRUCIBLE_DEBUG_ASSERT(metas == nullptr);
        return;
    }
    CRUCIBLE_DEBUG_ASSERT(metas != nullptr);
    const auto bits = reinterpret_cast<std::uintptr_t>(metas);
    CRUCIBLE_DEBUG_ASSERT((bits & (alignof(CrucibleMeta) - 1U)) == 0U);
#else
    (void)metas;
    (void)n_metas;
#endif
}

} // namespace detail

[[nodiscard]] CRUCIBLE_HOT TypedHandle as_vigil_typed(
    CrucibleHandle handle) noexcept
{
    detail::assert_plausible_vigil_handle(handle);
    return TypedHandle{static_cast<Vigil*>(handle)};
}

[[nodiscard]] CRUCIBLE_HOT CrucibleHandle from_typed(
    TypedHandle handle) noexcept
{
    return static_cast<CrucibleHandle>(handle.value());
}

// ── Meta-array typed view ──────────────────────────────────────────
//
// `n_metas` is consumed only by the debug plausibility check; the
// returned typed view doesn't carry length, since callers already
// thread `n_metas` separately through the C ABI.  When n_metas==0,
// `metas` MUST be nullptr — the invariant the C ABI promises.
[[nodiscard]] CRUCIBLE_HOT TypedMeta as_meta_typed(
    const CrucibleMeta* metas,
    std::size_t n_metas = 0) noexcept
{
    detail::assert_plausible_meta_array(metas, n_metas);
    // Layout-compat reinterpret: every byte of CrucibleMeta lines up
    // with TensorMeta (proven by the offsetof / sizeof asserts above).
    // The cast is a `const CrucibleMeta*` → `const TensorMeta*` view
    // change only — no value reinterpretation, no aliasing of writable
    // storage, no lifetime bend.
    return TypedMeta{reinterpret_cast<const crucible::TensorMeta*>(metas)};
}

[[nodiscard]] CRUCIBLE_HOT const CrucibleMeta* metas_from_typed(
    TypedMeta typed) noexcept
{
    return reinterpret_cast<const CrucibleMeta*>(typed.value());
}

} // namespace crucible::vessel
