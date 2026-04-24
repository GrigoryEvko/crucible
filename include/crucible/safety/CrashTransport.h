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
// acquire fence and return a `CrashEvent<PeerTag>` via
// `std::expected`.
//
// `CrashWatchedHandle<Proto, Resource, PeerTag, LoopCtx>` wraps a
// bare `SessionHandle<Proto, Resource, LoopCtx>` and a
// `OneShotFlag&`.  Each consumer method:
//
//   1. Peeks the flag (one relaxed load + branch — the spec's
//      ~1 cycle / op overhead budget).
//   2. On no-crash: forwards to the inner handle's consumer, marks
//      itself consumed, re-wraps the next-state handle in another
//      `CrashWatchedHandle` (bound to the same flag and peer tag).
//   3. On crash: takes an acquire fence (paired with the producer's
//      release in OneShotFlag::signal), marks itself consumed,
//      returns std::unexpected(CrashEvent<PeerTag>{recovered Resource}).
//      The Resource flows out so callers can re-establish a new
//      channel with whatever endpoint state survived.
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
// ─── References ───────────────────────────────────────────────────
//
//   misc/24_04_2026_safety_integration.md §11 — design rationale
//   safety/OneShotFlag.h          — the underlying signal primitive
//   safety/SessionCrash.h          — type-level crash semantics
//   safety/RecordingSessionHandle.h — sibling wrapper using the same
//                                     per-Proto specialization scaffold
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/OneShotFlag.h>
#include <crucible/safety/Session.h>
#include <crucible/safety/SessionCrash.h>

#include <atomic>
#include <cstddef>
#include <expected>
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
// ── CrashEvent<PeerTag> — the unexpected payload ───────────────────
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
// {...})` WITHOUT first detaching their inner SessionHandle, causing
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

template <typename PeerTag, typename Resource>
class CrashEvent {
public:
    using peer          = PeerTag;
    using resource_type = Resource;

    // Public read-access — backward-compatible with handler code that
    // reads `event.resource.field` directly (#430 keeps this open).
    Resource resource;

    // Pass-key-protected ctor.  Direct user construction
    // `CrashEvent<P, R>{r}` cannot compile — the user has no way to
    // mint a `WrapCrashReturnKey`.  The only authorized constructor
    // is `wrap_crash_return`, which is friended on the key class.
    constexpr CrashEvent(WrapCrashReturnKey, Resource r) noexcept
        : resource{std::move(r)} {}
};

// ═════════════════════════════════════════════════════════════════════
// ── wrap_crash_return — wrapper-side crash-path bundler (#430) ─────
// ═════════════════════════════════════════════════════════════════════
//
// Every wrapper around SessionHandle that returns
// `std::expected<NextHandle, CrashEvent<PeerTag, Resource>>` on a
// crash path MUST perform two operations atomically:
//
//   1. Detach the inner SessionHandle (`inner.detach(reason_tag)`) so
//      its destructor sees the consumed flag and skips the abandonment
//      abort.  Without this, the inner — at non-terminal protocol state
//      — fires SessionHandleBase's destructor abort when the wrapper's
//      crash-path stack frame unwinds.  This is exactly the bug shipped
//      in #400's first replace_all that caught only Send's path,
//      leaving recv / select / pick / branch silently aborting via the
//      inner's destructor; debug-instrumentation localised the hole and
//      the fix was a second pass that applied detach uniformly.
//
//   2. Construct the `CrashEvent<PeerTag, Resource>{recovered}` and
//      wrap in `std::unexpected{...}` so the caller gets the typed
//      crash signal back through `std::expected`'s error channel.
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
    return std::unexpected{
        CrashEvent<PeerTag, Resource>{
            WrapCrashReturnKey{}, std::move(recovered)}};
}

// ═════════════════════════════════════════════════════════════════════
// ── Forward declaration + factory ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename Proto, typename Resource,
          typename PeerTag, typename LoopCtx = void>
class CrashWatchedHandle;

namespace detail {

// Build a CrashWatchedHandle around a freshly-stepped inner handle,
// preserving the framework's Continue / Loop resolution.  Mirrors
// safety/RecordingSessionHandle.h's wrap_next_.
template <typename PeerTag, typename NextHandle>
[[nodiscard]] constexpr auto wrap_crash_next_(
    NextHandle inner,
    OneShotFlag& flag) noexcept
{
    using NextProto    = typename NextHandle::protocol;
    using NextResource = typename NextHandle::resource_type;
    using NextLoopCtx  = typename NextHandle::loop_ctx;
    return CrashWatchedHandle<NextProto, NextResource, PeerTag, NextLoopCtx>{
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

template <typename Resource, typename PeerTag, typename LoopCtx>
class [[nodiscard]] CrashWatchedHandle<End, Resource, PeerTag, LoopCtx>
    : public SessionHandleBase<End,
                               CrashWatchedHandle<End, Resource, PeerTag, LoopCtx>>
{
    SessionHandle<End, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    using protocol      = End;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using inner_type    = SessionHandle<End, Resource, LoopCtx>;

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<End,
                            CrashWatchedHandle<End, Resource, PeerTag, LoopCtx>>{loc}
        , inner_{std::move(inner)}, flag_{&flag} {}

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
          typename PeerTag, typename LoopCtx>
class [[nodiscard]] CrashWatchedHandle<Send<T, R>, Resource, PeerTag, LoopCtx>
    : public SessionHandleBase<Send<T, R>,
                               CrashWatchedHandle<Send<T, R>, Resource, PeerTag, LoopCtx>>
{
    SessionHandle<Send<T, R>, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    using protocol      = Send<T, R>;
    using message_type  = T;
    using continuation  = R;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using inner_type    = SessionHandle<Send<T, R>, Resource, LoopCtx>;

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Send<T, R>,
                            CrashWatchedHandle<Send<T, R>, Resource, PeerTag, LoopCtx>>{loc}
        , inner_{std::move(inner)}, flag_{&flag} {}

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept            = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr auto send(T value, Transport transport) &&
        -> std::expected<
            decltype(detail::wrap_crash_next_<PeerTag>(
                std::declval<inner_type>().send(std::move(value), std::move(transport)),
                std::declval<OneShotFlag&>())),
            CrashEvent<PeerTag, Resource>>
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
        return detail::wrap_crash_next_<PeerTag>(std::move(next), *flag_);
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag&    crash_flag() const  noexcept { return *flag_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── CrashWatchedHandle<Recv<T, R>, …> ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T, typename R, typename Resource,
          typename PeerTag, typename LoopCtx>
class [[nodiscard]] CrashWatchedHandle<Recv<T, R>, Resource, PeerTag, LoopCtx>
    : public SessionHandleBase<Recv<T, R>,
                               CrashWatchedHandle<Recv<T, R>, Resource, PeerTag, LoopCtx>>
{
    SessionHandle<Recv<T, R>, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    using protocol      = Recv<T, R>;
    using message_type  = T;
    using continuation  = R;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using inner_type    = SessionHandle<Recv<T, R>, Resource, LoopCtx>;

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Recv<T, R>,
                            CrashWatchedHandle<Recv<T, R>, Resource, PeerTag, LoopCtx>>{loc}
        , inner_{std::move(inner)}, flag_{&flag} {}

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
            std::pair<T, decltype(detail::wrap_crash_next_<PeerTag>(
                std::declval<inner_type>().recv(std::move(transport)).second,
                std::declval<OneShotFlag&>()))>,
            CrashEvent<PeerTag, Resource>>
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
            detail::wrap_crash_next_<PeerTag>(std::move(next), *flag_)};
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag&    crash_flag() const  noexcept { return *flag_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── CrashWatchedHandle<Select<Bs...>, …> ───────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename... Branches, typename Resource,
          typename PeerTag, typename LoopCtx>
class [[nodiscard]] CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, LoopCtx>
    : public SessionHandleBase<Select<Branches...>,
                               CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, LoopCtx>>
{
    SessionHandle<Select<Branches...>, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    using protocol      = Select<Branches...>;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using inner_type    = SessionHandle<Select<Branches...>, Resource, LoopCtx>;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Select<Branches...>,
                            CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, LoopCtx>>{loc}
        , inner_{std::move(inner)}, flag_{&flag} {}

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept            = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    // Transport-driven select: signal choice; route to crash if flag set.
    template <std::size_t I, typename Transport>
        requires (I < sizeof...(Branches))
              && std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) &&
        -> std::expected<
            decltype(detail::wrap_crash_next_<PeerTag>(
                std::declval<inner_type>().template select<I>(std::move(transport)),
                std::declval<OneShotFlag&>())),
            CrashEvent<PeerTag, Resource>>
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
        return detail::wrap_crash_next_<PeerTag>(std::move(next), *flag_);
    }

    // No-transport select.
    template <std::size_t I>
        requires (I < sizeof...(Branches))
    [[nodiscard]] constexpr auto select() &&
        -> std::expected<
            decltype(detail::wrap_crash_next_<PeerTag>(
                std::declval<inner_type>().template select<I>(),
                std::declval<OneShotFlag&>())),
            CrashEvent<PeerTag, Resource>>
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
        auto next = std::move(inner_).template select<I>();
        this->mark_consumed_();
        return detail::wrap_crash_next_<PeerTag>(std::move(next), *flag_);
    }

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
          typename PeerTag, typename LoopCtx>
class [[nodiscard]] CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, LoopCtx>
    : public SessionHandleBase<Offer<Branches...>,
                               CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, LoopCtx>>
{
    SessionHandle<Offer<Branches...>, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    using protocol      = Offer<Branches...>;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using peer          = PeerTag;
    using inner_type    = SessionHandle<Offer<Branches...>, Resource, LoopCtx>;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr CrashWatchedHandle(
        inner_type inner,
        OneShotFlag& flag,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Offer<Branches...>,
                            CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, LoopCtx>>{loc}
        , inner_{std::move(inner)}, flag_{&flag} {}

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept            = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    template <std::size_t I>
        requires (I < sizeof...(Branches))
    [[nodiscard]] constexpr auto pick() &&
        -> std::expected<
            decltype(detail::wrap_crash_next_<PeerTag>(
                std::declval<inner_type>().template pick<I>(),
                std::declval<OneShotFlag&>())),
            CrashEvent<PeerTag, Resource>>
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
        auto next = std::move(inner_).template pick<I>();
        this->mark_consumed_();
        return detail::wrap_crash_next_<PeerTag>(std::move(next), *flag_);
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag&    crash_flag() const  noexcept { return *flag_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── crash_watch — convenience factory ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Wrap an existing SessionHandle in a CrashWatchedHandle.  PeerTag
// must be specified explicitly (it's not deducible from the handle's
// type — the same handle could be watched against different peers).

template <typename PeerTag, typename Proto, typename Resource, typename LoopCtx>
[[nodiscard]] constexpr auto crash_watch(
    SessionHandle<Proto, Resource, LoopCtx> handle,
    OneShotFlag& flag) noexcept
{
    return CrashWatchedHandle<Proto, Resource, PeerTag, LoopCtx>{
        std::move(handle), flag};
}

// ═════════════════════════════════════════════════════════════════════
// ── Stop-aware unwrap helper ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Convenience for the common caller pattern: try a session op, fall
// through to crash recovery on the unexpected branch.  The recovery
// callback receives the `CrashEvent<PeerTag, Resource>` by value
// and is expected to return whatever the caller's downstream code
// needs (usually a re-establishment outcome or a typed error to
// propagate further up).
//
//     auto outcome = on_crash(std::move(h).send(msg, tx),
//         [](CrashEvent<PeerTag, R> ev) { ...recover... });

template <typename Expected, typename CrashHandler>
[[nodiscard]] constexpr auto on_crash(Expected&& result, CrashHandler&& handler)
    noexcept(std::is_nothrow_invocable_v<
                CrashHandler&&,
                typename std::remove_cvref_t<Expected>::error_type>)
{
    if (result) {
        return std::forward<Expected>(result);
    }
    std::forward<CrashHandler>(handler)(std::move(result.error()));
    return std::forward<Expected>(result);
}

}  // namespace crucible::safety::proto
