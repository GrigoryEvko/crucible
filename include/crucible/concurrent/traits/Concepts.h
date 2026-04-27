#pragma once

// ═══════════════════════════════════════════════════════════════════
// concurrent/traits/Concepts.h — formal concepts for the unified
// Permissioned* wrapper surface.
//
// Every Permissioned* wrapper in concurrent/ exposes a uniform API
// surface that the future signature dispatcher (27_04 doc §5.5) and
// scheduler-policy types can read structurally.  The concepts in this
// header are the contract.
//
// Topology axis (the load-bearing dichotomy):
//
//   Pool-based:   producer-side OR consumer-side (or both) is a
//                 fractional SharedPermissionPool.  Mode-transition
//                 takes no Permission argument — the pool's atomic
//                 state is the proof of quiescence.
//                 Members:  with_drained_access(Body) -> bool
//
//   Linear-only:  every endpoint holds a linear (move-only)
//                 Permission<Tag>.  No atomic pool to drain — the
//                 caller must surrender the recombined whole
//                 permission as type-level proof.
//                 Members:  with_recombined_access(P&&, Body) -> P
//
// Universal axes (every wrapper exposes these):
//
//   * value_type / user_tag / whole_tag typedefs
//   * is_exclusive_active() -> bool   (constexpr false for linear-only)
//   * capacity() -> size_t
//
// Diagnostic axes (FIFO-shaped wrappers; AtomicSnapshot is
// latest-value, deliberately omits these):
//
//   * empty_approx() -> bool
//   * size_approx() -> size_t
//
// ─── How the dispatcher uses these ────────────────────────────────
//
// concept-overloaded `dispatch(channel, ...)` discriminates on
// PoolBased / LinearOnly to pick the right mode-transition shape.
// The handle factories' shapes (linear / pool / indexed) are
// orthogonal — covered by separate per-handle concepts.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/permissions/Permission.h>

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::concurrent::traits {

// ── Universal typedef set ─────────────────────────────────────────

template <typename Ch>
concept HasUnifiedTypedefs =
    requires { typename Ch::value_type; }
 && requires { typename Ch::user_tag; }
 && requires { typename Ch::whole_tag; };

// ── capacity() — every fixed-cap wrapper ──────────────────────────

template <typename Ch>
concept HasCapacity =
    requires { { Ch::capacity() } noexcept -> std::same_as<std::size_t>; };

// ── is_exclusive_active() — every wrapper ─────────────────────────

template <typename Ch>
concept HasIsExclusiveActive =
    requires(const Ch& c) {
        { c.is_exclusive_active() } noexcept -> std::same_as<bool>;
    };

// ── empty_approx() / size_approx() — FIFO-shaped wrappers ─────────
//
// AtomicSnapshot is latest-value (not FIFO); these concepts hold for
// every other wrapper.

template <typename Ch>
concept HasEmptyApprox =
    requires(const Ch& c) {
        { c.empty_approx() } noexcept -> std::same_as<bool>;
    };

template <typename Ch>
concept HasSizeApprox =
    requires(const Ch& c) {
        { c.size_approx() } noexcept -> std::same_as<std::size_t>;
    };

// ── Mode-transition: pool-based ──────────────────────────────────
//
// Mpsc / Mpmc / ChaseLevDeque / Snapshot satisfy this.  Body is a
// noexcept void-returning callable; return is bool (true iff body
// ran, false iff pool was busy).

template <typename Ch>
concept HasPoolDrainedAccess =
    requires(Ch& c) {
        { c.with_drained_access([]() noexcept {}) }
            -> std::same_as<bool>;
    };

// ── Mode-transition: linear-only ──────────────────────────────────
//
// Spsc / ShardedGrid / CalendarGrid satisfy this.  Caller surrenders
// the recombined whole permission; body always runs (no failure
// path); whole permission is returned for re-split.

template <typename Ch>
concept HasLinearRecombinedAccess =
    requires(Ch& c, safety::Permission<typename Ch::whole_tag> p) {
        { c.with_recombined_access(std::move(p), []() noexcept {}) }
            -> std::same_as<safety::Permission<typename Ch::whole_tag>>;
    };

// ── Topology classification (mutually exclusive) ──────────────────
//
// Every Permissioned wrapper satisfies exactly one of these.

template <typename Ch>
concept PoolBasedChannel =
    HasUnifiedTypedefs<Ch>
 && HasIsExclusiveActive<Ch>
 && HasPoolDrainedAccess<Ch>;

template <typename Ch>
concept LinearOnlyChannel =
    HasUnifiedTypedefs<Ch>
 && HasIsExclusiveActive<Ch>
 && HasLinearRecombinedAccess<Ch>;

// ── Umbrella concept: any Permissioned wrapper ────────────────────

template <typename Ch>
concept PermissionedChannel =
    PoolBasedChannel<Ch> || LinearOnlyChannel<Ch>;

// ── FIFO-shape concept: queue-style with empty/size diagnostics ───
//
// Every Permissioned* EXCEPT Snapshot satisfies this.  The future
// dispatcher uses it to decide whether to query queue depth before
// scheduling.

template <typename Ch>
concept FifoChannel =
    PermissionedChannel<Ch>
 && HasEmptyApprox<Ch>
 && HasSizeApprox<Ch>
 && HasCapacity<Ch>;

}  // namespace crucible::concurrent::traits
