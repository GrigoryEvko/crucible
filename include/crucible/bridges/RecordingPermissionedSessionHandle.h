#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — RecordingPermissionedSessionHandle
//
// fixy-A2-006 (Phase 1) — audit-trail wrapper around the permissioned
// session handle.  The bare RecordingSessionHandle wraps the
// PermSet-unaware SessionHandle.  Production code that holds CSL
// permissions through session-protocol position (TraceRing,
// PermissionedSpscChannel, kernel-cache SWMR, observe broadcast)
// constructs PermissionedSessionHandle endpoints — without this
// header, those channels cannot record audit events into a
// SessionEventLog because no mint_recording_session overload accepts
// PSH.
//
// ─── Phase scope ───────────────────────────────────────────────────
//
// Phase 1 (this header) covers the production-critical PSH protocol
// heads that drive request/reply, fan-in/fan-out, and checkpointed
// flows:
//
//   RecordingPermissionedSessionHandle<End,                  PS, R, L>
//   RecordingPermissionedSessionHandle<Stop_g<C>,            PS, R, L>
//   RecordingPermissionedSessionHandle<Send<T, K>,           PS, R, L>
//   RecordingPermissionedSessionHandle<Recv<T, K>,           PS, R, L>
//   RecordingPermissionedSessionHandle<Select<Bs...>,        PS, R, L>
//   RecordingPermissionedSessionHandle<Offer<Bs...>,         PS, R, L>
//   RecordingPermissionedSessionHandle<CheckpointedSession<B, R>, PS, Res, L>
//
// Phase 2 (fixy-A2-006b/c/d/e) ships the delegation family — Delegate /
// Accept / EpochedDelegate / EpochedAccept.  All four ship in this
// header: Delegate (fixy-A2-006b), Accept (fixy-A2-006c),
// EpochedDelegate (fixy-A2-006d), EpochedAccept (fixy-A2-006e).
// Design decision for the delegation family: the inner-handle PSH
// (whether handed off by Delegate or produced by Accept) is passed
// THROUGH the wrapper unchanged.  Recipient-side auditing on the
// delegated channel is opt-in via a separate mint_recording_session
// call on the inner PSH — auto-wrapping would impose a hidden
// log/role assignment the recipient may not want.
//
// Epoch gates (fixy-A2-006d/e EpochedDelegate / EpochedAccept) are
// enforced by the INNER PSH at construction time via a class-body
// static_assert on LoopCtx; the recording wrapper inherits the gate
// transparently — a stale-epoch ctx fails to mint the carrier PSH,
// so the wrapper is never constructed.
//
// ─── Design contract ───────────────────────────────────────────────
//
// Mirrors RecordingSessionHandle's per-protocol class hierarchy.  Each
// specialization holds:
//
//   * inner_ : PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>
//     — the underlying PSH; we forward every public method through.
//   * log_   : SessionEventLog*    — the audit-trail destination.
//   * self_role_, peer_role_ : RoleTagId — caller-supplied identity.
//
// Records an event around each protocol step, then wraps the
// next-state PSH the inner method returned.  PS evolution happens
// inside the inner PSH; the recording wrapper is PS-transparent — it
// observes PS through perm_set typedef but doesn't manipulate it.
//
// ─── Wrapping next-state PSHs ─────────────────────────────────────
//
// wrap_next_permissioned_ is the analogue of detail::wrap_next_ for
// bare SessionHandle.  After inner_.method() returns a next-state
// PSH, we wrap that PSH in a fresh RecordingPSH preserving log + role
// IDs.  Continue / Loop unrolling happens INSIDE the inner PSH via
// detail::step_to_next_permissioned, so the recording wrapper always
// sees the post-unrolling head shape.
//
// ─── References ────────────────────────────────────────────────────
//
//   sessions/PermissionedSession.h    — the underlying PSH types
//   sessions/SessionEventLog.h        — log + SessionEvent factories
//   bridges/RecordingSessionHandle.h  — bare-handle recording analogue
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionEventLog.h>

#include <cstddef>
#include <source_location>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// Forward declaration; specialisations follow.
template <typename Proto,
          typename PS,
          typename Resource,
          typename LoopCtx = void>
class RecordingPermissionedSessionHandle;

// Forward declaration of the §XXI mint factory, so the passkey below
// can friend it before its definition (which follows all the
// specialisations at the bottom of this header).
template <typename Proto, typename PS, typename Resource, typename LoopCtx>
    requires ::crucible::safety::extract::IsSessionHandle<
        PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>>
[[nodiscard]] constexpr auto mint_recording_session(
    PermissionedSessionHandle<Proto, PS, Resource, LoopCtx> inner,
    SessionEventLog& log,
    RoleTagId self,
    RoleTagId peer) noexcept;

namespace detail {

// ─── §XXI Universal Mint Pattern closure (fix-15) ──────────────────
//
// Passkey gating construction of every RecordingPermissionedSessionHandle
// specialisation.  Its default constructor is private, so a value of
// this type can only be materialised by a friend.  Every public ctor of
// every specialisation takes a `recording_session_construct_key` as its
// FIRST parameter, making `mint_recording_session` the SOLE construction
// path (§XXI).  Mirrors detail::permissioned_session_construct_key in
// sessions/PermissionedSession.h:384.
//
// Authorised friends:
//   * mint_recording_session — the §XXI authorisation point.
//   * detail::wrap_next_permissioned_ — re-wraps the next-state inner
//     PSH after a protocol step (the recording layer's internal
//     re-construction site; see doc-comment §"Wrapping next-state PSHs").
//   * RecordingPermissionedSessionHandle — member methods (notably the
//     Offer<...> spec's branch(), which builds a fresh sibling-spec
//     RecordingPSH for each transport-driven branch handle) need to mint
//     the key to re-wrap.
struct recording_session_construct_key {
private:
    constexpr recording_session_construct_key() noexcept = default;

    template <typename NextHandle>
    friend constexpr auto wrap_next_permissioned_(
        NextHandle, SessionEventLog&, RoleTagId, RoleTagId) noexcept;

    template <typename Proto, typename PS, typename Resource, typename LoopCtx>
        requires ::crucible::safety::extract::IsSessionHandle<
            PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>>
    friend constexpr auto ::crucible::safety::proto::mint_recording_session(
        PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>,
        SessionEventLog&, RoleTagId, RoleTagId) noexcept;

    template <typename Proto, typename PS, typename Resource, typename LoopCtx>
    friend class ::crucible::safety::proto::RecordingPermissionedSessionHandle;
};

// Build a RecordingPermissionedSessionHandle from a freshly-stepped
// inner PSH.  Mirrors detail::wrap_next_ for the bare-handle case:
// pulls protocol, perm_set, resource_type, loop_ctx from the next
// handle's typedefs and forwards log + role context unchanged.
template <typename NextHandle>
[[nodiscard]] constexpr auto wrap_next_permissioned_(
    NextHandle        next,
    SessionEventLog&  log,
    RoleTagId         self_role,
    RoleTagId         peer_role) noexcept
{
    using NextProto    = typename NextHandle::protocol;
    using NextPS       = typename NextHandle::perm_set;
    using NextResource = typename NextHandle::resource_type;
    using NextLoopCtx  = typename NextHandle::loop_ctx;
    return RecordingPermissionedSessionHandle<
        NextProto, NextPS, NextResource, NextLoopCtx>{
            recording_session_construct_key{},
            std::move(next), log, self_role, peer_role};
}

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<End, PS, Resource, LoopCtx> ──
// ═════════════════════════════════════════════════════════════════════

template <typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<End, PS, Resource, LoopCtx>
    : public SessionHandleBase<
          End,
          RecordingPermissionedSessionHandle<End, PS, Resource, LoopCtx>>
{
    PermissionedSessionHandle<End, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = End;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    =
        PermissionedSessionHandle<End, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              End,
              RecordingPermissionedSessionHandle<
                  End, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    [[nodiscard]] constexpr Resource close() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        log_->append_event(SessionEvent{
            .from_role = self_role_,
            .to_role   = peer_role_,
            .op        = SessionOp::Close,
        });
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<Stop_g<C>, PS, Resource, L> ──
// ═════════════════════════════════════════════════════════════════════

template <CrashClass C, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>
    : public SessionHandleBase<
          Stop_g<C>,
          RecordingPermissionedSessionHandle<
              Stop_g<C>, PS, Resource, LoopCtx>>
{
    PermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = Stop_g<C>;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    =
        PermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>;
    static constexpr CrashClass crash_class = C;

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              Stop_g<C>,
              RecordingPermissionedSessionHandle<
                  Stop_g<C>, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    [[nodiscard]] constexpr Resource close(
        StopReasonKind reason = StopReasonKind::PeerCrashed,
        RecoveryPathHash recovery_path = {}) &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        log_->append_event(SessionEvent::stop(
            self_role_, peer_role_, peer_role_, reason, recovery_path));
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<Send<T, K>, PS, R, L> ────────
// ═════════════════════════════════════════════════════════════════════

template <typename T, typename K, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<Send<T, K>, PS, Resource, LoopCtx>
    : public SessionHandleBase<
          Send<T, K>,
          RecordingPermissionedSessionHandle<
              Send<T, K>, PS, Resource, LoopCtx>>
{
    PermissionedSessionHandle<Send<T, K>, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = Send<T, K>;
    using payload       = T;
    using continuation  = K;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    =
        PermissionedSessionHandle<Send<T, K>, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              Send<T, K>,
              RecordingPermissionedSessionHandle<
                  Send<T, K>, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <typename U = T, typename Transport>
        requires is_subsort_v<std::remove_cvref_t<U>, T> &&
                 std::is_invocable_v<Transport, Resource&, U&&>
    [[nodiscard]] constexpr auto send(U value, Transport transport) &&
    {
        log_->append_event(SessionEvent{
            .from_role      = self_role_,
            .to_role        = peer_role_,
            .payload_schema = default_schema_hash<T>,
            .payload_hash   = default_payload_hash_fn<T>(value),
            .op             = SessionOp::Send,
        });
        this->mark_consumed_();
        auto next = std::move(inner_).send(
            std::move(value), std::move(transport));
        return detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<Recv<T, K>, PS, R, L> ────────
// ═════════════════════════════════════════════════════════════════════

template <typename T, typename K, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<Recv<T, K>, PS, Resource, LoopCtx>
    : public SessionHandleBase<
          Recv<T, K>,
          RecordingPermissionedSessionHandle<
              Recv<T, K>, PS, Resource, LoopCtx>>
{
    PermissionedSessionHandle<Recv<T, K>, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = Recv<T, K>;
    using payload       = T;
    using continuation  = K;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    =
        PermissionedSessionHandle<Recv<T, K>, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              Recv<T, K>,
              RecordingPermissionedSessionHandle<
                  Recv<T, K>, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto recv(Transport transport) &&
    {
        this->mark_consumed_();
        auto pair_result = std::move(inner_).recv(std::move(transport));
        auto&  value     = pair_result.first;
        auto&& next      = std::move(pair_result.second);
        log_->append_event(SessionEvent{
            .from_role      = peer_role_,
            .to_role        = self_role_,
            .payload_schema = default_schema_hash<T>,
            .payload_hash   = default_payload_hash_fn<T>(value),
            .op             = SessionOp::Recv,
        });
        return std::pair{
            std::move(pair_result.first),
            detail::wrap_next_permissioned_(
                std::move(next), *log_, self_role_, peer_role_)};
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<Select<Bs...>, PS, R, L> ─────
// ═════════════════════════════════════════════════════════════════════

template <typename... Branches, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<Select<Branches...>, PS,
                                   Resource, LoopCtx>
    : public SessionHandleBase<
          Select<Branches...>,
          RecordingPermissionedSessionHandle<
              Select<Branches...>, PS, Resource, LoopCtx>>
{
    PermissionedSessionHandle<Select<Branches...>, PS,
                              Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = Select<Branches...>;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    =
        PermissionedSessionHandle<Select<Branches...>, PS,
                                  Resource, LoopCtx>;
    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              Select<Branches...>,
              RecordingPermissionedSessionHandle<
                  Select<Branches...>, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <std::size_t I, typename Transport>
        requires (I < sizeof...(Branches))
              && std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) &&
    {
        log_->append_event(SessionEvent{
            .from_role    = self_role_,
            .to_role      = peer_role_,
            .op           = SessionOp::Select,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template select<I>(std::move(transport));
        return detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
    }

    template <std::size_t I>
        requires (I < sizeof...(Branches))
    [[nodiscard]] constexpr auto select_local() &&
    {
        log_->append_event(SessionEvent{
            .from_role    = self_role_,
            .to_role      = peer_role_,
            .op           = SessionOp::Select,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template select_local<I>();
        return detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
    }

    template <std::size_t I>
    void select() && = delete(
        "[Wire_Variant_Required] RecordingPermissionedSessionHandle<"
        "Select<...>>::select<I>() without arguments is not allowed.  "
        "Choose select<I>(transport) for wire-based sessions or "
        "select_local<I>() for in-memory channels (mirror of "
        "PermissionedSession.h:#377 discipline).");

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<Offer<Bs...>, PS, R, L> ──────
// ═════════════════════════════════════════════════════════════════════

template <typename... Branches, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<Offer<Branches...>, PS,
                                   Resource, LoopCtx>
    : public SessionHandleBase<
          Offer<Branches...>,
          RecordingPermissionedSessionHandle<
              Offer<Branches...>, PS, Resource, LoopCtx>>
{
    PermissionedSessionHandle<Offer<Branches...>, PS,
                              Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = Offer<Branches...>;
    using perm_set      = PS;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    =
        PermissionedSessionHandle<Offer<Branches...>, PS,
                                  Resource, LoopCtx>;
    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              Offer<Branches...>,
              RecordingPermissionedSessionHandle<
                  Offer<Branches...>, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <std::size_t I>
        requires (I < sizeof...(Branches))
    [[nodiscard]] constexpr auto pick_local() &&
    {
        log_->append_event(SessionEvent{
            .from_role    = peer_role_,    // peer made the choice
            .to_role      = self_role_,    // self learns the choice
            .op           = SessionOp::Offer,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template pick_local<I>();
        return detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
    }

    template <std::size_t I>
    void pick() && = delete(
        "[Wire_Variant_Required] RecordingPermissionedSessionHandle<"
        "Offer<...>>::pick<I>() without arguments is not allowed.  Use "
        "pick_local<I>() for in-memory channels or branch(transport, "
        "handler) for transport-driven dispatch.");

    // Transport-driven branch — mirrors RecordingSessionHandle's
    // pattern: interpose on the transport to capture the chosen
    // branch index for the log entry, then wrap each branch handle in
    // its own RecordingPSH before invoking the user's handler.
    template <typename Transport, typename Handler>
        requires std::is_invocable_r_v<std::size_t, Transport, Resource&>
    constexpr auto branch(Transport transport, Handler handler) &&
    {
        auto* log_ptr     = log_;
        auto  self_role   = self_role_;
        auto  peer_role   = peer_role_;

        auto recording_handler =
            [log_ptr, self_role, peer_role,
             handler = std::move(handler)](auto inner_branch_handle) mutable {
                using BranchHandle   = decltype(inner_branch_handle);
                using BranchProto    = typename BranchHandle::protocol;
                using BranchPS       = typename BranchHandle::perm_set;
                using BranchResource = typename BranchHandle::resource_type;
                using BranchLoopCtx  = typename BranchHandle::loop_ctx;
                RecordingPermissionedSessionHandle<
                    BranchProto, BranchPS, BranchResource, BranchLoopCtx>
                    wrapped_branch{
                        detail::recording_session_construct_key{},
                        std::move(inner_branch_handle),
                        *log_ptr, self_role, peer_role};
                return std::invoke(std::move(handler),
                                   std::move(wrapped_branch));
            };

        auto recording_transport =
            [log_ptr, self_role, peer_role,
             tx = std::move(transport)](Resource& r) mutable -> std::size_t {
                const std::size_t idx = std::invoke(tx, r);
                log_ptr->append_event(SessionEvent{
                    .from_role    = peer_role,
                    .to_role      = self_role,
                    .op           = SessionOp::Offer,
                    .branch_index = static_cast<uint8_t>(idx),
                });
                return idx;
            };

        this->mark_consumed_();
        return std::move(inner_).branch(
            std::move(recording_transport),
            std::move(recording_handler));
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<CheckpointedSession<B, R>, …>
// ═════════════════════════════════════════════════════════════════════

template <typename ProtoBase, typename ProtoRollback,
          typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<
    CheckpointedSession<ProtoBase, ProtoRollback>,
    PS, Resource, LoopCtx>
    : public SessionHandleBase<
          CheckpointedSession<ProtoBase, ProtoRollback>,
          RecordingPermissionedSessionHandle<
              CheckpointedSession<ProtoBase, ProtoRollback>,
              PS, Resource, LoopCtx>>
{
    PermissionedSessionHandle<
        CheckpointedSession<ProtoBase, ProtoRollback>,
        PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol          = CheckpointedSession<ProtoBase, ProtoRollback>;
    using base_protocol     = ProtoBase;
    using rollback_protocol = ProtoRollback;
    using perm_set          = PS;
    using resource_type     = Resource;
    using loop_ctx          = LoopCtx;
    using inner_type        =
        PermissionedSessionHandle<protocol, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              protocol,
              RecordingPermissionedSessionHandle<
                  protocol, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    [[nodiscard]] constexpr auto base(
        CheckpointId checkpoint = {},
        ::crucible::ContentHash saved_state = {}) &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        log_->append_event(SessionEvent::checkpoint_base(
            self_role_, peer_role_, checkpoint, saved_state));
        this->mark_consumed_();
        auto next = std::move(inner_).base();
        return detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr auto rollback(
        CheckpointId checkpoint = {},
        ::crucible::ContentHash saved_state = {}) &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        log_->append_event(SessionEvent::checkpoint_rollback(
            self_role_, peer_role_, checkpoint, saved_state));
        this->mark_consumed_();
        auto next = std::move(inner_).rollback();
        return detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<
//        Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
//        PS, Resource, LoopCtx>
// ═════════════════════════════════════════════════════════════════════
//
// fixy-A2-006b — PSH Delegate specialization.  Mirrors the bare-handle
// RecordingSessionHandle<Delegate<T, K>, ...> specialization at
// bridges/RecordingSessionHandle.h:916.  Two key shape differences:
//
//   1. Carrier protocol is Delegate<DelegatedSession<InnerProto, InnerPS>, K>
//      (not Delegate<T, K>) because PSH-level delegation always uses the
//      DelegatedSession marker to thread the inner endpoint's PermSet.
//   2. The delegated-handle parameter binds to
//      PermissionedSessionHandle<InnerProto, ActualInnerPS, ...>
//      (not bare SessionHandle).  The carrier's PSH delegate() method
//      already static_asserts perm_set_equal_v<ActualInnerPS, InnerPS>
//      so the recording wrapper does not duplicate the check.
//
// Recording semantics: emits SessionEvent::delegate_handoff{
//     from = self_role, to = peer_role,
//     payload_schema = default_proto_hash<InnerProto>,
//     payload_hash   = inner_perm_set.value,
//     op = SessionOp::Delegate
// } BEFORE the inner delegate(...) call, then forwards to inner_ and
// wraps the resulting next-state PSH<K, PS, Resource, LoopCtx> in a
// fresh RecordingPSH preserving log + role context.
//
// Inner-handle wrapping decision (open in Phase 1 doc): the inner
// PermissionedSessionHandle is passed THROUGH to inner_.delegate(...)
// unchanged — the user wraps the inner endpoint with its own
// mint_recording_session call if they want recipient-side auditing on
// the delegated channel.  Auto-wrapping the inner PSH would impose a
// hidden log/role assignment that the recipient may not want.

template <typename InnerProto, typename InnerPS, typename K, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<
    Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
    PS, Resource, LoopCtx>
    : public SessionHandleBase<
          Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
          RecordingPermissionedSessionHandle<
              Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
              PS, Resource, LoopCtx>>
{
    using Protocol = Delegate<DelegatedSession<InnerProto, InnerPS>, K>;

    PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol          = Protocol;
    using delegated_proto   = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set    = InnerPS;
    using continuation      = K;
    using perm_set          = PS;
    using resource_type     = Resource;
    using loop_ctx          = LoopCtx;
    using inner_type        =
        PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              Protocol,
              RecordingPermissionedSessionHandle<
                  Protocol, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    // Wire variant — invokes Transport(resource, delegated.resource).
    template <typename ActualInnerPS, typename DelegatedResource,
              typename DelegatedLoopCtx, typename Transport>
        requires (!is_stop_v<InnerProto> &&
                  std::is_invocable_v<Transport, Resource&, DelegatedResource&&>)
    [[nodiscard]] constexpr auto delegate(
        PermissionedSessionHandle<InnerProto, ActualInnerPS,
                                  DelegatedResource, DelegatedLoopCtx>&& delegated,
        Transport transport,
        InnerPermSetHash inner_ps_hash = {}) &&
    {
        log_->append_event(SessionEvent::delegate_handoff(
            self_role_, peer_role_,
            default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate(
            std::move(delegated), std::move(transport));
        return detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
    }

    // In-memory variant — no transport, delegated-handle is consumed.
    template <typename ActualInnerPS, typename DelegatedResource,
              typename DelegatedLoopCtx>
        requires (!is_stop_v<InnerProto>)
    [[nodiscard]] constexpr auto delegate_local(
        PermissionedSessionHandle<InnerProto, ActualInnerPS,
                                  DelegatedResource, DelegatedLoopCtx>&& delegated,
        InnerPermSetHash inner_ps_hash = {}) &&
    {
        log_->append_event(SessionEvent::delegate_handoff(
            self_role_, peer_role_,
            default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate_local(std::move(delegated));
        return detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<
// ──        Accept<DelegatedSession<InnerProto, InnerPS>, K>,
// ──        PS, Resource, LoopCtx>
// ═════════════════════════════════════════════════════════════════════
//
// fixy-A2-006c — PSH Accept specialization.  Mirrors the bare-handle
// RecordingSessionHandle<Accept<T, K>, ...> specialization at
// bridges/RecordingSessionHandle.h:994.  The receiver-side dual of
// Delegate: where Delegate hands an inner endpoint OFF (carrier emits
// it via Transport), Accept produces an inner endpoint by INVERTING
// Transport on the carrier resource and returning it to the user
// alongside the continuation.
//
// Recording semantics: emits SessionEvent::accept_handoff{
//     from = self_role, to = peer_role,
//     payload_schema = default_proto_hash<InnerProto>,
//     payload_hash   = inner_perm_set.value,
//     op = SessionOp::Accept
// } AFTER inner_.accept(...) produces the delegated handle (the inner
// PSH must already exist before we can log its identity), then wraps
// the continuation PSH<K, PS, Resource, LoopCtx> in a fresh recording
// wrapper preserving log + role context.
//
// Inner-handle wrapping decision (parity with the Delegate spec): the
// returned delegated PermissionedSessionHandle is passed THROUGH to the
// caller unwrapped — they may audit it via a separate
// mint_recording_session call if recipient-side auditing is desired.
// Auto-wrapping the delegated PSH would impose a hidden log/role
// assignment that the recipient may not want.

template <typename InnerProto, typename InnerPS, typename K, typename PS,
          typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<
    Accept<DelegatedSession<InnerProto, InnerPS>, K>,
    PS, Resource, LoopCtx>
    : public SessionHandleBase<
          Accept<DelegatedSession<InnerProto, InnerPS>, K>,
          RecordingPermissionedSessionHandle<
              Accept<DelegatedSession<InnerProto, InnerPS>, K>,
              PS, Resource, LoopCtx>>
{
    using Protocol = Accept<DelegatedSession<InnerProto, InnerPS>, K>;

    PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol          = Protocol;
    using delegated_proto   = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set    = InnerPS;
    using continuation      = K;
    using perm_set          = PS;
    using resource_type     = Resource;
    using loop_ctx          = LoopCtx;
    using inner_type        =
        PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              Protocol,
              RecordingPermissionedSessionHandle<
                  Protocol, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    // Wire variant — Transport projects the carrier resource into the
    // delegated resource.  Mirror of inner PSH's requires clause.
    template <typename Transport,
              typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto accept(
        Transport transport,
        InnerPermSetHash inner_ps_hash = {}) &&
    {
        auto [delegated_handle, next] =
            std::move(inner_).accept(std::move(transport));
        log_->append_event(SessionEvent::accept_handoff(
            self_role_, peer_role_,
            default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    // In-memory variant — caller supplies the delegated resource directly.
    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto accept_with(
        DelegatedResource delegated_res,
        InnerPermSetHash inner_ps_hash = {}) &&
    {
        auto [delegated_handle, next] =
            std::move(inner_).accept_with(std::move(delegated_res));
        log_->append_event(SessionEvent::accept_handoff(
            self_role_, peer_role_,
            default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<
// ──        EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K,
// ──                        MinEpoch, MinGeneration>,
// ──        PS, Resource, LoopCtx>
// ═════════════════════════════════════════════════════════════════════
//
// fixy-A2-006d — PSH EpochedDelegate specialization.  The method
// signatures (.delegate / .delegate_local) and per-call requires
// clauses are STRUCTURALLY IDENTICAL to the plain Delegate spec at
// fixy-A2-006b: forwarding through inner_, emitting a delegate_handoff
// event, and wrapping the next-state PSH preserving log + role context.
//
// What changes vs Delegate: the inner PSH adds a class-level
//   static_assert(session_loop_ctx_epoch_matches_v<
//                 LoopCtx, MinEpoch, MinGeneration>);
// which fires AT CARRIER CONSTRUCTION TIME (during the carrier's
// mint_permissioned_session call, before any recording wrapper is
// involved).  The recording wrapper transparently inherits this gate
// — a stale-epoch ctx fails to mint the carrier PSH, so the wrapper
// is never constructed.
//
// Recording semantics (same as Delegate): emit
// SessionEvent::delegate_handoff(self, peer,
//   default_proto_hash<InnerProto>, inner_perm_set) BEFORE
// inner_.delegate(...) forwards.

template <typename InnerProto, typename InnerPS, typename K,
          std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<
    EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K,
                    MinEpoch, MinGeneration>,
    PS, Resource, LoopCtx>
    : public SessionHandleBase<
          EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K,
                          MinEpoch, MinGeneration>,
          RecordingPermissionedSessionHandle<
              EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K,
                              MinEpoch, MinGeneration>,
              PS, Resource, LoopCtx>>
{
    using Protocol = EpochedDelegate<
        DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>;

    PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol          = Protocol;
    using delegated_proto   = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set    = InnerPS;
    using continuation      = K;
    using perm_set          = PS;
    using resource_type     = Resource;
    using loop_ctx          = LoopCtx;
    using inner_type        =
        PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>;
    static constexpr std::uint64_t min_epoch      = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              Protocol,
              RecordingPermissionedSessionHandle<
                  Protocol, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    // Wire variant — Transport hands the carrier's resource off into
    // the delegated channel's wire.
    template <typename ActualInnerPS, typename DelegatedResource,
              typename DelegatedLoopCtx, typename Transport>
        requires (!is_stop_v<InnerProto> &&
                  std::is_invocable_v<Transport, Resource&, DelegatedResource&&>)
    [[nodiscard]] constexpr auto delegate(
        PermissionedSessionHandle<InnerProto, ActualInnerPS,
                                  DelegatedResource, DelegatedLoopCtx>&& delegated,
        Transport transport,
        InnerPermSetHash inner_ps_hash = {}) &&
    {
        log_->append_event(SessionEvent::delegate_handoff(
            self_role_, peer_role_,
            default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate(
            std::move(delegated), std::move(transport));
        return detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
    }

    // In-memory variant — no transport, delegated-handle is consumed.
    template <typename ActualInnerPS, typename DelegatedResource,
              typename DelegatedLoopCtx>
        requires (!is_stop_v<InnerProto>)
    [[nodiscard]] constexpr auto delegate_local(
        PermissionedSessionHandle<InnerProto, ActualInnerPS,
                                  DelegatedResource, DelegatedLoopCtx>&& delegated,
        InnerPermSetHash inner_ps_hash = {}) &&
    {
        log_->append_event(SessionEvent::delegate_handoff(
            self_role_, peer_role_,
            default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate_local(std::move(delegated));
        return detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingPermissionedSessionHandle<
// ──        EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K,
// ──                      MinEpoch, MinGeneration>,
// ──        PS, Resource, LoopCtx>
// ═════════════════════════════════════════════════════════════════════
//
// fixy-A2-006e — PSH EpochedAccept specialization.  Closes the A2-006
// delegation family.  Structurally identical to the plain Accept spec
// at fixy-A2-006c: forwards through inner_.accept(...) /
// inner_.accept_with(...), emits SessionEvent::accept_handoff(self,
// peer, default_proto_hash<InnerProto>, inner_perm_set) AFTER the
// inner PSH produces the delegated handle, and wraps the next-state
// PSH preserving log + role context.
//
// What changes vs Accept: the inner PSH adds a class-level
//   static_assert(session_loop_ctx_epoch_satisfies_v<
//                 LoopCtx, MinEpoch, MinGeneration>);
// which fires AT CARRIER CONSTRUCTION TIME (during the carrier's
// mint_permissioned_session call, before any recording wrapper is
// involved).  The recording wrapper transparently inherits this gate
// — a stale-epoch ctx fails to mint the carrier PSH, so the wrapper
// is never constructed.  Distinct from EpochedDelegate's matching
// gate (`_matches_v`, exact equality): EpochedAccept uses the
// satisfies-relation (recipient may be newer than the declared
// minimum but must not be stale or unannotated).
//
// Inner-handle wrapping decision (parity with Accept spec): the
// returned delegated PermissionedSessionHandle is passed THROUGH to
// the caller unwrapped — they may audit it via a separate
// mint_recording_session call if recipient-side auditing is desired.

template <typename InnerProto, typename InnerPS, typename K,
          std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<
    EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K,
                  MinEpoch, MinGeneration>,
    PS, Resource, LoopCtx>
    : public SessionHandleBase<
          EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K,
                        MinEpoch, MinGeneration>,
          RecordingPermissionedSessionHandle<
              EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K,
                            MinEpoch, MinGeneration>,
              PS, Resource, LoopCtx>>
{
    using Protocol = EpochedAccept<
        DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>;

    PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol          = Protocol;
    using delegated_proto   = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set    = InnerPS;
    using continuation      = K;
    using perm_set          = PS;
    using resource_type     = Resource;
    using loop_ctx          = LoopCtx;
    using inner_type        =
        PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>;
    static constexpr std::uint64_t min_epoch      = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;

    constexpr RecordingPermissionedSessionHandle(
        detail::recording_session_construct_key,
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<
              Protocol,
              RecordingPermissionedSessionHandle<
                  Protocol, PS, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(
        RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    // Wire variant — Transport projects the carrier resource into the
    // delegated resource.  Mirror of inner PSH's requires clause.
    template <typename Transport,
              typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto accept(
        Transport transport,
        InnerPermSetHash inner_ps_hash = {}) &&
    {
        auto [delegated_handle, next] =
            std::move(inner_).accept(std::move(transport));
        log_->append_event(SessionEvent::accept_handoff(
            self_role_, peer_role_,
            default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    // In-memory variant — caller supplies the delegated resource directly.
    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto accept_with(
        DelegatedResource delegated_res,
        InnerPermSetHash inner_ps_hash = {}) &&
    {
        auto [delegated_handle, next] =
            std::move(inner_).accept_with(std::move(delegated_res));
        log_->append_event(SessionEvent::accept_handoff(
            self_role_, peer_role_,
            default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_permissioned_(
            std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── mint_recording_session — PSH overload (Phase 1) ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// fixy-A2-006 — §XXI Universal Mint Pattern: ship the missing PSH
// overload so permissioned channels can be audited.  Returns a
// RecordingPermissionedSessionHandle wrapping the passed PSH, with
// log + role context attached.  fixy-A2-006b shipped Delegate;
// fixy-A2-006c shipped Accept; fixy-A2-006d shipped EpochedDelegate;
// fixy-A2-006e (this file) ships EpochedAccept — completing the
// delegation family.

// fixy-A2-026: explicit §XXI requires-clause.  Tautologically true
// because PermissionedSessionHandle<...> inherits from
// SessionHandleBase via PermissionedSessionHandleImpl; the clause is
// for grep-discoverability of the cross-tier authorization gate.

template <typename Proto, typename PS,
          typename Resource, typename LoopCtx>
    requires ::crucible::safety::extract::IsSessionHandle<
        PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>>
[[nodiscard]] constexpr auto mint_recording_session(
    PermissionedSessionHandle<Proto, PS, Resource, LoopCtx> inner,
    SessionEventLog& log,
    RoleTagId self,
    RoleTagId peer) noexcept
{
    return RecordingPermissionedSessionHandle<
        Proto, PS, Resource, LoopCtx>{
            detail::recording_session_construct_key{},
            std::move(inner), log, self, peer};
}

}  // namespace crucible::safety::proto
