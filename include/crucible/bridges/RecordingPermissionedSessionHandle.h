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
// Phase 2 (follow-up tasks fixy-A2-006b/c/d/e) covers the delegation
// family — Delegate / Accept / EpochedDelegate / EpochedAccept.
// Those specializations are deferred because the recipient-side
// inner-handle PSH carries its own PermSet, and threading audit
// recording across the delegated-handle boundary is a separate
// design decision (does the inner PSH get its own log entry; does
// the recipient automatically wrap inner in a recording handle of
// its own; what role IDs identify the inner session) that warrants
// its own audit pass.
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

namespace detail {

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
// ── mint_recording_session — PSH overload (Phase 1) ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// fixy-A2-006 — §XXI Universal Mint Pattern: ship the missing PSH
// overload so permissioned channels can be audited.  Returns a
// RecordingPermissionedSessionHandle wrapping the passed PSH, with
// log + role context attached.  PSH overloads for the delegation
// family (Delegate / Accept / EpochedDelegate / EpochedAccept) are
// deferred to fixy-A2-006b/c/d/e — until those Phase 2 specializations
// ship, attempting `mint_recording_session(psh)` on a Delegate-headed
// PSH fails substitution with an "incomplete type" or
// "no matching specialization" diagnostic, which is the correct
// behavior given Phase 1 scope: the type system rejects unsupported
// protocols rather than silently dropping them.

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
            std::move(inner), log, self, peer};
}

}  // namespace crucible::safety::proto
