#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto::PermissionedSessionHandle<Proto, PS,
// Resource, LoopCtx> — the central thesis of FOUND-C.
//
// Phase 3 of FOUND-C (#610–#615).  See `misc/27_04_csl_permission_
// session_wiring.md` §8 for the full spec.  Phase 4 ships
// session_fork; Phase 5 ships OneShotFlag crash transport.
//
// ─── Three-axis composition ────────────────────────────────────────
//
//   Axis 1 — Graded value invariants     (Refined / Tagged / Secret …)
//   Axis 2 — CSL permissions             (Permission<Tag>, splits_into …)
//   Axis 3 — Session protocols           (Send / Recv / Loop / End …)
//
// PermissionedSessionHandle weaves all three.  It is a CRTP-inheriting
// wrapper over `SessionHandle<Proto, Resource, LoopCtx>` from
// `sessions/Session.h` that adds a phantom type-level PermSet<Tags...>
// (carried as `[[no_unique_address]] PS` for zero-byte cost) and
// evolves it on every send / recv / pick / branch / close per the
// payload's permission-flow marker (`SessionPermPayloads.h`).
//
// ─── Design decisions (from the wiring plan §0.4) ──────────────────
//
//   D1 — All fifteen FOUND-C tasks ship in v1; no deferrals.
//   D2 — session_fork ships in v1 (Phase 4) for plain-mergeable
//        protocols; diverging-multiparty waits on coinductive merging.
//   D3 — Loop body permission balance is MANDATORY at compile time:
//        every Continue site asserts perm_set_equal_v<PS_at_continue,
//        PS_at_loop_entry>.  Forces each iteration to balance.
//   D4 — Select / Offer cross-branch PermSet convergence is enforced
//        STRUCTURALLY: every branch's terminal head (End / Stop /
//        Continue) carries its own static_assert on PS, so all branches
//        meeting at the same terminal must converge to the same PS by
//        construction.  No separate convergence metafunction needed.
//   D5 — Debug-mode abandonment-tracker enrichment lists the LEAKED
//        permission tags before the base SessionHandleBase destructor
//        prints its standard diagnostic and aborts.  Zero release cost.
//   D6 — is_permission_balanced_v<Γ, InitialPerms> SHIPPED standalone
//        (GAPS-002) in `sessions/SessionContext.h` as the structural
//        delta-fold over Γ's entries.  NOT conjuncted with is_safe_v
//        (unshipped, Task #346 / wiring plan §11 bounded-LTS walk)
//        per the Part IX honest-assessment discipline.
//   D7 — Doc-update sweep is bundled with the implementation PR.
//
// ─── Why CRTP over composition ─────────────────────────────────────
//
//   * Diagnostic naming.  SessionHandleBase's wrapper-name reflection
//     reads "PermissionedSessionHandle" when Derived is passed; the
//     abandonment diagnostic distinguishes a PSH abort from a bare
//     SessionHandle abort, a CrashWatchedHandle abort, or a
//     RecordingSessionHandle abort even when all four share the same
//     Proto.  Composition would force a misreport.
//   * Zero-overhead sizeof.  PS is empty (sizeof = 1, EBO-collapsible
//     to 0); SessionHandleBase's tracker_ is empty in release; the
//     handle's only non-empty member is the Resource.  Composition
//     would add a pointer-to-inner-handle, breaking the zero-cost
//     claim.
//   * Existing precedent.  bridges/CrashTransport.h::CrashWatchedHandle
//     and bridges/RecordingSessionHandle.h::RecordingSessionHandle
//     already use CRTP per protocol head.  Following the same pattern
//     keeps the framework's mental model consistent.
//
// ─── PermSet evolution per protocol head ───────────────────────────
//
//   Send<Plain T, K>             PS' = PS                    (unchanged)
//   Send<Transferable<T, X>, K>  PS' = perm_set_remove_t<PS, X>
//   Send<Borrowed<T, X>, K>      PS' = PS                    (scoped lend)
//   Send<Returned<T, X>, K>      PS' = perm_set_remove_t<PS, X>
//   Send<DelegatedSession<P, S>, K>
//                                PS' = perm_set_difference_t<PS, S>
//   Recv<Plain T, K>             PS' = PS                    (unchanged)
//   Recv<Transferable<T, X>, K>  PS' = perm_set_insert_t<PS, X>
//   Recv<Borrowed<T, X>, K>      PS' = PS                    (ReadView only)
//   Recv<Returned<T, X>, K>      PS' = perm_set_insert_t<PS, X>
//   Recv<DelegatedSession<P, S>, K>
//                                PS' = perm_set_union_t<PS, S>
//
// All four payload markers are normalised to the same dispatch via
// compute_perm_set_after_send_t / compute_perm_set_after_recv_t in
// `sessions/SessionPermPayloads.h`.
//
// ─── Delegate / Accept integration (GAPS-058) ──────────────────────
//
// Higher-order session delegation uses `DelegatedSession<P, InnerPS>`
// as the protocol payload in `Delegate<DelegatedSession<P, InnerPS>, K>`
// and `Accept<DelegatedSession<P, InnerPS>, K>`.
//
//   Delegate consumes a PermissionedSessionHandle<P, ActualInnerPS, ...>
//   and statically requires ActualInnerPS == InnerPS.  The delegator
//   loses the inner permissions because the inner handle is consumed.
//
//   Accept receives the endpoint resource and mints a
//   PermissionedSessionHandle<P, InnerPS, ...>.  The accepter gains
//   those permissions through the returned inner handle.  Its carrier
//   PS must be disjoint from InnerPS so the same CSL token is not held
//   simultaneously by the carrier and the delegated endpoint.
//
// ─── Crash transport composition (FOUND-C Phase 5) ─────────────────
//
// PSH composes with the existing OneShotFlag-based crash signal
// (`safety::OneShotFlag`, `bridges/CrashTransport.h::CrashWatchedHandle`)
// via the framework's typed-detach idiom.  v1 ships the COMPOSITION
// PATTERN — the `with_crash_check_or_detach` helper below + a
// worked-example test in `test_permissioned_session_handle.cpp` —
// rather than a fused `CrashWatchedPermissionedSessionHandle` class.
// Per the wiring plan §10.3, the crash-stop discipline is:
//
//   1. Peek the OneShotFlag before each PSH op.
//   2. If set: `std::move(h).detach(detach_reason::TransportClosedOutOfBand{})`
//      drops the type-level PS (permissions are NOT recovered from a
//      crashed peer — BSYZ22 crash-stop), and leaves the underlying
//      Resource recoverable via the channel reference the user holds
//      separately.
//   3. If clear: proceed with the normal PSH op (send/recv/...).
//
// The pattern preserves PSH's send/recv API (no std::expected wrapping,
// no API breakage) while giving the framework a typed escape hatch
// for crash-driven cleanup.  Production callers wanting the type
// system to AUTOMATICALLY check the flag at every PSH op (and return
// std::expected<NextHandle, CrashEvent>) should use
// `bridges/CrashTransport.h::CrashWatchedHandle` directly (which
// gives crash-aware send/recv) and layer permission tracking outside
// the type system, OR wait for v2's
// `CrashWatchedPermissionedSessionHandle` fused class.
//
// ─── Scope (what this header does NOT include) ─────────────────────
//
//   * session_fork — Phase 4.  Stub provided that fires
//     static_assert(false) directing the caller to Phase 4 once it
//     lands.
//   * Fused CrashWatchedPermissionedSessionHandle (single class with
//     BOTH crash awareness AND PS evolution) — v2.  v1 composes via
//     the `with_crash_check_or_detach` helper below (Phase 5 lite).
//   * is_permission_balanced_v<Γ, InitialPerms> — SHIPPED (GAPS-002)
//     in `sessions/SessionContext.h`.  Structural delta-fold over Γ's
//     entries: balanced iff per-Tag send/recv counts match AND every
//     transferred tag is in InitialPerms.  Borrowed contributes
//     nothing.  Not conjuncted with the unshipped is_safe_v (Task
//     #346, bounded LTS walk per wiring plan §11).
//   * Negative-compile harness — Phase 7 (shipped).
//   * Bench — Phase 8 (shipped).
//   * Doc-update sweep — Phase 9.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/permissions/PermSet.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionPermPayloads.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstdio>
#include <optional>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════
// ── LoopContext<Body, EntryPS> — Loop-balance bookkeeping ───────
// ═════════════════════════════════════════════════════════════════
//
// PermissionedSessionHandle's LoopCtx parameter carries BOTH the
// Loop body (so Continue knows where to step back to) AND the entry
// PermSet (so Continue can static_assert balance against it per
// Decision D3).  The bare SessionHandle's LoopCtx parameter is just
// the Body, with no PS bookkeeping — the bare framework has no PS
// to balance.

template <typename Body, typename EntryPS>
struct LoopContext {
    using body            = Body;
    using entry_perm_set  = EntryPS;
};

// ═════════════════════════════════════════════════════════════════
// ── Forward declaration ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════
//
// LoopCtx defaults to `void` to mirror SessionHandle's signature, so
// the top-level handle (no enclosing Loop) names just (Proto, PS,
// Resource).

template <typename Proto,
          typename PS,
          typename Resource,
          typename LoopCtx = void>
class PermissionedSessionHandle;

// Permission-aware delegation protocol marker.  `DelegatedSession` is
// not a standalone protocol head, but Delegate/Accept treat it as the
// payload that names the inner protocol plus the PermSet traveling
// with that endpoint.
template <typename InnerProto, typename InnerPS, typename K, typename LoopCtx>
struct is_well_formed<Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
                      LoopCtx>
    : std::bool_constant<
          is_well_formed<InnerProto, void>::value &&
          is_well_formed<K, LoopCtx>::value
      > {};

template <typename InnerProto, typename InnerPS, typename K, typename LoopCtx>
struct is_well_formed<Accept<DelegatedSession<InnerProto, InnerPS>, K>,
                      LoopCtx>
    : std::bool_constant<
          is_well_formed<InnerProto, void>::value &&
          is_well_formed<K, LoopCtx>::value
      > {};

// ═════════════════════════════════════════════════════════════════
// ── detail::step_to_next_permissioned ────────────────────────────
// ═════════════════════════════════════════════════════════════════
//
// Mirror of detail::step_to_next from Session.h, augmented with PS
// evolution and Loop balance enforcement.  Resolves:
//
//   * R = Continue  → step back to LoopCtx::body, asserting
//                     perm_set_equal_v<PS, LoopCtx::entry_perm_set>
//                     (Decision D3).
//   * R = Loop<B>   → enter inner Loop with LoopContext<B, PS> as
//                     the new LoopCtx (PS becomes the new entry PS).
//   * R = anything  → wrap in PermissionedSessionHandle<R, PS, Res, L>.

namespace detail {

template <typename R,
          typename PS,
          typename Resource,
          typename LoopCtx>
[[nodiscard]] constexpr auto step_to_next_permissioned(
    Resource r,
    std::source_location loc = std::source_location::current()) noexcept
{
    if constexpr (std::is_same_v<R, Continue>) {
        // Continue must have an enclosing Loop.  This is also enforced
        // by the bare framework's step_to_next, but checking here gives
        // a PSH-specific diagnostic that points at the right header.
        static_assert(!std::is_void_v<LoopCtx>,
            "crucible::session::diagnostic [Continue_Without_Loop]: "
            "PermissionedSessionHandle: Continue appears outside any "
            "enclosing Loop.  Wrap the protocol prefix containing "
            "Continue in Loop<Body>, or replace Continue with End to "
            "make the protocol one-shot.");

        using LoopBody    = typename LoopCtx::body;
        using LoopEntryPS = typename LoopCtx::entry_perm_set;

        // Decision D3 / Risk R1 — Loop body permission balance
        // enforcement.  Each iteration must leave the PS exactly as it
        // entered.  An iteration that drains a permission without
        // surrender or that gains a permission without surrender at end
        // would violate the loop invariant; the type system catches it
        // at the syntactic Continue site.
        static_assert(perm_set_equal_v<PS, LoopEntryPS>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle: Loop body's terminal PermSet "
            "differs from the Loop entry PermSet — the iteration's "
            "permission flow does not balance.  Each iteration of a "
            "Loop must leave the PermSet exactly as it entered "
            "(otherwise iteration N+1 would start in a state different "
            "from iteration N, violating the invariant).  Either "
            "surrender the leftover Transferable permissions before "
            "Continue (via send<Returned<...>>), or restructure the "
            "loop body to receive matching Returned permissions on "
            "each iteration so the net PS evolution is zero.");

        return PermissionedSessionHandle<LoopBody, LoopEntryPS,
                                         Resource, LoopCtx>{
            std::forward<Resource>(r), loc};
    } else if constexpr (is_loop_v<R>) {
        using InnerBody = typename R::body;
        using InnerCtx  = LoopContext<InnerBody, PS>;
        // Enter inner Loop: shadow LoopCtx with a fresh context whose
        // entry_perm_set captures the PS at Loop entry.  This is what
        // gives nested Loops their own balance check.
        return PermissionedSessionHandle<InnerBody, PS,
                                         Resource, InnerCtx>{
            std::forward<Resource>(r), loc};
    } else {
        // Plain head (End / Stop / Send / Recv / Select / Offer).  Wrap
        // and continue.  No PS evolution at the wrap step itself —
        // evolution happens on the consumer call (send/recv/etc.).
        return PermissionedSessionHandle<R, PS, Resource, LoopCtx>{
            std::forward<Resource>(r), loc};
    }
}

// ── Debug-mode abandonment enrichment (Decision D5 / Risk R3) ───
//
// In debug builds, when a non-terminal handle is destroyed without
// being consumed, this prints the LEAKED permission tags BEFORE the
// SessionHandleBase destructor prints its standard diagnostic and
// aborts.  Zero release cost — the entire body is `#ifndef NDEBUG`.
//
// The PS template parameter is the type-level set of permissions the
// handle was holding at abandonment.  Empty sets emit nothing (the
// base diagnostic is sufficient); non-empty sets emit a one-line
// header naming each leaked tag's display string.

template <typename PS>
inline void emit_leaked_permissions_debug() noexcept {
#ifndef NDEBUG
    if constexpr (PS::size > 0) {
        constexpr auto name = perm_set_name<PS>();
        std::fprintf(stderr,
            "─────────────────────────────────────────────────────────────────────\n"
            "[PermissionedSessionHandle] LEAKED PERMISSIONS (PS::size = %zu):\n"
            "  %.*s\n"
            "Each tag in the PermSet was acquired via Recv<Transferable<...>>\n"
            "or Recv<Returned<...>> but never surrendered before the handle\n"
            "was abandoned.  Surrender via Send<Returned<...>> back to the\n"
            "origin or close the protocol with EmptyPermSet at End/Stop.\n"
            "─────────────────────────────────────────────────────────────────────\n",
            PS::size,
            static_cast<int>(name.size()), name.data());
    }
#endif
}

}  // namespace detail

// ═════════════════════════════════════════════════════════════════
// ── PermissionedSessionHandle<End, PS, Resource, LoopCtx> ────────
// ═════════════════════════════════════════════════════════════════

template <typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<End, PS, Resource, LoopCtx>
    : public SessionHandleBase<End,
                               PermissionedSessionHandle<End, PS,
                                                         Resource, LoopCtx>>
{
    Resource                           resource_;
    [[no_unique_address]] PS           perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename R, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

    template <typename Proto, typename Res, typename... InitPerms>
    friend constexpr auto mint_permissioned_session(
        Res, Permission<InitPerms>&&...) noexcept;

public:
    using protocol      = End;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    constexpr explicit PermissionedSessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<End,
                            PermissionedSessionHandle<End, PS, Resource, LoopCtx>>{loc}
        , resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept            = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    // Debug-only destructor enrichment.  In release this is a no-op
    // (the if-constexpr collapses) and EBO continues to make the
    // handle the same size as the bare SessionHandle.  When the base
    // destructor runs after this Derived destructor, it sees
    // is_terminal_state_v<End> == true and skips its own abandonment
    // check, so this destructor's emit-leaked is the only place that
    // can fire for an End handle that was constructed but never
    // explicitly close()'d AND held a non-empty PS.
    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_()) {
            // For End specifically: the base's check is_terminal_state_v
            // exempts End so it doesn't fire abort.  But a non-empty PS
            // at End still represents a leak — the user reached End
            // without surrendering Transferable-acquired permissions.
            // Print the leaked tags here even though base will not
            // abort.
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    // Terminal close.  Decision D6 enforcement: PS must be empty —
    // every permission the handle ever acquired must have been
    // surrendered before reaching End.  This is the structural
    // convergence point the cross-branch enforcement (D4) relies on.
    [[nodiscard]] constexpr Resource close() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        static_assert(perm_set_equal_v<PS, EmptyPermSet>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle: reached End with a non-empty "
            "PermSet — every permission acquired through the protocol "
            "must be surrendered before close().  Either Send<Returned"
            "<...>> the remaining permissions back to their origin, or "
            "(if the protocol is genuinely one-shot consumption of the "
            "permission) extend the protocol to surrender via send "
            "before End.  Reaching End with leftover authority is "
            "structurally a permission leak.");
        this->mark_consumed_();
        return std::forward<Resource>(resource_);
    }

    [[nodiscard]] constexpr Resource&       resource() &       noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const & noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── PermissionedSessionHandle<Stop, PS, Resource, LoopCtx> ───────
// ═════════════════════════════════════════════════════════════════
//
// Stop is the crash-stop terminal from SessionCrash.h (BSYZ22).
// is_terminal_state<Stop> is specialised true.  Same close()
// semantics as End: PS must be empty.

template <CrashClass C, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Stop_g<C>,
                               PermissionedSessionHandle<Stop_g<C>, PS,
                                                         Resource, LoopCtx>>
{
    Resource                           resource_;
    [[no_unique_address]] PS           perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename R, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

public:
    using protocol      = Stop_g<C>;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    static constexpr CrashClass crash_class = C;

    constexpr explicit PermissionedSessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Stop_g<C>,
                            PermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>>{loc}
        , resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept            = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_()) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    [[nodiscard]] constexpr Resource close() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        static_assert(perm_set_equal_v<PS, EmptyPermSet>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle<Stop>: reached Stop with a "
            "non-empty PermSet.  Crash-stop discipline (BSYZ22) drops "
            "permissions on the floor at Stop; if that's the intended "
            "behaviour, surrender the permissions explicitly before "
            "Stop instead of relying on close to do it implicitly.");
        this->mark_consumed_();
        return std::forward<Resource>(resource_);
    }

    [[nodiscard]] constexpr Resource&       resource() &       noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const & noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── PermissionedSessionHandle<Send<T, R>, PS, Resource, LoopCtx>
// ═════════════════════════════════════════════════════════════════

template <typename T, typename R, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Send<T, R>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Send<T, R>,
                               PermissionedSessionHandle<Send<T, R>, PS,
                                                         Resource, LoopCtx>>
{
    Resource                           resource_;
    [[no_unique_address]] PS           perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

    template <typename Proto, typename Res, typename... InitPerms>
    friend constexpr auto mint_permissioned_session(
        Res, Permission<InitPerms>&&...) noexcept;

public:
    using protocol      = Send<T, R>;
    using payload       = T;
    using continuation  = R;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    constexpr explicit PermissionedSessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Send<T, R>,
                            PermissionedSessionHandle<Send<T, R>, PS, Resource, LoopCtx>>{loc}
        , resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept            = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Send<T, R>>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    // Send via Transport.  Two compile-time gates:
    //   * SendablePayload<T, PS> (body static_assert): sender holds
    //     the permission demanded by T's marker (or T is plain /
    //     Borrowed).  Encoded as a body static_assert with the
    //     framework-controlled [PermissionImbalance] prefix so the
    //     neg-compile harness can pattern-match it (a requires-clause
    //     would emit GCC-text "constraints not satisfied" instead).
    //     Decision D6 foundation — Phase 6 composes into the broader
    //     is_permission_balanced_v witness.
    //   * Transport invocability (requires-clause): matches bare
    //     SessionHandle's send contract.  Stays in the requires
    //     because Transport-shape mismatch is a structural signature
    //     mismatch, not a permission-flow issue.
    template <typename U = T, typename Transport>
        requires is_subsort_v<std::remove_cvref_t<U>, T> &&
                 std::is_invocable_v<Transport, Resource&, U&&>
    [[nodiscard]] constexpr auto send(U value, Transport transport) &&
        noexcept(std::is_nothrow_invocable_v<Transport, Resource&, U&&>
                 && std::is_nothrow_move_constructible_v<Resource>
                 && std::is_nothrow_move_constructible_v<U>)
    {
        static_assert(SendablePayload<T, PS>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::send: payload type T requires "
            "a permission tag the handle's PermSet does not contain.  "
            "Cases:\n"
            "  * Send<Transferable<T, X>, K>: sender must hold X "
            "(X must be in PS).\n"
            "  * Send<Returned<T, X>, K>: sender must hold X "
            "(X was previously borrowed and is being returned).\n"
            "  * Send<Borrowed<T, X>, K> and Send<Plain T, K>: "
            "always sendable (no permission demand).\n"
            "Verify the handle was minted (or evolved) with the "
            "permission you're trying to transfer.  If the protocol "
            "intends to lend (not transfer), wrap the payload in "
            "Borrowed<T, X> instead of Transferable<T, X>.");
        std::invoke(transport, resource_, std::move(value));
        this->mark_consumed_();
        using NextPS = compute_perm_set_after_send_t<PS, T>;
        return detail::step_to_next_permissioned<R, NextPS, Resource, LoopCtx>(
            std::forward<Resource>(resource_));
    }

    [[nodiscard]] constexpr Resource&       resource() &       noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const & noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── PermissionedSessionHandle<Recv<T, R>, PS, Resource, LoopCtx>
// ═════════════════════════════════════════════════════════════════

template <typename T, typename R, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Recv<T, R>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Recv<T, R>,
                               PermissionedSessionHandle<Recv<T, R>, PS,
                                                         Resource, LoopCtx>>
{
    Resource                           resource_;
    [[no_unique_address]] PS           perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

    template <typename Proto, typename Res, typename... InitPerms>
    friend constexpr auto mint_permissioned_session(
        Res, Permission<InitPerms>&&...) noexcept;

public:
    using protocol      = Recv<T, R>;
    using payload       = T;
    using continuation  = R;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    constexpr explicit PermissionedSessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Recv<T, R>,
                            PermissionedSessionHandle<Recv<T, R>, PS, Resource, LoopCtx>>{loc}
        , resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept            = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Recv<T, R>>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    // Receive via Transport.  Returns pair{payload value, next handle}
    // mirroring bare SessionHandle::recv.  The payload value itself
    // carries any embedded Permission tokens (Transferable / Returned)
    // that the sender bundled — extract via structured binding in
    // user code.  PS evolves per compute_perm_set_after_recv_t.
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto recv(Transport transport) &&
        noexcept(std::is_nothrow_invocable_r_v<T, Transport, Resource&>
                 && std::is_nothrow_move_constructible_v<Resource>
                 && std::is_nothrow_move_constructible_v<T>)
    {
        T value = std::invoke(transport, resource_);
        this->mark_consumed_();
        using NextPS = compute_perm_set_after_recv_t<PS, T>;
        auto next = detail::step_to_next_permissioned<R, NextPS,
                                                      Resource, LoopCtx>(
            std::forward<Resource>(resource_));
        return std::pair{std::move(value), std::move(next)};
    }

    [[nodiscard]] constexpr Resource&       resource() &       noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const & noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── PermissionedSessionHandle<Delegate<DelegatedSession<P, IPS>, K>>
// ═════════════════════════════════════════════════════════════════
//
// Permission-aware Honda throw/catch.  The carrier handle does not
// copy InnerPS into its own PS; the transferred inner endpoint owns
// those tokens.  Delegate consumes that inner PSH, and Accept mints
// the matching inner PSH on the recipient side.

template <typename InnerProto, typename InnerPS, typename K, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<
    Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
    PS, Resource, LoopCtx>
    : public SessionHandleBase<
          Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
          PermissionedSessionHandle<
              Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
              PS, Resource, LoopCtx>>
{
    using Protocol = Delegate<DelegatedSession<InnerProto, InnerPS>, K>;

    Resource                           resource_;
    [[no_unique_address]] PS           perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

    template <typename Proto, typename Res, typename... InitPerms>
    friend constexpr auto mint_permissioned_session(
        Res, Permission<InitPerms>&&...) noexcept;

public:
    using protocol        = Protocol;
    using delegated_proto = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set  = InnerPS;
    using continuation    = K;
    using perm_set        = PS;
    using resource_type   = Resource;
    using loop_ctx        = LoopCtx;

    constexpr explicit PermissionedSessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Protocol,
                            PermissionedSessionHandle<Protocol, PS,
                                                      Resource, LoopCtx>>{loc}
        , resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept            = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Protocol>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    template <typename ActualInnerPS, typename DelegatedResource,
              typename DelegatedLoopCtx, typename Transport>
        requires (!is_stop_v<InnerProto> &&
                  std::is_invocable_v<Transport, Resource&, DelegatedResource&&>)
    [[nodiscard]] constexpr auto delegate(
        PermissionedSessionHandle<InnerProto, ActualInnerPS,
                                  DelegatedResource, DelegatedLoopCtx>&& delegated,
        Transport transport) &&
        noexcept(std::is_nothrow_invocable_v<Transport, Resource&, DelegatedResource&&>
                 && std::is_nothrow_move_constructible_v<Resource>)
    {
        static_assert(perm_set_equal_v<ActualInnerPS, InnerPS>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::delegate: PermSet does not "
            "contain inner_ps tokens declared by DelegatedSession<P, "
            "InnerPS>.  The delegated handle's ActualInnerPS must "
            "match InnerPS exactly; otherwise the handoff would either "
            "fabricate missing authority or drop extra authority.");
        static_assert(perm_set_disjoint_v<PS, InnerPS>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::delegate: PermSet conflict -- "
            "inner_ps already owned by the carrier handle.  The "
            "delegated endpoint must be the unique owner of InnerPS "
            "before handoff.");
        static_assert(!is_terminal_state_v<InnerProto> ||
                      perm_set_equal_v<InnerPS, EmptyPermSet>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::delegate: "
            "is_permission_balanced_v<InnerProto> against InnerPS "
            "rejects.  A terminal delegated protocol cannot carry a "
            "non-empty inner_ps; close or rebalance the inner endpoint "
            "before handoff.");

        std::invoke(transport, resource_, std::move(delegated.resource_));
        delegated.mark_consumed_();
        this->mark_consumed_();
        return detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(
            std::forward<Resource>(resource_));
    }

    template <typename ActualInnerPS, typename DelegatedResource,
              typename DelegatedLoopCtx, typename Transport>
        requires is_stop_v<InnerProto>
    void delegate(
        PermissionedSessionHandle<InnerProto, ActualInnerPS,
                                  DelegatedResource, DelegatedLoopCtx>&&,
        Transport) && = delete(
        "[DelegateStop_NoContinuation] PermissionedSessionHandle<"
        "Delegate<DelegatedSession<Stop, InnerPS>, K>> cannot "
        "delegate an already-crashed endpoint and continue as K.  "
        "Handle Stop/crash before this permissioned handoff point.");

    template <typename ActualInnerPS, typename DelegatedResource,
              typename DelegatedLoopCtx>
        requires (!is_stop_v<InnerProto>)
    [[nodiscard]] constexpr auto delegate_local(
        PermissionedSessionHandle<InnerProto, ActualInnerPS,
                                  DelegatedResource, DelegatedLoopCtx>&& delegated) &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>
                 && std::is_nothrow_destructible_v<DelegatedResource>)
    {
        static_assert(perm_set_equal_v<ActualInnerPS, InnerPS>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::delegate_local: PermSet does "
            "not contain inner_ps tokens declared by DelegatedSession"
            "<P, InnerPS>.");
        static_assert(perm_set_disjoint_v<PS, InnerPS>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::delegate_local: PermSet "
            "conflict -- inner_ps already owned by the carrier "
            "handle.");
        static_assert(!is_terminal_state_v<InnerProto> ||
                      perm_set_equal_v<InnerPS, EmptyPermSet>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::delegate_local: "
            "is_permission_balanced_v<InnerProto> against InnerPS "
            "rejects.");
        delegated.mark_consumed_();
        (void)std::move(delegated);
        this->mark_consumed_();
        return detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(
            std::forward<Resource>(resource_));
    }

    template <typename ActualInnerPS, typename DelegatedResource,
              typename DelegatedLoopCtx>
    void delegate(
        PermissionedSessionHandle<InnerProto, ActualInnerPS,
                                  DelegatedResource, DelegatedLoopCtx>&&) && = delete(
        "[Wire_Variant_Required] PermissionedSessionHandle<Delegate<"
        "DelegatedSession<P, InnerPS>, K>>::delegate(handle) without "
        "a transport is not allowed.  Choose delegate(handle, "
        "transport) for wire handoff or delegate_local(handle) for "
        "explicit in-memory tests.");

    [[nodiscard]] constexpr Resource&       resource() &       noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const & noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── PermissionedSessionHandle<Accept<DelegatedSession<P, IPS>, K>>
// ═════════════════════════════════════════════════════════════════

template <typename InnerProto, typename InnerPS, typename K, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<
    Accept<DelegatedSession<InnerProto, InnerPS>, K>,
    PS, Resource, LoopCtx>
    : public SessionHandleBase<
          Accept<DelegatedSession<InnerProto, InnerPS>, K>,
          PermissionedSessionHandle<
              Accept<DelegatedSession<InnerProto, InnerPS>, K>,
              PS, Resource, LoopCtx>>
{
    using Protocol = Accept<DelegatedSession<InnerProto, InnerPS>, K>;

    Resource                           resource_;
    [[no_unique_address]] PS           perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

    template <typename Proto, typename Res, typename... InitPerms>
    friend constexpr auto mint_permissioned_session(
        Res, Permission<InitPerms>&&...) noexcept;

public:
    using protocol        = Protocol;
    using delegated_proto = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set  = InnerPS;
    using continuation    = K;
    using perm_set        = PS;
    using resource_type   = Resource;
    using loop_ctx        = LoopCtx;

    constexpr explicit PermissionedSessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Protocol,
                            PermissionedSessionHandle<Protocol, PS,
                                                      Resource, LoopCtx>>{loc}
        , resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept            = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Protocol>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    template <typename Transport,
              typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto accept(Transport transport) &&
        noexcept(std::is_nothrow_invocable_v<Transport, Resource&>
                 && std::is_nothrow_move_constructible_v<Resource>
                 && std::is_nothrow_move_constructible_v<DelegatedResource>)
    {
        static_assert(perm_set_disjoint_v<PS, InnerPS>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::accept: PermSet conflict -- "
            "inner_ps already owned by the carrier handle.  Accepting "
            "DelegatedSession<P, InnerPS> would duplicate a CSL "
            "permission token; split the authority into distinct tags "
            "or remove the overlapping carrier permission before "
            "accepting the endpoint.");
        static_assert(!is_terminal_state_v<InnerProto> ||
                      perm_set_equal_v<InnerPS, EmptyPermSet>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::accept: "
            "is_permission_balanced_v<InnerProto> against InnerPS "
            "rejects.  A terminal delegated protocol cannot carry a "
            "non-empty inner_ps.");

        DelegatedResource delegated_res = std::invoke(transport, resource_);
        this->mark_consumed_();
        PermissionedSessionHandle<InnerProto, InnerPS,
                                  DelegatedResource, void> delegated_handle{
            std::move(delegated_res)};
        auto continuation_handle =
            detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(
                std::forward<Resource>(resource_));
        return std::pair{std::move(delegated_handle),
                         std::move(continuation_handle)};
    }

    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto accept_with(DelegatedResource delegated_res) &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>
                 && std::is_nothrow_move_constructible_v<DelegatedResource>)
    {
        static_assert(perm_set_disjoint_v<PS, InnerPS>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::accept_with: PermSet conflict "
            "-- inner_ps already owned by the carrier handle.");
        static_assert(!is_terminal_state_v<InnerProto> ||
                      perm_set_equal_v<InnerPS, EmptyPermSet>,
            "crucible::session::diagnostic [PermissionImbalance]: "
            "PermissionedSessionHandle::accept_with: "
            "is_permission_balanced_v<InnerProto> against InnerPS "
            "rejects.");

        this->mark_consumed_();
        PermissionedSessionHandle<InnerProto, InnerPS,
                                  DelegatedResource, void> delegated_handle{
            std::move(delegated_res)};
        auto continuation_handle =
            detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(
                std::forward<Resource>(resource_));
        return std::pair{std::move(delegated_handle),
                         std::move(continuation_handle)};
    }

    [[nodiscard]] constexpr Resource&       resource() &       noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const & noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── PermissionedSessionHandle<Select<Bs…>, PS, Resource, LoopCtx>
// ═════════════════════════════════════════════════════════════════
//
// Decision D4 — branch convergence is enforced STRUCTURALLY.  Each
// branch's terminal head (End / Stop / Continue) carries its own PS
// static_assert (close() requires EmptyPermSet; Continue requires PS
// == LoopEntryPS).  Branches that all reach End must converge on
// EmptyPermSet by construction; branches that all loop-back must
// converge on LoopEntryPS by construction.  No separate convergence
// metafunction is needed — the type system enforces convergence at
// the convergence point itself.
//
// This is a tighter design than a metafunction-driven check because
// it's compositional: any future Select<Bs..., NewBranch> only adds
// to the convergence requirement at the existing terminal points.

template <typename... Branches, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Select<Branches...>, PS,
                                              Resource, LoopCtx>
    : public SessionHandleBase<Select<Branches...>,
                               PermissionedSessionHandle<Select<Branches...>, PS,
                                                         Resource, LoopCtx>>
{
    Resource                           resource_;
    [[no_unique_address]] PS           perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

    template <typename Proto, typename Res, typename... InitPerms>
    friend constexpr auto mint_permissioned_session(
        Res, Permission<InitPerms>&&...) noexcept;

public:
    using protocol      = Select<Branches...>;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    static_assert(branch_count > 0,
        "crucible::session::diagnostic [Empty_Choice_Combinator]: "
        "PermissionedSessionHandle<Select<>>: cannot construct a "
        "runnable handle on Select<> with zero branches.");

    constexpr explicit PermissionedSessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Select<Branches...>,
                            PermissionedSessionHandle<Select<Branches...>, PS,
                                                      Resource, LoopCtx>>{loc}
        , resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept            = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_()
            && !is_terminal_state_v<Select<Branches...>>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    // Pick branch I and signal the choice over the wire.  Mirrors the
    // bare framework's select<I>(transport).  PS does not evolve at
    // the pick itself — branch I's first head is what triggers the
    // next PS evolution.
    template <std::size_t I, typename Transport>
        requires std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) &&
        noexcept(std::is_nothrow_invocable_v<Transport, Resource&, std::size_t>
                 && std::is_nothrow_move_constructible_v<Resource>)
    {
        static_assert(I < sizeof...(Branches),
            "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
            "PermissionedSessionHandle<Select<...>>::select<I>(transport): "
            "branch index I is out of range.");
        std::invoke(transport, resource_, I);
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next_permissioned<Chosen, PS,
                                                 Resource, LoopCtx>(
            std::forward<Resource>(resource_));
    }

    // Wire-omitting variant — same naming discipline as the bare
    // framework (#377 force-explicit-discipline).  Use only for
    // in-memory channels and unit tests.
    template <std::size_t I>
    [[nodiscard]] constexpr auto select_local() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        static_assert(I < sizeof...(Branches),
            "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
            "PermissionedSessionHandle<Select<...>>::select_local<I>(): "
            "branch index I is out of range.");
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next_permissioned<Chosen, PS,
                                                 Resource, LoopCtx>(
            std::forward<Resource>(resource_));
    }

    // Match the bare framework's deletion of bare select<I>() to keep
    // user code from accidentally bypassing the wire-vs-local choice.
    template <std::size_t I>
    void select() && = delete(
        "[Wire_Variant_Required] PermissionedSessionHandle<Select<...>>"
        "::select<I>() without arguments is not allowed (mirror of "
        "Session.h:#377).  Choose select<I>(transport) for wire-based "
        "sessions or select_local<I>() for in-memory channels.");

    [[nodiscard]] constexpr Resource&       resource() &       noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const & noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── PermissionedSessionHandle<Offer<Bs…>, PS, Resource, LoopCtx>
// ═════════════════════════════════════════════════════════════════
//
// Same convergence story as Select.  branch(transport, handler)
// calls handler with the chosen branch's PSH; per-branch PS evolves
// according to that branch's first head.

template <typename... Branches, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Offer<Branches...>, PS,
                                              Resource, LoopCtx>
    : public SessionHandleBase<Offer<Branches...>,
                               PermissionedSessionHandle<Offer<Branches...>, PS,
                                                         Resource, LoopCtx>>
{
    Resource                           resource_;
    [[no_unique_address]] PS           perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

    template <typename Proto, typename Res, typename... InitPerms>
    friend constexpr auto mint_permissioned_session(
        Res, Permission<InitPerms>&&...) noexcept;

public:
    using protocol      = Offer<Branches...>;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    static_assert(branch_count > 0,
        "crucible::session::diagnostic [Empty_Choice_Combinator]: "
        "PermissionedSessionHandle<Offer<>>: cannot construct a "
        "runnable handle on Offer<> with zero branches.");

    constexpr explicit PermissionedSessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Offer<Branches...>,
                            PermissionedSessionHandle<Offer<Branches...>, PS,
                                                      Resource, LoopCtx>>{loc}
        , resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept            = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_()
            && !is_terminal_state_v<Offer<Branches...>>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    template <typename Transport, typename Handler>
        requires std::is_invocable_r_v<std::size_t, Transport, Resource&>
    constexpr auto branch(Transport transport, Handler handler) &&
    {
        const std::size_t idx = std::invoke(transport, resource_);
        this->mark_consumed_();
        return dispatch_branch_(idx, std::forward<Resource>(resource_), std::move(handler),
                                std::make_index_sequence<sizeof...(Branches)>{});
    }

    // Wire-omitting variant — mirrors bare Offer::pick_local.
    template <std::size_t I>
    [[nodiscard]] constexpr auto pick_local() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        static_assert(I < sizeof...(Branches),
            "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
            "PermissionedSessionHandle<Offer<...>>::pick_local<I>(): "
            "branch index I is out of range.");
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next_permissioned<Chosen, PS,
                                                 Resource, LoopCtx>(
            std::forward<Resource>(resource_));
    }

    template <std::size_t I>
    void pick() && = delete(
        "[Wire_Variant_Required] PermissionedSessionHandle<Offer<...>>"
        "::pick<I>() without arguments is not allowed (mirror of "
        "Session.h:#377).  Use pick_local<I>() for in-memory channels "
        "or branch(transport, handler) for wire-based sessions.");

    [[nodiscard]] constexpr Resource&       resource() &       noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const & noexcept { return resource_; }

private:
    template <std::size_t I>
    static constexpr auto make_branch_handle_(Resource r) {
        using B = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next_permissioned<B, PS, Resource, LoopCtx>(
            std::forward<Resource>(r));
    }

    template <std::size_t... Is, typename Handler>
    static constexpr auto dispatch_branch_(
        std::size_t                 idx,
        Resource                    res,
        Handler                     handler,
        std::index_sequence<Is...>)
    {
        if (idx >= sizeof...(Branches)) [[unlikely]] {
            std::abort();
        }

        using FirstHandle = decltype(make_branch_handle_<0>(std::declval<Resource>()));
        using Result      = std::invoke_result_t<Handler&&, FirstHandle>;

        if constexpr (std::is_void_v<Result>) {
            bool dispatched = false;
            ([&]() {
                if (!dispatched && idx == Is) {
                    std::invoke(std::move(handler),
                                make_branch_handle_<Is>(std::forward<Resource>(res)));
                    dispatched = true;
                }
            }(), ...);
        } else {
            std::optional<Result> result;
            bool dispatched = false;
            ([&]() {
                if (!dispatched && idx == Is) {
                    result.emplace(std::invoke(std::move(handler),
                                                make_branch_handle_<Is>(std::forward<Resource>(res))));
                    dispatched = true;
                }
            }(), ...);
            if (!result) [[unlikely]] std::abort();
            return std::move(*result);
        }
    }
};

// ═════════════════════════════════════════════════════════════════
// ── Factory: mint_permissioned_session<Proto, Resource, InitPerms…> ─
// ═════════════════════════════════════════════════════════════════
//
// Mints a PermissionedSessionHandle by consuming the supplied
// Permission tokens and recording their tags in the initial PS.
// The Permissions are CONSUMED (rvalue-ref binding) — the tags
// transfer from the caller's value-level holdings to the handle's
// type-level PS.  The resulting handle is the only path to those
// permissions until they're surrendered via Send<Returned> or
// dropped at End.
//
// Loop-prefixed protocols are unrolled the same way bare
// mint_session_handle does it.  Continue at the top level is
// rejected — same diagnostic shape as bare framework.

namespace detail {

template <typename Proto, typename InitialPS, typename Resource>
[[nodiscard]] constexpr auto mint_permissioned_session_with_loc(
    Resource r,
    std::source_location loc) noexcept
{
    static_assert(is_well_formed_v<Proto>,
        "crucible::session::diagnostic [Protocol_Ill_Formed]: "
        "mint_permissioned_session<Proto>: protocol is ill-formed.");
    static_assert(!is_empty_choice_v<Proto>,
        "crucible::session::diagnostic [Empty_Choice_Combinator]: "
        "mint_permissioned_session<Proto>: cannot construct a runnable "
        "handle on Select<> or Offer<> with zero branches.");
    static_assert(SessionResource<Resource>,
        "crucible::session::diagnostic [SessionResource_NotPinned]: "
        "mint_permissioned_session<Proto, Resource>: Resource fails the "
        "pin-discipline.  See SessionResource concept in Session.h.");

    if constexpr (is_loop_v<Proto>) {
        using Body = typename Proto::body;
        using Ctx  = LoopContext<Body, InitialPS>;
        return step_to_next_permissioned<Body, InitialPS, Resource, Ctx>(
            std::forward<Resource>(r), loc);
    } else {
        static_assert(!std::is_same_v<Proto, Continue>,
            "crucible::session::diagnostic [Continue_Without_Loop]: "
            "mint_permissioned_session<Continue>: Continue cannot be the "
            "top-level protocol.");
        return PermissionedSessionHandle<Proto, InitialPS, Resource,
                                          void>{std::forward<Resource>(r), loc};
    }
}

}  // namespace detail

template <typename Proto, typename Resource, typename... InitPerms>
[[nodiscard]] constexpr auto mint_permissioned_session(
    Resource r,
    Permission<InitPerms>&&... perms) noexcept
{
    // Consume the Permission tokens — their tags become the initial PS.
    // The rvalue-ref binding moved the caller's tokens into the
    // function's parameter pack; the fold below silences
    // -Wunused-parameter without taking another reference.  The tokens
    // destruct at function exit (Permission's dtor is trivial), so
    // ownership transfers from the caller to the type-level PS.
    using InitialPS = PermSet<InitPerms...>;
    ((void)perms, ...);

    return detail::mint_permissioned_session_with_loc<Proto, InitialPS, Resource>(
        std::forward<Resource>(r), std::source_location::current());
}

// ═════════════════════════════════════════════════════════════════
// ── with_crash_check_or_detach — Phase 5 crash transport composition
// ═════════════════════════════════════════════════════════════════
//
// Light-touch crash transport composition (per the wiring plan §10).
// Peeks the OneShotFlag once; if signalled, detaches the handle with
// `detach_reason::TransportClosedOutOfBand` and returns std::nullopt.
// If clear, invokes `body(std::move(h))` and returns its result.
//
// This is the v1 composition pattern — no PSH API change, no fused
// CrashWatchedPermissionedSessionHandle class (deferred to v2).  The
// caller threads the OneShotFlag through their own production loop:
//
//   while (more_work) {
//       auto next_opt = with_crash_check_or_detach(
//           std::move(h), peer_flag,
//           [&](auto h_in) { return std::move(h_in).send(...); });
//       if (!next_opt) {
//           // Crash detected: PS dropped (BSYZ22 crash-stop), Resource
//           // recoverable via separately-held channel reference.
//           break;
//       }
//       h = std::move(*next_opt);
//   }
//
// Discipline:
//
//   * Detach on crash drops the type-level PermSet entirely.  Per the
//     crash-stop discipline (BSYZ22), permissions are NOT recovered
//     from a crashed peer — the type system reflects this by dropping
//     PS at the abandonment point.  Any Transferable-acquired tokens
//     in PS are leaked (correct: their value-level Permission objects
//     destruct as PSH drops them).
//
//   * The Resource held by the handle is also lost from PSH's view
//     when detached.  Callers wanting to recover the underlying
//     channel must hold a separate reference (e.g. `Channel&` outside
//     the handle).
//
//   * Body must be invocable as `Body(PSHType&&) -> NextHandleType`.
//     std::optional<NextHandleType> is the return type; std::nullopt
//     iff crash was detected before body ran.
//
//   * peek() is relaxed-ordered (per OneShotFlag::peek doc).  For
//     production crash discipline, the producer's signal() is release-
//     ordered, so any state mutations the producer made before signal
//     are visible after the recipient's check_and_run pairs an
//     acquire-fence.  This helper uses peek (not check_and_run) for
//     the hot path — the discipline assumption is that a crash flag,
//     once set, is monotonic (never cleared during a session), so the
//     relaxed peek is sound.

template <typename PSH, typename Body>
    requires std::is_invocable_v<Body, PSH&&>
[[nodiscard]] constexpr auto with_crash_check_or_detach(
    PSH&& h,
    ::crucible::safety::OneShotFlag& flag,
    Body&& body)
    noexcept(std::is_nothrow_invocable_v<Body, PSH&&>)
    -> std::optional<std::invoke_result_t<Body, PSH&&>>
{
    if (flag.peek()) [[unlikely]] {
        // Acquire-fence pairs with the producer's release-store in
        // OneShotFlag::signal(), so any state the producer mutated
        // before signal is visible here (matches CrashWatchedHandle's
        // hot-path peek pattern from bridges/CrashTransport.h:354).
        std::atomic_thread_fence(std::memory_order_acquire);
        std::move(h).detach(detach_reason::TransportClosedOutOfBand{});
        return std::nullopt;
    }
    return std::optional{std::forward<Body>(body)(std::forward<PSH>(h))};
}

// ═════════════════════════════════════════════════════════════════
// ── session_fork — multi-party session establishment ─────────────
// ═════════════════════════════════════════════════════════════════
//
// Phase 4 of FOUND-C (Task #616).  Establishes a multi-party session
// from a global type G + per-role permissions, spawning one jthread
// per role with the role's projected protocol view.  Composes:
//
//   * sessions/SessionGlobal.h::Project<G, Role> for per-role local-
//     protocol projection.  v1 supports protocols that DON'T require
//     plain-merging at non-sender/non-receiver roles.  Diverging
//     multiparty (Raft, 2PC-with-multi-followers) requires full
//     coinductive merging (Task #381) — until that lands, those
//     globals fail at the Project<...> instantiation site, naming
//     the divergent branch.
//   * permissions/PermissionFork.h::mint_permission_fork for the structured
//     fork-join over std::jthread + RAII Whole rebuild on join.
//   * splits_into_pack<Whole, RolePerms...> manifest must be declared
//     by the user in the same TU as the role-tag definitions (per
//     the CSL discipline in permissions/Permission.h).
//
// API:
//
//   template <typename G, typename Whole, typename... RolePerms,
//             typename SharedChannel, typename... Bodies>
//   [[nodiscard]] Permission<Whole> session_fork(
//       SharedChannel& ch,
//       Permission<Whole>&& whole,
//       Bodies&&... bodies);
//
//   * G            — a global protocol type from SessionGlobal.h
//                    (Transmission / Choice / Rec_G / End_G / StopG)
//   * Whole        — the parent permission tag the user holds
//   * RolePerms... — one tag per participating role; order matches Bodies
//   * SharedChannel — Pinned channel shared by all roles' transports
//                    (passed by lvalue reference); each role's PSH binds
//                    its Resource = SharedChannel&
//   * Bodies       — one callable per role, signature
//                    `Body_i(PermissionedSessionHandle<project_t<G, Role_i>,
//                                                       PermSet<Role_i>,
//                                                       SharedChannel&>&&)
//                    -> void`
//                    (must be noexcept, per mint_permission_fork's invariant)
//
// Returns: the rebuilt Permission<Whole> after all role threads join.
//
// Discipline:
//
//   * Each role's body is responsible for advancing its projected
//     protocol to a terminal state (End / Stop) AND for either
//     surrendering its role permission via the protocol (Send<Returned
//     <..., RolePerm>>) before close, OR for calling .detach(reason)
//     to drop PS without the close-time perm-surrender check.
//   * For protocols where each role's projected local type doesn't
//     consume its role permission via the wire, the body uses
//     detach(detach_reason::TestInstrumentation{} or similar) to
//     terminate cleanly with non-empty PS.  This is the "role
//     permission as proof of participation" pattern — the type
//     system only checks that the role HOLDS the perm during its
//     session; surrender is at the body author's discretion.
//
// What this does NOT support (v1 limitations):
//
//   * Diverging multiparty (Raft, MoE all-to-all): blocked on Task
//     #381 (full coinductive merging in SessionGlobal.h::plain_merge_t).
//     Project<G, ThirdPartyRole> for diverging G fails with the
//     framework's existing plain-merge diagnostic.
//   * Cross-process roles (CNTP): the SharedChannel reference is a
//     within-process abstraction.  Cross-process forks need cross-
//     process Permission semantics — open question in 24_04 §C, v2.

namespace detail {

// Build the per-role lambda that mint_permissioned_session + invokes body.
// Pulled out so the parameter pack expansion at the call site stays
// readable.
template <typename G, typename Role, typename SharedChannel, typename Body>
[[nodiscard]] constexpr auto session_fork_role_lambda(
    SharedChannel& ch,
    Body&& body) noexcept
{
    return [&ch, body = std::forward<Body>(body)](
               Permission<Role>&& role_perm) mutable noexcept {
        using LocalProto = typename Project<G, Role>::type;
        // mint_permissioned_session consumes the per-role Permission and
        // produces the projected PSH whose Resource is the shared
        // channel by reference.  LocalProto may begin with Loop —
        // establish unrolls it.
        auto handle = mint_permissioned_session<LocalProto, SharedChannel&,
                                              Role>(ch, std::move(role_perm));
        std::move(body)(std::move(handle));
    };
}

}  // namespace detail

template <typename G, typename Whole, typename... RolePerms,
          typename SharedChannel, typename... Bodies>
[[nodiscard]] Permission<Whole> session_fork(
    SharedChannel& ch,
    Permission<Whole>&& whole_perm,
    Bodies&&... bodies) noexcept
{
    static_assert(is_global_well_formed_v<G>,
        "crucible::session::diagnostic [Protocol_Ill_Formed]: "
        "session_fork<G, ...>: global type G is ill-formed.");
    static_assert(sizeof...(RolePerms) == sizeof...(Bodies),
        "crucible::session::diagnostic [PermissionImbalance]: "
        "session_fork: number of RolePerms template arguments must "
        "match number of body callables.");
    static_assert(splits_into_pack_v<Whole, RolePerms...>,
        "crucible::session::diagnostic [PermissionImbalance]: "
        "session_fork<G, Whole, RolePerms...>: requires "
        "splits_into_pack<Whole, RolePerms...>::value true.  Declare "
        "the manifest in the same TU as the Whole and Role tags so "
        "reviewers see the entire region tree at one glance.");
    static_assert(SessionResource<SharedChannel&>,
        "crucible::session::diagnostic [SessionResource_NotPinned]: "
        "session_fork: the SharedChannel must be Pinned (its address "
        "must be stable across the spawned threads' lifetimes).  "
        "Derive your channel from safety::Pinned<ChannelType>.");

    // Compose: each role's lambda calls mint_permissioned_session with
    // the role's projected protocol + the role permission token; then
    // invokes the user's body with the constructed PSH.  mint_permission_fork
    // does the heavy lifting: split Whole into per-role tokens, spawn
    // one jthread per role, join via RAII array destructor, rebuild
    // Whole on return.
    return mint_permission_fork<RolePerms...>(
        std::move(whole_perm),
        detail::session_fork_role_lambda<G, RolePerms, SharedChannel,
                                          Bodies>(
            ch, std::forward<Bodies>(bodies))...
    );
}

}  // namespace crucible::safety::proto

// ═══════════════════════════════════════════════════════════════════
// Embedded smoke test — exercises construction and the type-level
// dispatch.  Runtime smoke verifies that a single-step Send +
// matching Recv composes (no transport actually invoked; just shapes).
// ═══════════════════════════════════════════════════════════════════

namespace crucible::safety::proto::detail::permissioned_session_smoke {

// Synthetic tags for compile-time exercises.
struct WorkPerm {};
struct HotPerm  {};

// Simple value Resource (FakeChannel).  Pinned not required for
// value-type Resources.
struct FakeChannel {
    int last_sent = 0;
};

// ── Sizeof equality with bare SessionHandle ─────────────────────────
//
// PermissionedSessionHandle<P, PS, R> must be the SAME size as the
// bare SessionHandle<P, R> for ALL build modes.  In release, both
// collapse to sizeof(R) (PS is empty + EBO; tracker is empty + EBO).
// In debug, the tracker contributes one byte + alignment but both
// wrappers pay the SAME tracker cost — sizeof equality holds in
// every mode.
//
// Asserting `==` (not `<=`) catches a future regression where PS or
// LoopContext accidentally gains a non-empty member, or where
// [[no_unique_address]] gets dropped.  The proof of zero overhead is
// load-bearing for the wiring plan §13 bench harness's machine-code-
// parity claim.

static_assert(std::is_empty_v<EmptyPermSet>);
static_assert(std::is_empty_v<PermSet<WorkPerm>>);
static_assert(std::is_empty_v<PermSet<WorkPerm, HotPerm>>);

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, FakeChannel>)
              == sizeof(SessionHandle<End, FakeChannel>));

static_assert(sizeof(PermissionedSessionHandle<End, PermSet<WorkPerm, HotPerm>,
                                                FakeChannel>)
              == sizeof(SessionHandle<End, FakeChannel>));

static_assert(sizeof(PermissionedSessionHandle<Send<int, End>, EmptyPermSet,
                                                FakeChannel>)
              == sizeof(SessionHandle<Send<int, End>, FakeChannel>));

// ── Type-level shape verification for Send permission flow ─────────
//
// Build a PSH for Send<Transferable<int, WorkPerm>, End> with PS
// containing WorkPerm.  Compute the type after a hypothetical send
// (without invoking transport): the next handle's PS should lose
// WorkPerm.

using WorkChannel = FakeChannel;
using SendProto   = Send<Transferable<int, WorkPerm>, End>;
using PSWith      = PermSet<WorkPerm>;
using PSWithout   = EmptyPermSet;
using DelegatedPayload = DelegatedSession<SendProto, PSWith>;
using DelegateProto    = Delegate<DelegatedPayload, End>;
using AcceptProto      = Accept<DelegatedPayload, End>;

// The next-PS metafunction matches PSWithout for this Transferable.
static_assert(perm_set_equal_v<
    compute_perm_set_after_send_t<PSWith, Transferable<int, WorkPerm>>,
    PSWithout>);

// ── Type-level shape verification for Recv permission flow ─────────
//
// Recv of a Transferable<int, HotPerm> grows PS by HotPerm.

static_assert(perm_set_equal_v<
    compute_perm_set_after_recv_t<EmptyPermSet, Transferable<int, HotPerm>>,
    PermSet<HotPerm>>);

// Permission-aware Delegate/Accept shape: carrier PS stays separate
// from the inner endpoint's InnerPS; the accepted/delegated endpoint
// itself carries PSWith.
static_assert(is_well_formed_v<DelegateProto>);
static_assert(is_well_formed_v<AcceptProto>);
static_assert(std::is_same_v<dual_of_t<DelegateProto>, AcceptProto>);
static_assert(std::is_same_v<
    typename PermissionedSessionHandle<DelegateProto, EmptyPermSet,
                                       FakeChannel>::inner_perm_set,
    PSWith>);
static_assert(sizeof(PermissionedSessionHandle<DelegateProto, EmptyPermSet,
                                               FakeChannel>)
              == sizeof(SessionHandle<DelegateProto, FakeChannel>));
static_assert(sizeof(PermissionedSessionHandle<AcceptProto, EmptyPermSet,
                                               FakeChannel>)
              == sizeof(SessionHandle<AcceptProto, FakeChannel>));

// ── runtime_smoke_test (per the discipline) ────────────────────────
//
// Construct a PSH on End with EmptyPermSet, close it, and observe the
// returned Resource.  Construct a PSH on Send and verify the type
// machinery resolves correctly.  No transport is invoked.

inline void runtime_smoke_test() noexcept {
    // End-handle close round-trip.
    {
        FakeChannel ch{42};
        PermissionedSessionHandle<End, EmptyPermSet, FakeChannel> h{ch};
        FakeChannel out = std::move(h).close();
        // Resource was moved through; identity preserved.
        if (out.last_sent != 42) std::abort();
    }

    // mint_permissioned_session with a single Permission, then close at
    // End — but PS would be PermSet<WorkPerm>, which fails the
    // close() static_assert (PS must be empty).  So instead, mint a
    // permission, drop it via permission_drop, then establish with no
    // perms and close.
    {
        auto perm = ::crucible::safety::mint_permission_root<WorkPerm>();
        ::crucible::safety::permission_drop(std::move(perm));

        auto handle = mint_permissioned_session<End>(FakeChannel{7});
        FakeChannel out = std::move(handle).close();
        if (out.last_sent != 7) std::abort();
    }

    // Send/recv shape check via static_asserts.
    {
        using PSHSend  = PermissionedSessionHandle<SendProto, PSWith, FakeChannel>;
        using PSHEnd   = PermissionedSessionHandle<End, EmptyPermSet, FakeChannel>;
        static_assert(std::is_same_v<typename PSHSend::perm_set,  PSWith>);
        static_assert(std::is_same_v<typename PSHEnd::perm_set,   EmptyPermSet>);
        static_assert(std::is_same_v<typename PSHSend::protocol,  SendProto>);
        static_assert(std::is_same_v<typename PSHEnd::protocol,   End>);
    }

    // LoopContext basics — body/entry_perm_set typedefs.
    {
        using Ctx = LoopContext<Send<int, Continue>, EmptyPermSet>;
        static_assert(std::is_same_v<typename Ctx::body, Send<int, Continue>>);
        static_assert(std::is_same_v<typename Ctx::entry_perm_set, EmptyPermSet>);
    }

    // Establish Loop unrolls one iteration: top-level Loop<Body> with
    // initial PS becomes a PSH on Body with PS = initial and a
    // LoopContext<Body, initial> in LoopCtx.  Verify the type
    // machinery resolves correctly.
    {
        using LoopProto = Loop<Send<int, Continue>>;  // plain int — no PS
        using LoopHandle =
            decltype(mint_permissioned_session<LoopProto>(FakeChannel{}));
        static_assert(std::is_same_v<typename LoopHandle::protocol,
                                     Send<int, Continue>>);
        static_assert(std::is_same_v<typename LoopHandle::perm_set,
                                     EmptyPermSet>);
        static_assert(std::is_same_v<typename LoopHandle::loop_ctx,
                                     LoopContext<Send<int, Continue>,
                                                 EmptyPermSet>>);
    }

    // step_to_next_permissioned: plain head wraps directly.
    {
        using NextEnd =
            decltype(detail::step_to_next_permissioned<End, EmptyPermSet,
                                                       FakeChannel, void>(
                FakeChannel{}));
        static_assert(std::is_same_v<NextEnd,
            PermissionedSessionHandle<End, EmptyPermSet, FakeChannel, void>>);
    }

    // step_to_next_permissioned: Loop<B> head shadows LoopCtx with the
    // new context whose entry_perm_set captures the current PS.
    {
        using NextLoop =
            decltype(detail::step_to_next_permissioned<
                Loop<Send<int, Continue>>, PermSet<WorkPerm>,
                FakeChannel, void>(FakeChannel{}));
        static_assert(std::is_same_v<typename NextLoop::protocol,
                                     Send<int, Continue>>);
        static_assert(std::is_same_v<typename NextLoop::perm_set,
                                     PermSet<WorkPerm>>);
        static_assert(std::is_same_v<typename NextLoop::loop_ctx,
                                     LoopContext<Send<int, Continue>,
                                                 PermSet<WorkPerm>>>);
    }

    // detail::emit_leaked_permissions_debug compiles and is callable
    // without a live PSH (the call is a no-op for empty PS in release;
    // in debug it would fprintf for non-empty PS but we pass empty so
    // nothing is printed even in debug).
    detail::emit_leaked_permissions_debug<EmptyPermSet>();
}

}  // namespace crucible::safety::proto::detail::permissioned_session_smoke
