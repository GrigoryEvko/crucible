#pragma once

// The wrapper holds its inner handle by value rather than deriving from it.
// The inner consumer methods are rvalue-ref-only, and each step has to mark
// both the wrapper and the inner handle consumed, which inheritance cannot
// express.  Only the abandonment-checking base is inherited.
//
// The log is held by reference, not owned, so both sides of one session can
// write into the same log and produce one ordered record of the exchange.
//
// A plain handle records the event before invoking the user transport: the
// operation is already determined, and recording intent first keeps the trail
// honest if the transport fails.  A crash-watched handle cannot do that,
// because the watched peer may already be dead.  It records the normal
// operation only after the step succeeds, and records a Stop instead when the
// step fails, so replay observes the transition that actually happened.
//
// There is no specialisation for a loop or a continuation.  Both are resolved
// to a concrete protocol step before a handle of either kind exists.

#include <crucible/Platform.h>
#include <crucible/bridges/CrashTransport.h>
#include <crucible/safety/IsSessionHandle.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionEventLog.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

template <typename Proto, typename Resource, typename LoopCtx = void>
class RecordingSessionHandle;

template <typename Proto, typename Resource, typename PeerTag, CrashClass C = CrashClass::Abort,
          typename LoopCtx = void, typename PS = EmptyPermSet>
class RecordingCrashWatchedHandle;

namespace detail {

template <typename NextHandle>
[[nodiscard]] constexpr auto wrap_next_(NextHandle next, SessionEventLog& log, RoleTagId self_role,
                                        RoleTagId peer_role) noexcept {
    using NextProto = typename NextHandle::protocol;
    using NextResource = typename NextHandle::resource_type;
    using NextLoopCtx = typename NextHandle::loop_ctx;
    return RecordingSessionHandle<NextProto, NextResource, NextLoopCtx>{std::move(next), log, self_role, peer_role};
}

template <typename PeerTag, CrashClass C, typename NextHandle>
[[nodiscard]] constexpr auto wrap_recording_crash_next_(NextHandle next, SessionEventLog& log, RoleTagId self_role,
                                                        RoleTagId peer_role) noexcept {
    using NextProto = typename NextHandle::protocol;
    using NextResource = typename NextHandle::resource_type;
    using NextLoopCtx = typename NextHandle::loop_ctx;
    using NextPS = typename NextHandle::perm_set;
    return RecordingCrashWatchedHandle<NextProto, NextResource, PeerTag, C, NextLoopCtx, NextPS>{std::move(next), log,
                                                                                                 self_role, peer_role};
}

// A crash seen through the flag reaches no crash terminal, so the tier is not
// carried by the protocol at that point.  Callers pass the tolerated tier
// explicitly, otherwise replay cannot tell the recovery families apart.
constexpr void record_crash_stop_(
    SessionEventLog& log, RoleTagId self_role, RoleTagId peer_role,
    ::crucible::algebra::lattices::CrashClass crash_class = ::crucible::algebra::lattices::CrashClass::Abort) {
    log.append_event(SessionEvent::stop(self_role, peer_role, peer_role, StopReasonKind::PeerCrashed,
                                        RecoveryPathHash{}, crash_class));
}

}  // namespace detail

template <typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]]
RecordingCrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<End, RecordingCrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS>> {
    CrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = End;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    using inner_type = CrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS>;
    static constexpr CrashClass crash_class = C;

    constexpr RecordingCrashWatchedHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer_role,
                                          std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<End, RecordingCrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer_role} {}

    constexpr RecordingCrashWatchedHandle(RecordingCrashWatchedHandle&&) noexcept = default;
    constexpr RecordingCrashWatchedHandle& operator=(RecordingCrashWatchedHandle&&) noexcept = default;
    ~RecordingCrashWatchedHandle() = default;

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
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return inner_.crash_flag(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <CrashClass StopC, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]]
RecordingCrashWatchedHandle<Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Stop_g<StopC>,
                               RecordingCrashWatchedHandle<Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS>> {
    CrashWatchedHandle<Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Stop_g<StopC>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    using inner_type = CrashWatchedHandle<Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS>;
    static constexpr CrashClass crash_class = C;

    constexpr RecordingCrashWatchedHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer_role,
                                          std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Stop_g<StopC>,
                            RecordingCrashWatchedHandle<Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer_role} {}

    constexpr RecordingCrashWatchedHandle(RecordingCrashWatchedHandle&&) noexcept = default;
    constexpr RecordingCrashWatchedHandle& operator=(RecordingCrashWatchedHandle&&) noexcept = default;
    ~RecordingCrashWatchedHandle() = default;

    [[nodiscard]] constexpr Resource
    close(StopReasonKind reason = StopReasonKind::PeerCrashed,
          RecoveryPathHash recovery_path = {}) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        // The recorded tier is the terminal's own StopC, not the tolerated C
        // of the watch.  They differ, and only the terminal's tier names the
        // recovery family the peer actually entered.
        log_->append_event(SessionEvent::stop(self_role_, peer_role_, peer_role_, reason, recovery_path, StopC));
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return inner_.crash_flag(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename T, typename K, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]]
RecordingCrashWatchedHandle<Send<T, K>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Send<T, K>, RecordingCrashWatchedHandle<Send<T, K>, Resource, PeerTag, C, LoopCtx, PS>> {
    CrashWatchedHandle<Send<T, K>, Resource, PeerTag, C, LoopCtx, PS> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Send<T, K>;
    using message_type = T;
    using continuation = K;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    static constexpr CrashClass crash_class = C;
    using inner_type = CrashWatchedHandle<Send<T, K>, Resource, PeerTag, C, LoopCtx, PS>;

    constexpr RecordingCrashWatchedHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer_role,
                                          std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Send<T, K>, RecordingCrashWatchedHandle<Send<T, K>, Resource, PeerTag, C, LoopCtx, PS>>{
              loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer_role} {}

    constexpr RecordingCrashWatchedHandle(RecordingCrashWatchedHandle&&) noexcept = default;
    constexpr RecordingCrashWatchedHandle& operator=(RecordingCrashWatchedHandle&&) noexcept = default;
    ~RecordingCrashWatchedHandle() = default;

    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr auto send(T value, Transport transport) && {
        auto event = SessionEvent{
            .from_role = self_role_,
            .to_role = peer_role_,
            .payload_schema = default_schema_hash<T>,
            .payload_hash = default_payload_hash_fn<T>(value),
            .op = SessionOp::Send,
        };

        this->mark_consumed_();
        auto result = std::move(inner_).send(std::move(value), std::move(transport));

        using InnerNext = std::remove_cvref_t<decltype(*result)>;
        using Error = typename decltype(result)::error_type;
        using WrappedNext = decltype(detail::wrap_recording_crash_next_<PeerTag, C>(
            std::declval<InnerNext>(), std::declval<SessionEventLog&>(), std::declval<RoleTagId>(),
            std::declval<RoleTagId>()));
        using Out = std::expected<WrappedNext, Error>;

        if (!result) {
            detail::record_crash_stop_(*log_, self_role_, peer_role_, C);
            return Out{std::unexpected{std::move(result.error())}};
        }

        log_->append_event(event);
        return Out{detail::wrap_recording_crash_next_<PeerTag, C>(std::move(*result), *log_, self_role_, peer_role_)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return inner_.crash_flag(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename T, typename K, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]]
RecordingCrashWatchedHandle<Recv<T, K>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Recv<T, K>, RecordingCrashWatchedHandle<Recv<T, K>, Resource, PeerTag, C, LoopCtx, PS>> {
    CrashWatchedHandle<Recv<T, K>, Resource, PeerTag, C, LoopCtx, PS> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Recv<T, K>;
    using message_type = T;
    using continuation = K;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    static constexpr CrashClass crash_class = C;
    using inner_type = CrashWatchedHandle<Recv<T, K>, Resource, PeerTag, C, LoopCtx, PS>;

    constexpr RecordingCrashWatchedHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer_role,
                                          std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Recv<T, K>, RecordingCrashWatchedHandle<Recv<T, K>, Resource, PeerTag, C, LoopCtx, PS>>{
              loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer_role} {}

    constexpr RecordingCrashWatchedHandle(RecordingCrashWatchedHandle&&) noexcept = default;
    constexpr RecordingCrashWatchedHandle& operator=(RecordingCrashWatchedHandle&&) noexcept = default;
    ~RecordingCrashWatchedHandle() = default;

    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        this->mark_consumed_();
        auto result = std::move(inner_).recv(std::move(transport));

        using InnerPair = std::remove_cvref_t<decltype(*result)>;
        using InnerNext = std::remove_cvref_t<decltype(std::declval<InnerPair>().second)>;
        using Error = typename decltype(result)::error_type;
        using WrappedNext = decltype(detail::wrap_recording_crash_next_<PeerTag, C>(
            std::declval<InnerNext>(), std::declval<SessionEventLog&>(), std::declval<RoleTagId>(),
            std::declval<RoleTagId>()));
        using Out = std::expected<std::pair<T, WrappedNext>, Error>;

        if (!result) {
            detail::record_crash_stop_(*log_, self_role_, peer_role_, C);
            return Out{std::unexpected{std::move(result.error())}};
        }

        auto [value, next] = std::move(*result);
        log_->append_event(SessionEvent{
            .from_role = peer_role_,
            .to_role = self_role_,
            .payload_schema = default_schema_hash<T>,
            .payload_hash = default_payload_hash_fn<T>(value),
            .op = SessionOp::Recv,
        });
        return Out{std::pair{std::move(value), detail::wrap_recording_crash_next_<PeerTag, C>(std::move(next), *log_,
                                                                                              self_role_, peer_role_)}};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return inner_.crash_flag(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename... Branches, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]]
RecordingCrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Select<Branches...>,
                               RecordingCrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS>> {
    CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Select<Branches...>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    static constexpr CrashClass crash_class = C;
    using inner_type = CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS>;
    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr RecordingCrashWatchedHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer_role,
                                          std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Select<Branches...>,
                            RecordingCrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer_role} {}

    constexpr RecordingCrashWatchedHandle(RecordingCrashWatchedHandle&&) noexcept = default;
    constexpr RecordingCrashWatchedHandle& operator=(RecordingCrashWatchedHandle&&) noexcept = default;
    ~RecordingCrashWatchedHandle() = default;

    template <std::size_t I, typename Transport>
        requires(I < sizeof...(Branches)) && std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && {
        auto event = SessionEvent{
            .from_role = self_role_,
            .to_role = peer_role_,
            .op = SessionOp::Select,
            .branch_index = static_cast<uint8_t>(I),
        };

        this->mark_consumed_();
        auto result = std::move(inner_).template select<I>(std::move(transport));

        using InnerNext = std::remove_cvref_t<decltype(*result)>;
        using Error = typename decltype(result)::error_type;
        using WrappedNext = decltype(detail::wrap_recording_crash_next_<PeerTag, C>(
            std::declval<InnerNext>(), std::declval<SessionEventLog&>(), std::declval<RoleTagId>(),
            std::declval<RoleTagId>()));
        using Out = std::expected<WrappedNext, Error>;

        if (!result) {
            detail::record_crash_stop_(*log_, self_role_, peer_role_, C);
            return Out{std::unexpected{std::move(result.error())}};
        }

        log_->append_event(event);
        return Out{detail::wrap_recording_crash_next_<PeerTag, C>(std::move(*result), *log_, self_role_, peer_role_)};
    }

    template <std::size_t I>
        requires(I < sizeof...(Branches))
    [[nodiscard]] constexpr auto select_local() && {
        auto event = SessionEvent{
            .from_role = self_role_,
            .to_role = peer_role_,
            .op = SessionOp::Select,
            .branch_index = static_cast<uint8_t>(I),
        };

        this->mark_consumed_();
        auto result = std::move(inner_).template select_local<I>();

        using InnerNext = std::remove_cvref_t<decltype(*result)>;
        using Error = typename decltype(result)::error_type;
        using WrappedNext = decltype(detail::wrap_recording_crash_next_<PeerTag, C>(
            std::declval<InnerNext>(), std::declval<SessionEventLog&>(), std::declval<RoleTagId>(),
            std::declval<RoleTagId>()));
        using Out = std::expected<WrappedNext, Error>;

        if (!result) {
            detail::record_crash_stop_(*log_, self_role_, peer_role_, C);
            return Out{std::unexpected{std::move(result.error())}};
        }

        log_->append_event(event);
        return Out{detail::wrap_recording_crash_next_<PeerTag, C>(std::move(*result), *log_, self_role_, peer_role_)};
    }

    template <std::size_t I>
    void select() && = delete("[Wire_Variant_Required] RecordingCrashWatchedHandle<Select<...>>::"
                              "select<I>() without arguments is not available.  Use "
                              "`select<I>(transport)` for the wire path or `select_local<I>()` "
                              "for the in-memory variant.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return inner_.crash_flag(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename... Branches, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]]
RecordingCrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Offer<Branches...>,
                               RecordingCrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS>> {
    CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Offer<Branches...>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    static constexpr CrashClass crash_class = C;
    using inner_type = CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS>;
    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr RecordingCrashWatchedHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer_role,
                                          std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Offer<Branches...>,
                            RecordingCrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer_role} {}

    constexpr RecordingCrashWatchedHandle(RecordingCrashWatchedHandle&&) noexcept = default;
    constexpr RecordingCrashWatchedHandle& operator=(RecordingCrashWatchedHandle&&) noexcept = default;
    ~RecordingCrashWatchedHandle() = default;

    template <std::size_t I>
        requires(I < sizeof...(Branches))
    [[nodiscard]] constexpr auto pick_local() && {
        auto event = SessionEvent{
            .from_role = peer_role_,
            .to_role = self_role_,
            .op = SessionOp::Offer,
            .branch_index = static_cast<uint8_t>(I),
        };

        this->mark_consumed_();
        auto result = std::move(inner_).template pick_local<I>();

        using InnerNext = std::remove_cvref_t<decltype(*result)>;
        using Error = typename decltype(result)::error_type;
        using WrappedNext = decltype(detail::wrap_recording_crash_next_<PeerTag, C>(
            std::declval<InnerNext>(), std::declval<SessionEventLog&>(), std::declval<RoleTagId>(),
            std::declval<RoleTagId>()));
        using Out = std::expected<WrappedNext, Error>;

        if (!result) {
            detail::record_crash_stop_(*log_, self_role_, peer_role_, C);
            return Out{std::unexpected{std::move(result.error())}};
        }

        log_->append_event(event);
        return Out{detail::wrap_recording_crash_next_<PeerTag, C>(std::move(*result), *log_, self_role_, peer_role_)};
    }

    template <std::size_t I>
    void pick() && = delete("[Wire_Variant_Required] RecordingCrashWatchedHandle<Offer<...>>::"
                            "pick<I>() without arguments is not available.  Use "
                            "`pick_local<I>()` to advance without receiving a peer label.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return inner_.crash_flag(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<End, Resource, LoopCtx>
    : public SessionHandleBase<End, RecordingSessionHandle<End, Resource, LoopCtx>> {
    SessionHandle<End, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = End;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<End, Resource, LoopCtx>;

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<End, RecordingSessionHandle<End, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        log_->record_now(SessionEvent{
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

template <CrashClass C, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Stop_g<C>, Resource, LoopCtx>
    : public SessionHandleBase<Stop_g<C>, RecordingSessionHandle<Stop_g<C>, Resource, LoopCtx>> {
    SessionHandle<Stop_g<C>, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Stop_g<C>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<Stop_g<C>, Resource, LoopCtx>;
    static constexpr CrashClass crash_class = C;

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Stop_g<C>, RecordingSessionHandle<Stop_g<C>, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    [[nodiscard]] constexpr Resource
    close(StopReasonKind reason = StopReasonKind::PeerCrashed,
          RecoveryPathHash recovery_path = {}) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        log_->append_event(SessionEvent::stop(self_role_, peer_role_, peer_role_, reason, recovery_path, C));
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename ProtoBase, typename ProtoRollback, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, Resource, LoopCtx>
    : public SessionHandleBase<
          CheckpointedSession<ProtoBase, ProtoRollback>,
          RecordingSessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, Resource, LoopCtx>> {
    SessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = CheckpointedSession<ProtoBase, ProtoRollback>;
    using base_protocol = ProtoBase;
    using rollback_protocol = ProtoRollback;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<protocol, Resource, LoopCtx>;

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<protocol, RecordingSessionHandle<protocol, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    [[nodiscard]] constexpr auto
    base(CheckpointId checkpoint = {},
         ::crucible::ContentHash saved_state = {}) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        log_->append_event(SessionEvent::checkpoint_base(self_role_, peer_role_, checkpoint, saved_state));
        this->mark_consumed_();
        auto next = std::move(inner_).base();
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr auto
    rollback(CheckpointId checkpoint = {},
             ::crucible::ContentHash saved_state = {}) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        log_->append_event(SessionEvent::checkpoint_rollback(self_role_, peer_role_, checkpoint, saved_state));
        this->mark_consumed_();
        auto next = std::move(inner_).rollback();
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename T, typename K, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Delegate<T, K>, Resource, LoopCtx>
    : public SessionHandleBase<Delegate<T, K>, RecordingSessionHandle<Delegate<T, K>, Resource, LoopCtx>> {
    SessionHandle<Delegate<T, K>, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Delegate<T, K>;
    using delegated_proto = T;
    using continuation = K;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<Delegate<T, K>, Resource, LoopCtx>;

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Delegate<T, K>, RecordingSessionHandle<Delegate<T, K>, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    template <typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires(!is_stop_v<T> && std::is_invocable_v<Transport, Resource&, DelegatedResource &&>)
    [[nodiscard]] constexpr auto
    delegate(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&& delegated, Transport transport,
             InnerPermSetHash inner_perm_set =
                 {}) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, DelegatedResource&&>
                                 && std::is_nothrow_move_constructible_v<Resource>) {
        log_->append_event(
            SessionEvent::delegate_handoff(self_role_, peer_role_, default_proto_hash<T>, inner_perm_set));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate(std::move(delegated), std::move(transport));
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    template <typename DelegatedResource, typename DelegatedLoopCtx>
        requires(!is_stop_v<T>)
    [[nodiscard]] constexpr auto delegate_local(
        SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&& delegated,
        InnerPermSetHash inner_perm_set = {}) && noexcept(std::is_nothrow_move_constructible_v<Resource>
                                                          && std::is_nothrow_destructible_v<DelegatedResource>) {
        log_->append_event(
            SessionEvent::delegate_handoff(self_role_, peer_role_, default_proto_hash<T>, inner_perm_set));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate_local(std::move(delegated));
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename T, typename K, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Accept<T, K>, Resource, LoopCtx>
    : public SessionHandleBase<Accept<T, K>, RecordingSessionHandle<Accept<T, K>, Resource, LoopCtx>> {
    SessionHandle<Accept<T, K>, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Accept<T, K>;
    using delegated_proto = T;
    using continuation = K;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<Accept<T, K>, Resource, LoopCtx>;

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Accept<T, K>, RecordingSessionHandle<Accept<T, K>, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    template <typename Transport, typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto accept(Transport transport, InnerPermSetHash inner_perm_set = {}) && noexcept(
        std::is_nothrow_invocable_v<Transport, Resource&> && std::is_nothrow_move_constructible_v<Resource>
        && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        auto [delegated_handle, next] = std::move(inner_).accept(std::move(transport));
        log_->append_event(SessionEvent::accept_handoff(self_role_, peer_role_, default_proto_hash<T>, inner_perm_set));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto
    accept_with(DelegatedResource delegated_res, InnerPermSetHash inner_perm_set = {}) && noexcept(
        std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        auto [delegated_handle, next] = std::move(inner_).accept_with(std::move(delegated_res));
        log_->append_event(SessionEvent::accept_handoff(self_role_, peer_role_, default_proto_hash<T>, inner_perm_set));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename Resource,
          typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<EpochedDelegate<T, K, MinEpoch, MinGeneration>, Resource, LoopCtx>
    : public SessionHandleBase<
          EpochedDelegate<T, K, MinEpoch, MinGeneration>,
          RecordingSessionHandle<EpochedDelegate<T, K, MinEpoch, MinGeneration>, Resource, LoopCtx>> {
    using Protocol = EpochedDelegate<T, K, MinEpoch, MinGeneration>;

    SessionHandle<Protocol, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Protocol;
    using delegated_proto = T;
    using continuation = K;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<Protocol, Resource, LoopCtx>;
    static constexpr std::uint64_t min_epoch = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Protocol, RecordingSessionHandle<Protocol, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    template <typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires(!is_stop_v<T> && std::is_invocable_v<Transport, Resource&, DelegatedResource &&>)
    [[nodiscard]] constexpr auto
    delegate(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&& delegated, Transport transport,
             InnerPermSetHash inner_perm_set =
                 {}) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, DelegatedResource&&>
                                 && std::is_nothrow_move_constructible_v<Resource>) {
        // The epoch and generation thresholds live only in the type.  The
        // plain handoff event has no field for them, so a dedicated event
        // kind is the only way they reach the log.
        log_->append_event(SessionEvent::epoched_delegate_handoff(self_role_, peer_role_, default_proto_hash<T>,
                                                                  MinEpoch, MinGeneration, inner_perm_set));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate(std::move(delegated), std::move(transport));
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    template <typename DelegatedResource, typename DelegatedLoopCtx>
        requires(!is_stop_v<T>)
    [[nodiscard]] constexpr auto delegate_local(
        SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&& delegated,
        InnerPermSetHash inner_perm_set = {}) && noexcept(std::is_nothrow_move_constructible_v<Resource>
                                                          && std::is_nothrow_destructible_v<DelegatedResource>) {
        log_->append_event(SessionEvent::epoched_delegate_handoff(self_role_, peer_role_, default_proto_hash<T>,
                                                                  MinEpoch, MinGeneration, inner_perm_set));
        this->mark_consumed_();
        auto next = std::move(inner_).delegate_local(std::move(delegated));
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename Resource,
          typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<EpochedAccept<T, K, MinEpoch, MinGeneration>, Resource, LoopCtx>
    : public SessionHandleBase<
          EpochedAccept<T, K, MinEpoch, MinGeneration>,
          RecordingSessionHandle<EpochedAccept<T, K, MinEpoch, MinGeneration>, Resource, LoopCtx>> {
    using Protocol = EpochedAccept<T, K, MinEpoch, MinGeneration>;

    SessionHandle<Protocol, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Protocol;
    using delegated_proto = T;
    using continuation = K;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<Protocol, Resource, LoopCtx>;
    static constexpr std::uint64_t min_epoch = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Protocol, RecordingSessionHandle<Protocol, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    template <typename Transport, typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto accept(Transport transport, InnerPermSetHash inner_perm_set = {}) && noexcept(
        std::is_nothrow_invocable_v<Transport, Resource&> && std::is_nothrow_move_constructible_v<Resource>
        && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        auto [delegated_handle, next] = std::move(inner_).accept(std::move(transport));
        // The epoch and generation thresholds live only in the type.  The
        // plain handoff event has no field for them, so a dedicated event
        // kind is the only way they reach the log.
        log_->append_event(SessionEvent::epoched_accept_handoff(self_role_, peer_role_, default_proto_hash<T>, MinEpoch,
                                                                MinGeneration, inner_perm_set));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto
    accept_with(DelegatedResource delegated_res, InnerPermSetHash inner_perm_set = {}) && noexcept(
        std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        auto [delegated_handle, next] = std::move(inner_).accept_with(std::move(delegated_res));
        log_->append_event(SessionEvent::epoched_accept_handoff(self_role_, peer_role_, default_proto_hash<T>, MinEpoch,
                                                                MinGeneration, inner_perm_set));
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(delegated_handle), std::move(wrapped_next)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename T, typename R, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Send<T, R>, Resource, LoopCtx>
    : public SessionHandleBase<Send<T, R>, RecordingSessionHandle<Send<T, R>, Resource, LoopCtx>> {
    SessionHandle<Send<T, R>, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Send<T, R>;
    using message_type = T;
    using continuation = R;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<Send<T, R>, Resource, LoopCtx>;

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Send<T, R>, RecordingSessionHandle<Send<T, R>, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr auto send(T value, Transport transport) && {
        log_->record_now(SessionEvent{
            .from_role = self_role_,
            .to_role = peer_role_,
            .payload_schema = default_schema_hash<T>,
            .payload_hash = default_payload_hash_fn<T>(value),
            .op = SessionOp::Send,
        });
        this->mark_consumed_();
        auto next = std::move(inner_).send(std::move(value), std::move(transport));
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename T, typename R, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Recv<T, R>, Resource, LoopCtx>
    : public SessionHandleBase<Recv<T, R>, RecordingSessionHandle<Recv<T, R>, Resource, LoopCtx>> {
    SessionHandle<Recv<T, R>, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Recv<T, R>;
    using message_type = T;
    using continuation = R;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<Recv<T, R>, Resource, LoopCtx>;

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Recv<T, R>, RecordingSessionHandle<Recv<T, R>, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    // A receive records after the transport call, unlike a send, because the
    // payload hash has to cover the value that actually arrived.
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        auto [value, next] = std::move(inner_).recv(std::move(transport));
        log_->record_now(SessionEvent{
            .from_role = peer_role_,
            .to_role = self_role_,
            .payload_schema = default_schema_hash<T>,
            .payload_hash = default_payload_hash_fn<T>(value),
            .op = SessionOp::Recv,
        });
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(value), std::move(wrapped_next)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename... Branches, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Select<Branches...>, Resource, LoopCtx>
    : public SessionHandleBase<Select<Branches...>, RecordingSessionHandle<Select<Branches...>, Resource, LoopCtx>> {
    SessionHandle<Select<Branches...>, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Select<Branches...>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<Select<Branches...>, Resource, LoopCtx>;
    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Select<Branches...>, RecordingSessionHandle<Select<Branches...>, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    template <std::size_t I, typename Transport>
        requires(I < sizeof...(Branches)) && std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && {
        log_->record_now(SessionEvent{
            .from_role = self_role_,
            .to_role = peer_role_,
            .op = SessionOp::Select,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template select<I>(std::move(transport));
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    // The log does not distinguish the wire choice from the local one.  Both
    // fix the same protocol shape, and that shape is what replay needs.  The
    // absence of a wire step is recoverable from the method name alone.
    template <std::size_t I>
        requires(I < sizeof...(Branches))
    [[nodiscard]] constexpr auto select_local() && {
        log_->record_now(SessionEvent{
            .from_role = self_role_,
            .to_role = peer_role_,
            .op = SessionOp::Select,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template select_local<I>();
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    template <std::size_t I>
    void select() && = delete("[Wire_Variant_Required] RecordingSessionHandle<Select<...>>::"
                              "select<I>() without arguments is not available.  "
                              "Use `select<I>(transport)` for the wire path or "
                              "`select_local<I>()` for the in-memory variant.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

template <typename... Branches, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Offer<Branches...>, Resource, LoopCtx>
    : public SessionHandleBase<Offer<Branches...>, RecordingSessionHandle<Offer<Branches...>, Resource, LoopCtx>> {
    SessionHandle<Offer<Branches...>, Resource, LoopCtx> inner_;
    SessionEventLog* log_ = nullptr;
    RoleTagId self_role_{};
    RoleTagId peer_role_{};

public:
    using protocol = Offer<Branches...>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_type = SessionHandle<Offer<Branches...>, Resource, LoopCtx>;
    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr RecordingSessionHandle(inner_type inner, SessionEventLog& log, RoleTagId self, RoleTagId peer,
                                     std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Offer<Branches...>, RecordingSessionHandle<Offer<Branches...>, Resource, LoopCtx>>{loc},
          inner_{std::move(inner)},
          log_{&log},
          self_role_{self},
          peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    // The caller already knows the branch, so no label arrives from the peer.
    // The log records the transition anyway: replay needs the branch taken,
    // not the means by which it was learned.
    template <std::size_t I>
        requires(I < sizeof...(Branches))
    [[nodiscard]] constexpr auto pick_local() && {
        log_->record_now(SessionEvent{
            .from_role = peer_role_,  // peer made the choice
            .to_role = self_role_,  // self learns the choice
            .op = SessionOp::Offer,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template pick_local<I>();
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    template <std::size_t I>
    void pick() && = delete("[Wire_Variant_Required] RecordingSessionHandle<Offer<...>>::"
                            "pick<I>() without arguments is not available.  "
                            "Use `pick_local<I>()` to advance without receiving a peer "
                            "label, or call the peer-receiving variant when one is "
                            "available.");

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
            using BranchResource = typename BranchHandle::resource_type;
            using BranchLoopCtx = typename BranchHandle::loop_ctx;
            RecordingSessionHandle<BranchProto, BranchResource, BranchLoopCtx> wrapped_branch{
                std::move(inner_branch_handle), *log_ptr, self_role, peer_role};
            return std::invoke(std::move(handler), std::move(wrapped_branch));
        };

        auto recording_transport = [log_ptr, self_role, peer_role,
                                    tx = std::move(transport)](Resource& r) mutable -> std::size_t {
            const std::size_t idx = std::invoke(tx, r);
            log_ptr->record_now(SessionEvent{
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

// The requires clause on each factory is satisfied by construction: the
// parameter type already is a handle.  It stays because it is the grep target
// that makes every cross-tier authorisation point findable in one pass, and
// the parameter match alone leaves the gate invisible to an audit.

template <typename Proto, typename Resource, typename LoopCtx>
    requires ::crucible::safety::extract::IsSessionHandle<SessionHandle<Proto, Resource, LoopCtx>>
[[nodiscard]] constexpr auto mint_recording_session(SessionHandle<Proto, Resource, LoopCtx> inner, SessionEventLog& log,
                                                    RoleTagId self, RoleTagId peer) noexcept {
    return RecordingSessionHandle<Proto, Resource, LoopCtx>{std::move(inner), log, self, peer};
}

template <typename Proto, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
    requires ::crucible::safety::extract::IsSessionHandle<CrashWatchedHandle<Proto, Resource, PeerTag, C, LoopCtx, PS>>
[[nodiscard]] constexpr auto mint_recording_session(CrashWatchedHandle<Proto, Resource, PeerTag, C, LoopCtx, PS> inner,
                                                    SessionEventLog& log, RoleTagId self, RoleTagId peer) noexcept {
    return RecordingCrashWatchedHandle<Proto, Resource, PeerTag, C, LoopCtx, PS>{std::move(inner), log, self, peer};
}

}  // namespace crucible::safety::proto
