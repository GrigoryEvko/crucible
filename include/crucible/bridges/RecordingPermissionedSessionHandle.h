#pragma once

// Recording is transparent to permissions.  The permission set evolves inside
// the wrapped handle at each step, and this layer only reads the resulting
// type.  Nothing here adds, splits or consumes a permission.
//
// A delegated endpoint passes through unrecorded, whether it is handed off or
// received.  The alternative, wrapping it automatically, would assign it a log
// and a pair of role identities chosen by the sender rather than by the side
// that will actually drive it.  A recipient that wants an audit trail on the
// delegated channel mints its own recording wrapper over it.
//
// Epoch admission is not checked here.  The wrapped handle asserts it at its
// own construction, which happens first, so a stale epoch fails before a
// recording wrapper can exist.

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

template <typename Proto, typename PS, typename Resource, typename LoopCtx = void>
class RecordingPermissionedSessionHandle;

// The mint is declared here so the pass-key can friend it.  Its definition
// needs every specialisation and therefore comes at the end of the file.
template <typename Proto, typename PS, typename Resource, typename LoopCtx>
    requires ::crucible::safety::extract::IsSessionHandle<PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>>
[[nodiscard]] constexpr auto mint_recording_session(PermissionedSessionHandle<Proto, PS, Resource, LoopCtx> inner,
                                                    SessionEventLog& log, RoleTagId self, RoleTagId peer) noexcept;

namespace detail {

// Every specialisation takes this key as its first constructor parameter, and
// only a friend can produce one, so the mint is the sole way in from outside.
// The wrappers are also friends of it: a step re-wraps the next handle, and
// the branch method builds one wrapper per branch, both from inside the class.
struct recording_session_construct_key {
private:
    constexpr recording_session_construct_key() noexcept = default;

    template <typename NextHandle>
    friend constexpr auto wrap_next_permissioned_(NextHandle, SessionEventLog&, RoleTagId, RoleTagId) noexcept;

    template <typename Proto, typename PS, typename Resource, typename LoopCtx>
        requires ::crucible::safety::extract::IsSessionHandle<PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>>
    friend constexpr auto ::crucible::safety::proto::mint_recording_session(
        PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>, SessionEventLog&, RoleTagId, RoleTagId) noexcept;

    template <typename Proto, typename PS, typename Resource, typename LoopCtx>
    friend class ::crucible::safety::proto::RecordingPermissionedSessionHandle;
};

template <typename NextHandle>
[[nodiscard]] constexpr auto wrap_next_permissioned_(NextHandle next, SessionEventLog& log, RoleTagId self_role,
                                                     RoleTagId peer_role) noexcept {
    using NextProto = typename NextHandle::protocol;
    using NextPS = typename NextHandle::perm_set;
    using NextResource = typename NextHandle::resource_type;
    using NextLoopCtx = typename NextHandle::loop_ctx;
    return RecordingPermissionedSessionHandle<NextProto, NextPS, NextResource, NextLoopCtx>{
        recording_session_construct_key{}, std::move(next), log, self_role, peer_role};
}

}  // namespace detail

template <typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingPermissionedSessionHandle<End, PS, Resource, LoopCtx>
    : public SessionHandleBase<End, RecordingPermissionedSessionHandle<End, PS, Resource, LoopCtx>> {
    PermissionedSessionHandle<End, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = End;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<End, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<End, RecordingPermissionedSessionHandle<End, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        log_->append_event(SessionEvent{
            .from_role = self_role_,
            .to_role = peer_role_,
            .op = SessionOp::Close,
        });
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <CrashClass C, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Stop_g<C>, RecordingPermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>> {
    PermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Stop_g<C>;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>;
    static constexpr CrashClass crash_class = C;

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Stop_g<C>, RecordingPermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    [[nodiscard]] constexpr Resource
    close(StopReasonKind reason = StopReasonKind::PeerCrashed,
          RecoveryPathHash recovery_path = {}) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        log_->append_event(SessionEvent::stop(self_role_, peer_role_, peer_role_, reason, recovery_path));
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename T, typename K, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingPermissionedSessionHandle<Send<T, K>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Send<T, K>, RecordingPermissionedSessionHandle<Send<T, K>, PS, Resource, LoopCtx>> {
    PermissionedSessionHandle<Send<T, K>, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Send<T, K>;
    using payload = T;
    using continuation = K;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<Send<T, K>, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Send<T, K>, RecordingPermissionedSessionHandle<Send<T, K>, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <typename U = T, typename Transport>
        requires is_subsort_v<std::remove_cvref_t<U>, T> && std::is_invocable_v<Transport, Resource&, U&&>
    [[nodiscard]] constexpr auto send(U value, Transport transport) && {
        log_->append_event(SessionEvent{
            .from_role = self_role_,
            .to_role = peer_role_,
            .payload_schema = default_schema_hash<T>,
            .payload_hash = default_payload_hash_fn<T>(value),
            .op = SessionOp::Send,
        });
        this->mark_consumed_();
        auto next = std::move(inner_).send(std::move(value), std::move(transport));
        return detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename T, typename K, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingPermissionedSessionHandle<Recv<T, K>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Recv<T, K>, RecordingPermissionedSessionHandle<Recv<T, K>, PS, Resource, LoopCtx>> {
    PermissionedSessionHandle<Recv<T, K>, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Recv<T, K>;
    using payload = T;
    using continuation = K;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<Recv<T, K>, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Recv<T, K>, RecordingPermissionedSessionHandle<Recv<T, K>, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        this->mark_consumed_();
        auto pair_result = std::move(inner_).recv(std::move(transport));
        auto& value = pair_result.first;
        auto&& next = std::move(pair_result.second);
        log_->append_event(SessionEvent{
            .from_role = peer_role_,
            .to_role = self_role_,
            .payload_schema = default_schema_hash<T>,
            .payload_hash = default_payload_hash_fn<T>(value),
            .op = SessionOp::Recv,
        });
        return std::pair{std::move(pair_result.first),
                         detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename... Branches, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingPermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Select<Branches...>,
                               RecordingPermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx>> {
    PermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Select<Branches...>;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx>;
    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Select<Branches...>,
                            RecordingPermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <std::size_t I, typename Transport>
        requires(I < sizeof...(Branches)) && std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && {
        log_->append_event(SessionEvent{
            .from_role = self_role_,
            .to_role = peer_role_,
            .op = SessionOp::Select,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template select<I>(std::move(transport));
        return detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
    }

    template <std::size_t I>
        requires(I < sizeof...(Branches))
    [[nodiscard]] constexpr auto select_local() && {
        log_->append_event(SessionEvent{
            .from_role = self_role_,
            .to_role = peer_role_,
            .op = SessionOp::Select,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template select_local<I>();
        return detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
    }

    template <std::size_t I>
    void select() && = delete("[Wire_Variant_Required] RecordingPermissionedSessionHandle<"
                              "Select<...>>::select<I>() without arguments is not allowed.  "
                              "Choose select<I>(transport) for wire-based sessions or "
                              "select_local<I>() for in-memory channels.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename... Branches, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingPermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Offer<Branches...>,
                               RecordingPermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx>> {
    PermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Offer<Branches...>;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx>;
    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Offer<Branches...>,
                            RecordingPermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <std::size_t I>
        requires(I < sizeof...(Branches))
    [[nodiscard]] constexpr auto pick_local() && {
        log_->append_event(SessionEvent{
            .from_role = peer_role_,  // peer made the choice
            .to_role = self_role_,  // self learns the choice
            .op = SessionOp::Offer,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template pick_local<I>();
        return detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
    }

    template <std::size_t I>
    void pick() && = delete("[Wire_Variant_Required] RecordingPermissionedSessionHandle<"
                            "Offer<...>>::pick<I>() without arguments is not allowed.  Use "
                            "pick_local<I>() for in-memory channels or branch(transport, "
                            "handler) for transport-driven dispatch.");

    // The branch index is known only once the transport returns, so the event
    // is recorded from inside an interposed transport rather than before the
    // call.  The handler receives wrapped branch handles, which is what keeps
    // recording alive past the dispatch.
    template <typename Transport, typename Handler>
        requires std::is_invocable_r_v<std::size_t, Transport, Resource&>
    constexpr auto branch(Transport transport, Handler handler) && {
        auto* log_ptr = log_;
        auto self_role = self_role_;
        auto peer_role = peer_role_;

        auto recording_handler = [log_ptr, self_role, peer_role,
                                  handler = std::move(handler)](auto inner_branch_handle) mutable {
            using BranchHandle = decltype(inner_branch_handle);
            using BranchProto = typename BranchHandle::protocol;
            using BranchPS = typename BranchHandle::perm_set;
            using BranchResource = typename BranchHandle::resource_type;
            using BranchLoopCtx = typename BranchHandle::loop_ctx;
            RecordingPermissionedSessionHandle<BranchProto, BranchPS, BranchResource, BranchLoopCtx> wrapped_branch{
                detail::recording_session_construct_key{}, std::move(inner_branch_handle), *log_ptr, self_role,
                peer_role};
            return std::invoke(std::move(handler), std::move(wrapped_branch));
        };

        auto recording_transport = [log_ptr, self_role, peer_role,
                                    tx = std::move(transport)](Resource& r) mutable -> std::size_t {
            const std::size_t idx = std::invoke(tx, r);
            log_ptr->append_event(SessionEvent{
                .from_role = peer_role,
                .to_role = self_role,
                .op = SessionOp::Offer,
                .branch_index = static_cast<uint8_t>(idx),
            });
            return idx;
        };

        this->mark_consumed_();
        return std::move(inner_).branch(std::move(recording_transport), std::move(recording_handler));
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename ProtoBase, typename ProtoRollback, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, PS, Resource, LoopCtx>
    : public SessionHandleBase<
          CheckpointedSession<ProtoBase, ProtoRollback>,
          RecordingPermissionedSessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, PS, Resource, LoopCtx>> {
    PermissionedSessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = CheckpointedSession<ProtoBase, ProtoRollback>;
    using base_protocol = ProtoBase;
    using rollback_protocol = ProtoRollback;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<protocol, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<protocol, RecordingPermissionedSessionHandle<protocol, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    [[nodiscard]] constexpr auto
    base(CheckpointId checkpoint = {},
         ::crucible::ContentHash saved_state = {}) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        log_->append_event(SessionEvent::checkpoint_base(self_role_, peer_role_, checkpoint, saved_state));
        this->mark_consumed_();
        auto next = std::move(inner_).base();
        return detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr auto
    rollback(CheckpointId checkpoint = {},
             ::crucible::ContentHash saved_state = {}) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        log_->append_event(SessionEvent::checkpoint_rollback(self_role_, peer_role_, checkpoint, saved_state));
        this->mark_consumed_();
        auto next = std::move(inner_).rollback();
        return detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// The delegated handle's own permission set is checked against the declared
// one by the wrapped handle's delegate step.  This wrapper does not repeat
// that check.

template <typename InnerProto, typename InnerPS, typename K, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<Delegate<DelegatedSession<InnerProto, InnerPS>, K>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
                               RecordingPermissionedSessionHandle<Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
                                                                  PS, Resource, LoopCtx>> {
    using Protocol = Delegate<DelegatedSession<InnerProto, InnerPS>, K>;

    PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Protocol;
    using delegated_proto = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set = InnerPS;
    using continuation = K;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Protocol, RecordingPermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires(!is_stop_v<InnerProto> && std::is_invocable_v<Transport, Resource&, DelegatedResource &&>)
    [[nodiscard]] constexpr auto
    delegate(PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&& delegated,
             Transport transport, InnerPermSetHash inner_ps_hash = {}) && {
        log_->append_event(
            SessionEvent::delegate_handoff(self_role_, peer_role_, default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate(std::move(delegated), std::move(transport));
        return detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
    }

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx>
        requires(!is_stop_v<InnerProto>)
    [[nodiscard]] constexpr auto delegate_local(
        PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&& delegated,
        InnerPermSetHash inner_ps_hash = {}) && {
        log_->append_event(
            SessionEvent::delegate_handoff(self_role_, peer_role_, default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate_local(std::move(delegated));
        return detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// The event is recorded after the step rather than before it, unlike the
// hand-off side.  What the event names is the accepted endpoint, which does
// not exist until the step has produced it.

template <typename InnerProto, typename InnerPS, typename K, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<Accept<DelegatedSession<InnerProto, InnerPS>, K>, PS, Resource, LoopCtx>
    : public SessionHandleBase<
          Accept<DelegatedSession<InnerProto, InnerPS>, K>,
          RecordingPermissionedSessionHandle<Accept<DelegatedSession<InnerProto, InnerPS>, K>, PS, Resource, LoopCtx>> {
    using Protocol = Accept<DelegatedSession<InnerProto, InnerPS>, K>;

    PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Protocol;
    using delegated_proto = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set = InnerPS;
    using continuation = K;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>;

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Protocol, RecordingPermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <typename Transport, typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto accept(Transport transport, InnerPermSetHash inner_ps_hash = {}) && {
        auto [delegated_handle, next] = std::move(inner_).accept(std::move(transport));
        log_->append_event(
            SessionEvent::accept_handoff(self_role_, peer_role_, default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto accept_with(DelegatedResource delegated_res, InnerPermSetHash inner_ps_hash = {}) && {
        auto [delegated_handle, next] = std::move(inner_).accept_with(std::move(delegated_res));
        log_->append_event(
            SessionEvent::accept_handoff(self_role_, peer_role_, default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// The epoch thresholds appear only in the type here.  The wrapped handle
// requires the loop context to match them exactly at its own construction.

template <typename InnerProto, typename InnerPS, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>,
                                   PS, Resource, LoopCtx>
    : public SessionHandleBase<EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>,
                               RecordingPermissionedSessionHandle<
                                   EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>,
                                   PS, Resource, LoopCtx>> {
    using Protocol = EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>;

    PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Protocol;
    using delegated_proto = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set = InnerPS;
    using continuation = K;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>;
    static constexpr std::uint64_t min_epoch = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Protocol, RecordingPermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires(!is_stop_v<InnerProto> && std::is_invocable_v<Transport, Resource&, DelegatedResource &&>)
    [[nodiscard]] constexpr auto
    delegate(PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&& delegated,
             Transport transport, InnerPermSetHash inner_ps_hash = {}) && {
        log_->append_event(
            SessionEvent::delegate_handoff(self_role_, peer_role_, default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate(std::move(delegated), std::move(transport));
        return detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
    }

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx>
        requires(!is_stop_v<InnerProto>)
    [[nodiscard]] constexpr auto delegate_local(
        PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&& delegated,
        InnerPermSetHash inner_ps_hash = {}) && {
        log_->append_event(
            SessionEvent::delegate_handoff(self_role_, peer_role_, default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate_local(std::move(delegated));
        return detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// The acceptance side admits a context newer than the declared minimum, where
// the hand-off side demands exact equality.  A recipient that has moved ahead
// is still able to accept, and only a stale or unannotated one is refused.  As
// on the hand-off side, the wrapped handle enforces this at its own
// construction.

template <typename InnerProto, typename InnerPS, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
RecordingPermissionedSessionHandle<EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>, PS,
                                   Resource, LoopCtx>
    : public SessionHandleBase<EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>,
                               RecordingPermissionedSessionHandle<
                                   EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>, PS,
                                   Resource, LoopCtx>> {
    using Protocol = EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>;

    PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Protocol;
    using delegated_proto = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set = InnerPS;
    using continuation = K;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>;
    static constexpr std::uint64_t min_epoch = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;

    constexpr RecordingPermissionedSessionHandle(detail::recording_session_construct_key, inner_type inner,
                                                 SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Protocol, RecordingPermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingPermissionedSessionHandle(RecordingPermissionedSessionHandle&&) noexcept = default;
    constexpr RecordingPermissionedSessionHandle& operator=(RecordingPermissionedSessionHandle&&) noexcept = default;
    ~RecordingPermissionedSessionHandle() = default;

    template <typename Transport, typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto accept(Transport transport, InnerPermSetHash inner_ps_hash = {}) && {
        auto [delegated_handle, next] = std::move(inner_).accept(std::move(transport));
        log_->append_event(
            SessionEvent::accept_handoff(self_role_, peer_role_, default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto accept_with(DelegatedResource delegated_res, InnerPermSetHash inner_ps_hash = {}) && {
        auto [delegated_handle, next] = std::move(inner_).accept_with(std::move(delegated_res));
        log_->append_event(
            SessionEvent::accept_handoff(self_role_, peer_role_, default_proto_hash<InnerProto>, inner_ps_hash));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_permissioned_(std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// The requires clause is satisfied by construction: the parameter type is
// already a handle.  It stays because it is the grep target that makes every
// authorisation point findable.

template <typename Proto, typename PS, typename Resource, typename LoopCtx>
    requires ::crucible::safety::extract::IsSessionHandle<PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>>
[[nodiscard]] constexpr auto mint_recording_session(PermissionedSessionHandle<Proto, PS, Resource, LoopCtx> inner,
                                                    SessionEventLog& log, RoleTagId self, RoleTagId peer) noexcept {
    return RecordingPermissionedSessionHandle<Proto, PS, Resource, LoopCtx>{detail::recording_session_construct_key{},
                                                                            std::move(inner), log, self, peer};
}

}  // namespace crucible::safety::proto
