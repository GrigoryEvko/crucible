#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — OneShotFlag × runtime crash transport
// for sessions  (#400 SAFEINT-A11, §11)
//
// SessionCrash.h ships the TYPE-LEVEL crash semantics (Stop combinator,
// Crash<Peer> payload, ReliableSet, has_crash_branch_for_peer_v) and
// notes that the runtime mechanism transitioning a live SessionHandle
// to Stop on peer death is "out of scope for L8."  This header is that
// runtime mechanism.
//
// ─── The wire ─────────────────────────────────────────────────────
//
// Each session that crosses a process / node boundary owns one
// `OneShotFlag` per remote peer.  CNTP completion-error handlers,
// SWIM confirmed-dead handlers, kernel-driver socket-close detectors
// — all of these PRODUCERS call `flag.signal()` exactly once when the
// peer dies.  The session-handle CONSUMERS (the per-op Send/Recv/
// Select/Offer/close) check the flag with a relaxed atomic peek
// before each operation; on the unlikely true path they take the
// acquire fence, detach the permissioned inner handle, mint inherited
// survivor permissions, and return a `CrashEvent<PeerTag, Resource,
// SurvivorTags...>` via `std::expected`.
//
// `CrashWatchedHandle<Proto, Resource, PeerTag, CrashClass C, LoopCtx, PS>`
// wraps a `PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>` and a
// `OneShotFlag&`.  `C` is the declared Stop_g crash grade observed by
// this runtime watcher; the default is CrashClass::Abort for the
// historical, strongest recovery path.  The public mint also accepts a bare
// `SessionHandle<Proto, Resource, LoopCtx>` and adapts it to
// `PS = EmptyPermSet` for backward-compatible non-permissioned callers.
// Each consumer method:
//
//   1. Peeks the flag (one relaxed load + branch — the spec's
//      ~1 cycle / op overhead budget).
//   2. On no-crash: forwards to the inner handle's consumer, marks
//      itself consumed, re-wraps the next-state handle in another
//      `CrashWatchedHandle` (bound to the same flag and peer tag).
//   3. On crash: takes an acquire fence (paired with the producer's
//      release in OneShotFlag::signal), marks itself consumed,
//      returns std::unexpected(CrashEvent<PeerTag, Resource,
//      SurvivorTags...>{recovered Resource, inherited permissions}).
//      The Resource flows out so callers can re-establish a new
//      channel with whatever endpoint state survived; the inherited
//      Permission tokens flow out per `survivors_t<PeerTag>`.
//
// ─── Design decisions ─────────────────────────────────────────────
//
// 1.  **`std::expected` over exceptions**: Crucible bans exceptions
//     on the hot path (`-fno-exceptions`).  `std::expected` is a
//     union + tag, ~1ns to test `.has_value()`, perfect fit.
//
// 2.  **Per-Proto specialization** (End / Send / Recv / Select /
//     Offer): mirrors `RecordingSessionHandle`'s scaffolding from
//     #404.  Each specialization mirrors the bare `SessionHandle`'s
//     consumer surface but with the `expected<...>` return wrap.
//
// 3.  **Stop is NOT auto-entered**: the wrapper does NOT mutate the
//     inner handle into a `SessionHandle<Stop, R>` on crash — it
//     destroys the inner handle and returns the recovered Resource.
//     Phase 2 (deferred) could auto-dispatch Crash<Peer> branches
//     when the wrapped handle is at an Offer with such a branch, by
//     inspecting `has_crash_branch_for_peer_v<Proto, PeerTag>`.
//
// 4.  **PeerTag is phantom**: the wrapper stores no per-peer runtime
//     state beyond the flag pointer; PeerTag exists purely so the
//     CrashEvent's compile-time signature names the responsible
//     peer.  Multi-peer sessions wrap the inner handle in nested
//     CrashWatchedHandles (one per peer); the type alias chain
//     records every watched peer at the type level.
//
// 5.  **Pinned by reference**: the `OneShotFlag` is held by raw
//     pointer (always non-null after construction).  Lifetime is
//     the caller's responsibility — typically the SessionEstab-
//     lishmentService allocates the flag, hands a reference to the
//     CrashWatchedHandle, and outlives the session.  The wrapper
//     itself is move-only (single-consumer linearity preserved).
//
// 6.  **Permission inheritance is explicit**: on peer death, surviving
//     permissions inherit only through the `inherits_from<DeadTag,
//     SurvivorTag>` / `survivor_registry<DeadTag>` lattice from
//     permissions/PermissionInherit.h.  Specializing that registry at
//     the peer tag declaration site is load-bearing: a PeerTag with no
//     declared survivor list is rejected before CrashWatchedHandle can
//     be minted.
//
// ─── Row-typed surface (FOUND-G62) ────────────────────────────────
//
// Each CrashWatchedHandle instance and consumer call site has a static
// CrashClass per CrashLattice (Abort ⊑ Throw ⊑ ErrorReturn ⊑ NoThrow).
// The handle-level class is the Stop_g grade expected in protocol
// continuations.  NoThrow is rejected for watched crash transport because
// an unreliable peer cannot be declared "cannot crash" while still being
// guarded by a crash flag.  The call-site classifications are enforced at
// the type level via OneShotFlag's pinned-surface methods
// (handles/OneShotFlag.h, FOUND-G62):
//
//     site                              underlying       CrashClass
//     ────────────────────────────────  ─────────────    ──────────
//     producer-side: signal()           atomic store     Throw
//                  → signal_throw()     (FOUND-G62)
//
//     consumer-side: peek() steady      atomic load      NoThrow
//       (the false-on-flag path)        + branch
//                  → peek_nothrow()     (FOUND-G62)
//
//     recovery-handler: peek() true     acquire-fence    ErrorReturn
//       (the unlikely true path that    + recovery
//        returns CrashEvent via         + std::expected
//        std::expected; FOUND-C v1 PSH  return
//        composition)
//                  → try_acknowledge_error_return(F)  (FOUND-G62)
//
// New code wiring CrashWatchedHandle into a NEW protocol arm should
// use the pinned surface so the handle carries its CrashClass
// classification at the type level.  Existing call sites (Send /
// Recv / Select / Offer / close specializations below) continue to
// use the raw peek() — the additive overlay preserves the per-call
// shape without churn.  Audit migration is a follow-up task.
//
// ─── References ───────────────────────────────────────────────────
//
//   misc/24_04_2026_safety_integration.md §11 — design rationale
//   misc/28_04_2026_effects.md §4.3.10        — FOUND-G62 type-level
//                                               classification rationale
//   safety/OneShotFlag.h          — the underlying signal primitive
//                                   + FOUND-G62 pinned-surface methods
//   safety/Crash.h                — the CrashClass wrapper type
//   safety/SessionCrash.h          — type-level crash semantics
//   safety/RecordingSessionHandle.h — sibling wrapper using the same
//                                     per-Proto specialization scaffold
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/permissions/PermissionInherit.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// Forward-declared so the pass-key class can friend it (#430).  Real
// definition appears below CrashEvent.
template <typename PeerTag,
          typename InnerHandle,
          typename Resource,
          typename Reason>
[[nodiscard]] constexpr auto wrap_crash_return(
    InnerHandle&& inner, Reason reason_tag, Resource recovered) noexcept;

// ═════════════════════════════════════════════════════════════════════
// ── CrashEvent<PeerTag, Resource, SurvivorTags...> ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Carries the peer identity (compile-time) and the recovered
// Resource value (runtime — the inner handle's Resource is moved
// out before the wrapper returns the unexpected branch, so callers
// can salvage state for a re-establishment attempt).
//
// Resource is captured by-value to give the caller exclusive
// ownership; the wrapper is destroyed in the act of returning.
//
// ─── Construction is RESTRICTED (#430) ──────────────────────────────
//
// The constructor is PRIVATE.  The ONLY type permitted to construct a
// CrashEvent is the friend factory `wrap_crash_return`, which bundles
// the construction with the mandatory `inner.detach(reason)` step that
// every wrapper crash-path must perform.  This makes the bug from
// #400 — wrapper crash-paths that returned `std::unexpected(CrashEvent
// {...})` WITHOUT first detaching their inner handle, causing
// a silent abandonment abort via the inner's destructor — STRUCTURALLY
// IMPOSSIBLE.
//
// Callers READ via the public `resource` field (left public so existing
// `event.resource.field` access patterns in tests / recovery handlers
// keep working unchanged).
//
// Audit:
//   grep "wrap_crash_return"   — every detach + unexpected pair
//   grep "CrashEvent<.*>{"     — should match only wrap_crash_return's
//                                 body and (legacy) doc-comment text

// ─── Pass-key for CrashEvent's restricted ctor (#430) ──────────────
//
// The pass-key idiom: an empty type whose ctor is private, with the
// only friend being the helper that's authorized to construct CrashEvent.
// Anyone else attempting to spell `CrashEvent<P, R>{key, r}` cannot
// produce a `key` of type `WrapCrashReturnKey` to begin with.  This
// sidesteps the template-friend signature-matching subtleties that
// arise when friending a function template directly.
class WrapCrashReturnKey {
    constexpr WrapCrashReturnKey() noexcept = default;

    // Only the wrap_crash_return helper can mint a key.
    template <typename PeerTag2, typename InnerHandle2,
              typename Resource2, typename Reason2>
    friend constexpr auto wrap_crash_return(InnerHandle2&&, Reason2, Resource2) noexcept;
};

template <typename PeerTag, typename Resource, typename... SurvivorTags>
class CrashEvent {
public:
    using peer          = PeerTag;
    using resource_type = Resource;
    using survivors     = ::crucible::permissions::inheritance_list<SurvivorTags...>;
    using permissions_type =
        std::tuple<::crucible::safety::Permission<SurvivorTags>...>;

    // Public read-access — backward-compatible with handler code that
    // reads `event.resource.field` directly (#430 keeps this open).
    Resource resource;
    [[no_unique_address]] permissions_type permissions;

    // Pass-key-protected ctor.  Direct user construction
    // `CrashEvent<P, R>{r}` cannot compile — the user has no way to
    // mint a `WrapCrashReturnKey`.  The only authorized constructor
    // is `wrap_crash_return`, which is friended on the key class.
    constexpr CrashEvent(WrapCrashReturnKey, Resource r, permissions_type perms) noexcept
        : resource{std::move(r)}, permissions{std::move(perms)} {}
};

namespace detail {

template <typename PeerTag, typename Resource, typename Survivors>
struct crash_event_from_survivors;

template <typename PeerTag, typename Resource, typename... SurvivorTags>
struct crash_event_from_survivors<
    PeerTag,
    Resource,
    ::crucible::permissions::inheritance_list<SurvivorTags...>>
{
    using type = CrashEvent<PeerTag, Resource, SurvivorTags...>;
};

template <typename PeerTag, typename Resource>
using crash_event_for_t = typename crash_event_from_survivors<
    PeerTag,
    Resource,
    ::crucible::permissions::survivors_t<PeerTag>>::type;

template <typename Event>
struct crash_event_matches_survivors : std::false_type {};

template <typename PeerTag, typename Resource, typename... SurvivorTags>
struct crash_event_matches_survivors<
    CrashEvent<PeerTag, Resource, SurvivorTags...>>
    : std::is_same<
          CrashEvent<PeerTag, Resource, SurvivorTags...>,
          crash_event_for_t<PeerTag, Resource>> {};

template <typename Event>
inline constexpr bool crash_event_matches_survivors_v =
    crash_event_matches_survivors<Event>::value;

template <typename PeerTag>
consteval void require_crash_survivors_declared_() {
    static_assert(!::crucible::permissions::inheritance_list_empty_v<
        ::crucible::permissions::survivors_t<PeerTag>>,
        "CrashWatchedHandle requires permission_inherit survivors for "
        "PeerTag. Specialize survivor_registry<PeerTag> at the peer tag "
        "declaration site before enabling crash recovery.");
}

template <CrashClass C>
inline constexpr bool crash_watched_class_admissible_v =
    C != CrashClass::NoThrow;

template <CrashClass C>
consteval void require_crash_watched_class_admissible_() {
    static_assert(crash_watched_class_admissible_v<C>,
        "crucible::session::diagnostic "
        "[CrashWatched_NoThrow_Rejected]: "
        "CrashWatchedHandle cannot be parameterized with "
        "CrashClass::NoThrow for an unreliable watched peer. Remove the "
        "watcher for genuinely no-throw peers, or declare Abort/Throw/"
        "ErrorReturn recovery.");
}

template <typename Proto, CrashClass C>
struct stop_class_compatible : std::true_type {};

template <CrashClass StopC, CrashClass C>
struct stop_class_compatible<Stop_g<StopC>, C>
    : std::bool_constant<StopC == C> {};

template <typename T, typename R, CrashClass C>
struct stop_class_compatible<Send<T, R>, C>
    : stop_class_compatible<R, C> {};

template <typename T, typename R, CrashClass C>
struct stop_class_compatible<Recv<T, R>, C>
    : stop_class_compatible<R, C> {};

template <typename... Branches, CrashClass C>
struct stop_class_compatible<Select<Branches...>, C>
    : std::bool_constant<(stop_class_compatible<Branches, C>::value && ...)> {};

template <typename... Branches, CrashClass C>
struct stop_class_compatible<Offer<Branches...>, C>
    : std::bool_constant<(stop_class_compatible<Branches, C>::value && ...)> {};

template <typename Body, CrashClass C>
struct stop_class_compatible<Loop<Body>, C>
    : stop_class_compatible<Body, C> {};

template <typename Inner, typename K, CrashClass C>
struct stop_class_compatible<Delegate<Inner, K>, C>
    : std::bool_constant<stop_class_compatible<Inner, C>::value &&
                         stop_class_compatible<K, C>::value> {};

template <typename Inner, typename K, CrashClass C>
struct stop_class_compatible<Accept<Inner, K>, C>
    : std::bool_constant<stop_class_compatible<Inner, C>::value &&
                         stop_class_compatible<K, C>::value> {};

template <typename Inner, typename K,
          std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          CrashClass C>
struct stop_class_compatible<
    EpochedDelegate<Inner, K, MinEpoch, MinGeneration>, C>
    : std::bool_constant<stop_class_compatible<Inner, C>::value &&
                         stop_class_compatible<K, C>::value> {};

template <typename Inner, typename K,
          std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          CrashClass C>
struct stop_class_compatible<
    EpochedAccept<Inner, K, MinEpoch, MinGeneration>, C>
    : std::bool_constant<stop_class_compatible<Inner, C>::value &&
                         stop_class_compatible<K, C>::value> {};

template <typename Proto, CrashClass C>
inline constexpr bool stop_class_compatible_v =
    stop_class_compatible<Proto, C>::value;

template <typename Proto, CrashClass C>
consteval void require_stop_class_compatible_() {
    static_assert(stop_class_compatible_v<Proto, C>,
        "crucible::session::diagnostic "
        "[CrashWatched_StopClass_Mismatch]: "
        "CrashWatchedHandle's declared CrashClass does not match a "
        "Stop_g<C> reachable in the watched protocol. Align the handle "
        "CrashClass with the protocol Stop_g grade.");
}

template <typename Proto, CrashClass C>
consteval void require_crash_watched_contract_() {
    require_crash_watched_class_admissible_<C>();
    require_stop_class_compatible_<Proto, C>();
}

}  // namespace detail

template <typename Event>
concept CrashEventMatchesSurvivors =
    detail::crash_event_matches_survivors_v<Event>;

// ═════════════════════════════════════════════════════════════════════
// ── wrap_crash_return — wrapper-side crash-path bundler (#430) ─────
// ═════════════════════════════════════════════════════════════════════
//
// Every wrapper around PermissionedSessionHandle that returns
// `std::expected<NextHandle, detail::crash_event_for_t<PeerTag, Resource>>` on a
// crash path MUST perform two operations atomically:
//
//   1. Detach the inner PermissionedSessionHandle (`inner.detach(reason_tag)`) so
//      its destructor sees the consumed flag and skips the abandonment
//      abort.  Without this, the inner — at non-terminal protocol state
//      — fires SessionHandleBase's destructor abort when the wrapper's
//      crash-path stack frame unwinds.  This is exactly the bug shipped
//      in #400's first replace_all that caught only Send's path,
//      leaving recv / select / pick / branch silently aborting via the
//      inner's destructor; debug-instrumentation localised the hole and
//      the fix was a second pass that applied detach uniformly.
//
//   2. Construct the survivor-aware CrashEvent and wrap it in
//      `std::unexpected{...}` so the caller gets the recovered Resource
//      plus Permission tokens minted from `survivors_t<PeerTag>`.
//
// `wrap_crash_return` BUNDLES the two operations.  It is the ONLY
// construction site for `CrashEvent` (per the friend declaration above),
// so wrapper crash-paths that try to spell the unexpected return
// manually (`return std::unexpected(CrashEvent<P, R>{...})`) get a
// crisp compile error pointing at the private ctor — the bug from
// #400 becomes structurally impossible.
//
// Audit (review-discoverable, grep-mechanical):
//
//   grep "wrap_crash_return"   — every wrapper crash-path site
//   grep "CrashEvent<.*>{"     — must match ONLY wrap_crash_return's
//                                 body and doc comments; any other
//                                 hit is a wrapper trying to bypass
//                                 the discipline (review-reject)

template <typename PeerTag,
          typename InnerHandle,
          typename Resource,
          typename Reason>
[[nodiscard]] constexpr auto wrap_crash_return(
    InnerHandle&& inner,
    Reason reason_tag,
    Resource recovered) noexcept
{
    using Inner = std::remove_cvref_t<InnerHandle>;
    static_assert(std::is_same_v<std::remove_cvref_t<Resource>,
                                 typename Inner::resource_type>,
        "wrap_crash_return recovered resource type must match inner "
        "resource_type.");

    // The detach call is what saves the inner from firing its
    // destructor's abandonment abort.  detach()'s `requires
    // DetachReason<Reason>` constraint enforces that `reason_tag`
    // is a tag from `detach_reason::*`; mismatched / missing tags
    // produce the named diagnostic from #376.
    std::move(inner).detach(reason_tag);
    // Mint a WrapCrashReturnKey (only this function can — its private
    // ctor is friended on this template) and pass it to CrashEvent's
    // pass-key-protected ctor.  Direct user `CrashEvent<P, R>{r}`
    // construction outside this body cannot mint a key, so the
    // unexpected-without-detach pattern from #400 is now a compile
    // error rather than a runtime abort.
    detail::require_crash_survivors_declared_<PeerTag>();

    return std::unexpected{
        detail::crash_event_for_t<PeerTag, Resource>{
            WrapCrashReturnKey{},
            std::move(recovered),
            ::crucible::permissions::permission_inherit<PeerTag>()}};
}

// ═════════════════════════════════════════════════════════════════════
// ── Forward declaration + factory ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename Proto, typename Resource,
          typename PeerTag, CrashClass C = CrashClass::Abort,
          typename LoopCtx = void,
          typename PS = EmptyPermSet>
class CrashWatchedHandle;

namespace detail {

template <typename LoopCtx, typename PS>
struct permissioned_loop_ctx_from_bare {
    using type = LoopCtx;
};

template <typename PS>
struct permissioned_loop_ctx_from_bare<void, PS> {
    using type = void;
};

template <typename Body, typename PS>
struct permissioned_loop_ctx_from_bare<Loop<Body>, PS> {
    using type = LoopContext<Body, PS>;
};

template <typename Body, typename EntryPS, typename PS>
struct permissioned_loop_ctx_from_bare<LoopContext<Body, EntryPS>, PS> {
    using type = LoopContext<Body, EntryPS>;
};

template <std::uint64_t CurrentEpoch,
          std::uint64_t CurrentGeneration,
          typename InnerLoopCtx,
          typename PS>
struct permissioned_loop_ctx_from_bare<
    EpochCtx<CurrentEpoch, CurrentGeneration, InnerLoopCtx>,
    PS> {
    using type = EpochCtx<
        CurrentEpoch,
        CurrentGeneration,
        typename permissioned_loop_ctx_from_bare<InnerLoopCtx, PS>::type>;
};

template <VendorBackend V, typename InnerLoopCtx, typename PS>
struct permissioned_loop_ctx_from_bare<VendorCtx<V, InnerLoopCtx>, PS> {
    using type = VendorCtx<V,
        typename permissioned_loop_ctx_from_bare<InnerLoopCtx, PS>::type>;
};

template <typename LoopCtx, typename PS>
using permissioned_loop_ctx_from_bare_t =
    typename permissioned_loop_ctx_from_bare<LoopCtx, PS>::type;

// Build a CrashWatchedHandle around a freshly-stepped inner handle,
// preserving the framework's Continue / Loop resolution.  Mirrors
// safety/RecordingSessionHandle.h's wrap_next_.
template <typename PeerTag, CrashClass C, typename NextHandle>
[[nodiscard]] constexpr auto wrap_crash_next_(
    NextHandle inner,
    OneShotFlag& flag) noexcept
{
    using NextProto    = typename NextHandle::protocol;
    using NextResource = typename NextHandle::resource_type;
    using NextLoopCtx  = typename NextHandle::loop_ctx;
    using NextPS       = typename NextHandle::perm_set;
    return CrashWatchedHandle<
        NextProto, NextResource, PeerTag, C, NextLoopCtx, NextPS>{
        std::move(inner), flag};
}

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── CrashWatchedHandle<End, …> ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// End is terminal — close() succeeds even if the flag is set, since
// the protocol completed normally before the crash signal arrived.
// (Different from mid-protocol Send/Recv where a crash means we
// can't actually deliver the next message.)  Still peek for symmetry
// in the audit trail; do not gate on it.

template <typename Resource, typename PeerTag,
          CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]] CrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<End,
                               CrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS>>
{
    PermissionedSessionHandle<End, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
        "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
        "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(detail::stop_class_compatible_v<End, C>,
        "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
        "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol      = End;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using perm_set      = PS;
    using inner_type    = PermissionedSessionHandle<End, PS, Resource, LoopCtx>;
    using stop_type     = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<End,
                            CrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS>>{loc}
        , inner_{std::move(inner)}, flag_{&flag}
    {
        detail::require_crash_watched_contract_<End, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept            = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    [[nodiscard]] constexpr Resource close() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag&    crash_flag() const  noexcept { return *flag_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── CrashWatchedHandle<Stop_g<C>, …> ───────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Stop_g is terminal like End, but its crash class is load-bearing:
// a watched handle declared with CrashClass::Throw must terminate in
// Stop_g<Throw>, not a silently widened Stop_g<Abort>.

template <CrashClass StopC, typename Resource, typename PeerTag,
          CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]]
CrashWatchedHandle<Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<
          Stop_g<StopC>,
          CrashWatchedHandle<Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS>>
{
    PermissionedSessionHandle<Stop_g<StopC>, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
        "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
        "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(StopC == C,
        "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
        "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol      = Stop_g<StopC>;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using perm_set      = PS;
    using inner_type    = PermissionedSessionHandle<Stop_g<StopC>, PS, Resource, LoopCtx>;
    using stop_type     = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              Stop_g<StopC>,
              CrashWatchedHandle<
                  Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS>>{loc}
        , inner_{std::move(inner)}, flag_{&flag}
    {
        detail::require_crash_watched_contract_<Stop_g<StopC>, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept            = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    [[nodiscard]] constexpr Resource close() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag&    crash_flag() const  noexcept { return *flag_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── CrashWatchedHandle<Send<T, R>, …> ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T, typename R, typename Resource,
          typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]] CrashWatchedHandle<Send<T, R>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Send<T, R>,
                               CrashWatchedHandle<Send<T, R>, Resource, PeerTag, C, LoopCtx, PS>>
{
    PermissionedSessionHandle<Send<T, R>, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
        "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
        "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(detail::stop_class_compatible_v<Send<T, R>, C>,
        "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
        "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol      = Send<T, R>;
    using message_type  = T;
    using continuation  = R;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using perm_set      = PS;
    using inner_type    = PermissionedSessionHandle<Send<T, R>, PS, Resource, LoopCtx>;
    using stop_type     = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Send<T, R>,
                            CrashWatchedHandle<Send<T, R>, Resource, PeerTag, C, LoopCtx, PS>>{loc}
        , inner_{std::move(inner)}, flag_{&flag}
    {
        detail::require_crash_watched_contract_<Send<T, R>, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept            = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr auto send(T value, Transport transport) &&
        -> std::expected<
            decltype(detail::wrap_crash_next_<PeerTag, C>(
                std::declval<inner_type>().send(std::move(value), std::move(transport)),
                std::declval<OneShotFlag&>())),
            detail::crash_event_for_t<PeerTag, Resource>>
    {
        // Hot-path peek: one relaxed atomic load.  No fence on the
        // happy path.
        if (flag_->peek()) [[unlikely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            // Recover Resource via the diagnostic borrow then move it
            // out before detaching the inner.  detach() is the typed
            // escape hatch for non-terminal abandonment;
            // TransportClosedOutOfBand exactly names the audit class
            // (peer-crash detected at a lower layer).
            Resource recovered = std::move(inner_.resource());
            this->mark_consumed_();
            // wrap_crash_return (#430) bundles `inner.detach(reason)` +
            // `return std::unexpected(CrashEvent{...})`.  CrashEvent's
            // ctor is private; this helper is its only friend, so a
            // wrapper that returned unexpected without detaching would
            // not compile (the bug from #400's first replace_all).
            return wrap_crash_return<PeerTag>(
                std::move(inner_),
                detach_reason::TransportClosedOutOfBand{},
                std::move(recovered));
        }
        auto next = std::move(inner_).send(std::move(value), std::move(transport));
        this->mark_consumed_();
        return detail::wrap_crash_next_<PeerTag, C>(std::move(next), *flag_);
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag&    crash_flag() const  noexcept { return *flag_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── CrashWatchedHandle<Recv<T, R>, …> ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T, typename R, typename Resource,
          typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]] CrashWatchedHandle<Recv<T, R>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Recv<T, R>,
                               CrashWatchedHandle<Recv<T, R>, Resource, PeerTag, C, LoopCtx, PS>>
{
    PermissionedSessionHandle<Recv<T, R>, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
        "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
        "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(detail::stop_class_compatible_v<Recv<T, R>, C>,
        "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
        "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol      = Recv<T, R>;
    using message_type  = T;
    using continuation  = R;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using perm_set      = PS;
    using inner_type    = PermissionedSessionHandle<Recv<T, R>, PS, Resource, LoopCtx>;
    using stop_type     = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Recv<T, R>,
                            CrashWatchedHandle<Recv<T, R>, Resource, PeerTag, C, LoopCtx, PS>>{loc}
        , inner_{std::move(inner)}, flag_{&flag}
    {
        detail::require_crash_watched_contract_<Recv<T, R>, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept            = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    // recv: on crash, return CrashEvent with no message (the message
    // never arrived).  On no-crash, return std::expected<pair<T,
    // NextHandle>, CrashEvent>.
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto recv(Transport transport) &&
        -> std::expected<
            std::pair<T, decltype(detail::wrap_crash_next_<PeerTag, C>(
                std::declval<inner_type>().recv(std::move(transport)).second,
                std::declval<OneShotFlag&>()))>,
            detail::crash_event_for_t<PeerTag, Resource>>
    {
        if (flag_->peek()) [[unlikely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            // Recover Resource then explicitly detach the inner
            // handle so its destructor's abandonment-check passes.
            Resource recovered = std::move(inner_.resource());
            this->mark_consumed_();
            // wrap_crash_return (#430) bundles `inner.detach(reason)` +
            // `return std::unexpected(CrashEvent{...})`.  CrashEvent's
            // ctor is private; this helper is its only friend, so a
            // wrapper that returned unexpected without detaching would
            // not compile (the bug from #400's first replace_all).
            return wrap_crash_return<PeerTag>(
                std::move(inner_),
                detach_reason::TransportClosedOutOfBand{},
                std::move(recovered));
        }
        auto [value, next] = std::move(inner_).recv(std::move(transport));
        this->mark_consumed_();
        return std::pair{
            std::move(value),
            detail::wrap_crash_next_<PeerTag, C>(std::move(next), *flag_)};
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag&    crash_flag() const  noexcept { return *flag_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── CrashWatchedHandle<Select<Bs...>, …> ───────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename... Branches, typename Resource,
          typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]] CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Select<Branches...>,
                               CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS>>
{
    PermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
        "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
        "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(detail::stop_class_compatible_v<Select<Branches...>, C>,
        "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
        "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol      = Select<Branches...>;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using perm_set      = PS;
    using inner_type    = PermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx>;
    using stop_type     = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Select<Branches...>,
                            CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS>>{loc}
        , inner_{std::move(inner)}, flag_{&flag}
    {
        detail::require_crash_watched_contract_<Select<Branches...>, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept            = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    // Transport-driven select: signal choice; route to crash if flag set.
    template <std::size_t I, typename Transport>
        requires (I < sizeof...(Branches))
              && std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) &&
        -> std::expected<
            decltype(detail::wrap_crash_next_<PeerTag, C>(
                std::declval<inner_type>().template select<I>(std::move(transport)),
                std::declval<OneShotFlag&>())),
            detail::crash_event_for_t<PeerTag, Resource>>
    {
        if (flag_->peek()) [[unlikely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            // Recover Resource then explicitly detach the inner
            // handle so its destructor's abandonment-check passes.
            Resource recovered = std::move(inner_.resource());
            this->mark_consumed_();
            // wrap_crash_return (#430) bundles `inner.detach(reason)` +
            // `return std::unexpected(CrashEvent{...})`.  CrashEvent's
            // ctor is private; this helper is its only friend, so a
            // wrapper that returned unexpected without detaching would
            // not compile (the bug from #400's first replace_all).
            return wrap_crash_return<PeerTag>(
                std::move(inner_),
                detach_reason::TransportClosedOutOfBand{},
                std::move(recovered));
        }
        auto next = std::move(inner_).template select<I>(std::move(transport));
        this->mark_consumed_();
        return detail::wrap_crash_next_<PeerTag, C>(std::move(next), *flag_);
    }

    // No-transport select.
    // Renamed from `select<I>()` to `select_local<I>()` (#377) so the
    // wire ABSENCE is visible at the call site.  CrashWatchedHandle's
    // crash-detection still applies — peer crash before the local
    // .select_local<I>() call returns crash event; otherwise the
    // local handle advances to branch I without signalling the peer.
    template <std::size_t I>
        requires (I < sizeof...(Branches))
    [[nodiscard]] constexpr auto select_local() &&
        -> std::expected<
            decltype(detail::wrap_crash_next_<PeerTag, C>(
                std::declval<inner_type>().template select_local<I>(),
                std::declval<OneShotFlag&>())),
            detail::crash_event_for_t<PeerTag, Resource>>
    {
        if (flag_->peek()) [[unlikely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            // Recover Resource then explicitly detach the inner
            // handle so its destructor's abandonment-check passes.
            Resource recovered = std::move(inner_.resource());
            this->mark_consumed_();
            // wrap_crash_return (#430) bundles `inner.detach(reason)` +
            // `return std::unexpected(CrashEvent{...})`.  CrashEvent's
            // ctor is private; this helper is its only friend, so a
            // wrapper that returned unexpected without detaching would
            // not compile (the bug from #400's first replace_all).
            return wrap_crash_return<PeerTag>(
                std::move(inner_),
                detach_reason::TransportClosedOutOfBand{},
                std::move(recovered));
        }
        auto next = std::move(inner_).template select_local<I>();
        this->mark_consumed_();
        return detail::wrap_crash_next_<PeerTag, C>(std::move(next), *flag_);
    }

    // Deleted `select<I>()` overload (#377) — forces every call site
    // to choose between the wire variant `.select<I>(transport)` and
    // the wire-omitting `.select_local<I>()`.
    template <std::size_t I>
    void select() && = delete(
        "[Wire_Variant_Required] CrashWatchedHandle<Select<...>>::"
        "select<I>() without arguments is no longer allowed (#377).  "
        "Choose `select<I>(transport)` for the wire path, or "
        "`select_local<I>()` for the in-memory variant.  See "
        "SessionHandle<Select<...>>::select for the full discipline.");

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag&    crash_flag() const  noexcept { return *flag_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── CrashWatchedHandle<Offer<Bs...>, …> ────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// pick<I>() — peer's choice already known by caller; flag-gated.
// branch(transport, handler) — would need to interpose on transport
//   like RecordingSessionHandle does for Offer; for #400 Phase 1 we
//   ship pick<I> only.  branch() with auto-dispatch into Crash<Peer>
//   branches is the natural Phase 2 (combines #368's "walk every
//   Offer for crash branches" with this header's runtime wire).

template <typename... Branches, typename Resource,
          typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]] CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Offer<Branches...>,
                               CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS>>
{
    PermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
        "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
        "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(detail::stop_class_compatible_v<Offer<Branches...>, C>,
        "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
        "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol      = Offer<Branches...>;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using perm_set      = PS;
    using inner_type    = PermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx>;
    using stop_type     = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Offer<Branches...>,
                            CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS>>{loc}
        , inner_{std::move(inner)}, flag_{&flag}
    {
        detail::require_crash_watched_contract_<Offer<Branches...>, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept            = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    // Renamed to `pick_local<I>()` (#377) — surfaces the wire absence
    // for the Offer side too.  Same crash-detection semantics; the
    // local handle assumes branch I without receiving the peer label.
    template <std::size_t I>
        requires (I < sizeof...(Branches))
    [[nodiscard]] constexpr auto pick_local() &&
        -> std::expected<
            decltype(detail::wrap_crash_next_<PeerTag, C>(
                std::declval<inner_type>().template pick_local<I>(),
                std::declval<OneShotFlag&>())),
            detail::crash_event_for_t<PeerTag, Resource>>
    {
        if (flag_->peek()) [[unlikely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            // Recover Resource then explicitly detach the inner
            // handle so its destructor's abandonment-check passes.
            Resource recovered = std::move(inner_.resource());
            this->mark_consumed_();
            // wrap_crash_return (#430) bundles `inner.detach(reason)` +
            // `return std::unexpected(CrashEvent{...})`.  CrashEvent's
            // ctor is private; this helper is its only friend, so a
            // wrapper that returned unexpected without detaching would
            // not compile (the bug from #400's first replace_all).
            return wrap_crash_return<PeerTag>(
                std::move(inner_),
                detach_reason::TransportClosedOutOfBand{},
                std::move(recovered));
        }
        auto next = std::move(inner_).template pick_local<I>();
        this->mark_consumed_();
        return detail::wrap_crash_next_<PeerTag, C>(std::move(next), *flag_);
    }

    // Deleted `pick<I>()` overload (#377) — same discipline as
    // CrashWatchedHandle<Select<...>>::select; force every call site
    // to make the wire choice explicit.
    template <std::size_t I>
    void pick() && = delete(
        "[Wire_Variant_Required] CrashWatchedHandle<Offer<...>>::"
        "pick<I>() without arguments is no longer allowed (#377).  "
        "Use `pick_local<I>()` to advance without receiving a peer "
        "label, or call the peer-receiving variant when one is "
        "available.  See SessionHandle<Offer<...>>::pick for the "
        "full discipline.");

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag&    crash_flag() const  noexcept { return *flag_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── mint_crash_watched_session<PeerTag> — Universal Mint Pattern ───
// ═════════════════════════════════════════════════════════════════════
//
// Token mint per CLAUDE.md §XXI — wraps either an existing bare
// SessionHandle or a PermissionedSessionHandle in a CrashWatchedHandle
// bound to the supplied OneShotFlag and compile-time PeerTag.  Bare
// handles are adapted to `PS = EmptyPermSet`; permissioned handles
// preserve their exact consumer-side PermSet through every next-state
// wrapper.
//
// PeerTag must be specified explicitly — it is NOT deducible from the
// handle's type because the same handle can be watched against
// different peers (multi-peer sessions wrap a chain of CrashWatched-
// Handles, one per peer).
//
// `PeerTag` must have a non-empty `survivor_registry<PeerTag>`.
// On peer death, surviving permissions inherit per the
// `survivor_registry` / `inherits_from<DeadTag, SurvivorTag>` lattice
// in PermissionInherit.h.  Specializing the survivor registry at the
// peer tag declaration site is the load-bearing discipline.
//
// Convention compliance (CLAUDE.md §XXI):
//   * Name follows mint_<noun>: mint_crash_watched_session.
//   * Signature is a TOKEN MINT (no Ctx parameter); ownership of the
//     incoming handle is the proof of authority.
//   * [[nodiscard]] constexpr noexcept — purely structural wrap.
//   * Returns the concrete type CrashWatchedHandle<Proto, Resource,
//     PeerTag, LoopCtx, PS>; never type-erased.
//   * Discoverable via `grep "mint_crash_watched_session"`.
//
// Negative-compile fixtures (HS14):
//   test/safety_neg/neg_mint_crash_watched_session_non_handle.cpp
//   test/safety_neg/neg_mint_crash_watched_session_missing_peer_tag.cpp

template <typename PeerTag, CrashClass C = CrashClass::Abort,
          typename Proto, typename Resource, typename LoopCtx>
[[nodiscard]] constexpr auto mint_crash_watched_session(
    SessionHandle<Proto, Resource, LoopCtx> handle,
    OneShotFlag& flag) noexcept
{
    detail::require_crash_watched_contract_<Proto, C>();
    detail::require_crash_survivors_declared_<PeerTag>();

    Resource recovered = std::move(handle.resource());
    std::move(handle).detach(detach_reason::OwnerLifetimeBoundEarlyExit{});
    using PSLoopCtx =
        detail::permissioned_loop_ctx_from_bare_t<LoopCtx, EmptyPermSet>;
    return CrashWatchedHandle<Proto, Resource, PeerTag, C, PSLoopCtx, EmptyPermSet>{
        PermissionedSessionHandle<Proto, EmptyPermSet, Resource, PSLoopCtx>{
            std::move(recovered)},
        flag};
}

template <typename PeerTag, CrashClass C = CrashClass::Abort,
          typename Proto, typename PS, typename Resource, typename LoopCtx>
[[nodiscard]] constexpr auto mint_crash_watched_session(
    PermissionedSessionHandle<Proto, PS, Resource, LoopCtx> handle,
    OneShotFlag& flag) noexcept
{
    detail::require_crash_watched_contract_<Proto, C>();
    detail::require_crash_survivors_declared_<PeerTag>();

    return CrashWatchedHandle<Proto, Resource, PeerTag, C, LoopCtx, PS>{
        std::move(handle), flag};
}

// ═════════════════════════════════════════════════════════════════════
// ── Stop-aware unwrap helper ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Convenience for the common caller pattern: try a session op, fall
// through to crash recovery on the unexpected branch.  The recovery
// callback receives the survivor-aware CrashEvent by value and is
// expected to return whatever the caller's downstream code needs
// (usually a re-establishment outcome or a typed error to propagate
// further up).
//
//     auto outcome = on_crash(std::move(h).send(msg, tx),
//         [](detail::crash_event_for_t<PeerTag, R> ev) { ...recover... });

template <typename Expected, typename CrashHandler>
[[nodiscard]] constexpr auto on_crash(Expected&& result, CrashHandler&& handler)
    noexcept(std::is_nothrow_invocable_v<
                CrashHandler&&,
                typename std::remove_cvref_t<Expected>::error_type>)
{
    using Error = typename std::remove_cvref_t<Expected>::error_type;
    static_assert(CrashEventMatchesSurvivors<Error>,
        "CrashEvent survivor list must match survivors_t<PeerTag>.");

    if (result) {
        return std::forward<Expected>(result);
    }
    std::forward<CrashHandler>(handler)(std::move(result.error()));
    return std::forward<Expected>(result);
}

}  // namespace crucible::safety::proto
