#pragma once

// ── crucible::safety::extract::is_session_handle_v ──────────────────
//
// FOUND-D08-extra of 27_04_2026.md §5.5 + 28_04_2026_effects.md §6.1.
// The dispatcher-side reading-surface predicate that recognizes
// session-typed handles — every type that inherits (directly or
// indirectly) from `crucible::safety::proto::SessionHandleBase<Proto,
// Derived>` for some Proto.
//
// The session-type stack publishes `SessionHandle<Proto, Resource,
// LoopCtx>` (binary), the multiparty MPST handle family, the
// crash-stop variants (`SessionHandle<CrashAware<...>, ...>`), the
// checkpointed variants (`SessionHandle<CheckpointedSession<...>,
// ...>`), and the FOUND-C v1 `PermissionedSessionHandle<Proto, PS,
// Resource, LoopCtx>` — all of which derive from
// `SessionHandleBase`.  This predicate detects the family.
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_session_handle_v<T>     Variable template: true iff T (after
//                              cv-ref stripping) inherits from
//                              `SessionHandleBase<P, D>` for some P
//                              and D — covering every session-handle
//                              specialisation in `sessions/*`.
//
//   IsSessionHandle<T>         Concept form for `requires`-clauses.
//
//   session_handle_proto_t<T>  Alias to the Proto template argument
//                              extracted from T's SessionHandleBase
//                              base.  Constrained on
//                              is_session_handle_v; ill-formed
//                              otherwise.
//
// ── Detection strategy: convertible-to-base ─────────────────────────
//
// `SessionHandleBase` is a CRTP-friendly base; every session-typed
// handle inherits from it.  `std::is_convertible_v<T*,
// SessionHandleBase<P, D>*>` admits T iff T inherits publicly from
// the base.  We recover (P, D) by exhaustive search via a
// `to_base` overload set: if `to_base(declval<T*>())` resolves to a
// pointer-to-`SessionHandleBase<P, D>`, we extract P, D from the
// resolved overload's signature.
//
// In practice the implementation is:
//
//   1. Define an overload `to_base(SessionHandleBase<P, D>*) -> SessionHandleBase<P, D>*`
//      (template, deduces P and D).
//   2. Apply via `decltype(detail::to_base(std::declval<T*>()))`.
//   3. If the call is ambiguous (multiple base specialisations) or
//      ill-formed (T does not inherit from any SessionHandleBase),
//      the detection variable falls back to false.
//
// ── Why public inheritance is required ──────────────────────────────
//
// Private inheritance from SessionHandleBase would not expose the
// base, defeating the convertible-to-base check.  This is
// intentional: every shipping session-handle wrapper inherits
// PUBLICLY from SessionHandleBase to expose the consumed-tracker
// destructor diagnostic.  The detection mirrors the design.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval predicate.
//   TypeSafe — base-conversion is the ONLY admission path; no
//              implicit conversion across protocols, no silent
//              flattening of distinct Proto specialisations.
//   DetSafe — same T → same predicate value.

#include <crucible/sessions/Session.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── detail: convertible-to-base detection ──────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// Overload: given a pointer to SessionHandleBase<P, D>, deduce P and D
// and return a pointer of the same base type.  Used as the SFINAE
// probe in the `to_base_or_null` decomposition below.
template <typename Proto, typename Derived>
::crucible::safety::proto::SessionHandleBase<Proto, Derived>*
to_session_base(::crucible::safety::proto::SessionHandleBase<Proto, Derived>*) noexcept;

// Fallback — chosen when T does not derive from any
// SessionHandleBase<...>.  Returns void* so the SFINAE branch sees a
// distinct return type.  We never call these functions; only their
// return types matter.
void* to_session_base(...) noexcept;

// SFINAE-friendly conversion probe.
template <typename T>
using session_base_probe_t =
    decltype(detail::to_session_base(std::declval<T*>()));

// is_session_handle_impl: true iff session_base_probe_t<T> resolves
// to a SessionHandleBase<P, D>* (i.e., the templated overload won
// over the void* fallback).
template <typename T>
struct is_session_handle_impl {
    static constexpr bool value = !std::is_same_v<
        session_base_probe_t<T>, void*>;
};

// Proto extractor: decompose the resolved base-pointer type.
template <typename M>
struct session_base_decomp {
    using proto = void;
    using derived = void;
    static constexpr bool matches = false;
};

template <typename Proto, typename Derived>
struct session_base_decomp<
    ::crucible::safety::proto::SessionHandleBase<Proto, Derived>*>
{
    using proto = Proto;
    using derived = Derived;
    static constexpr bool matches = true;
};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Public surface ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr bool is_session_handle_v =
    detail::is_session_handle_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSessionHandle = is_session_handle_v<T>;

template <typename T>
    requires is_session_handle_v<T>
using session_handle_proto_t =
    typename detail::session_base_decomp<
        detail::session_base_probe_t<std::remove_cvref_t<T>>>::proto;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Negative side fully checked in-header.  Positive side (real
// SessionHandle / PermissionedSessionHandle) lives in the sentinel TU
// which can pull in sessions/SpscSession.h etc. without bloating
// this header's transitive include surface.

namespace detail::is_session_handle_self_test {

// Plain types are NOT session handles.
static_assert(!is_session_handle_v<int>);
static_assert(!is_session_handle_v<int*>);
static_assert(!is_session_handle_v<void>);
static_assert(!is_session_handle_v<char>);

// A foreign type that has no relation to SessionHandleBase — rejected.
struct foreign_type { int x; };
static_assert(!is_session_handle_v<foreign_type>);

// Pointer-to-handle is NOT a handle (remove_cvref does not strip ptrs).
struct foreign_with_unrelated_base { int y; };
static_assert(!is_session_handle_v<foreign_with_unrelated_base*>);

// IsSessionHandle concept rejects non-handles.
static_assert(!IsSessionHandle<int>);
static_assert(!IsSessionHandle<foreign_type>);

}  // namespace detail::is_session_handle_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Header-side smoke covers negative cases only (positive matrix in
// the sentinel TU).

inline bool is_session_handle_smoke_test() noexcept {
    using namespace detail::is_session_handle_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !is_session_handle_v<int>;
        ok = ok && !is_session_handle_v<void>;
        ok = ok && !is_session_handle_v<foreign_type>;
        ok = ok && !IsSessionHandle<foreign_type>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
