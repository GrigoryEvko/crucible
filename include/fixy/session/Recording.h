#pragma once

// Recording: one decorator that writes each step of a session to a
// SessionEventLog (fixy/session/EventLog.h).
//
// Recorded<Inner> wraps a plain handle, a crash-watched handle
// (fixy/session/CrashTransport.h) or a checkpoint handle
// (fixy/session/Checkpoint.h).  Each operation forwards to the inner
// handle, records what happened, and wraps the successor.
//
// ── Order of the decorators ─────────────────────────────────────────
//
// Recording is the outer decorator.  The crash transport decides
// whether a message reaches the wire, so only a recorder outside it can
// see the decision: it records a send or a select to a crashed peer as
// LostToCrashedPeer, and it records the crash branch as a Stop.  The
// other order has no constructor: mint_crash_session and
// mint_checkpoint_session build their handle from a Resource, and
// nothing wraps an existing handle in either of them.
//
// ── What is recorded ────────────────────────────────────────────────
//
//   send      Send, with the fate the transport met.  A payload of a
//             delegated session is recorded as Delegate instead, with
//             the hash of the delegated protocol and of its permission
//             set.
//   recv      Recv, or Accept for a delegated session.  The crash
//             record of a crash branch is recorded as Stop, with the
//             cause the detector saw.
//   select    Select, with the fate.  A Commit, Roll or Abort label is
//             also recorded as its checkpoint kind, Active.
//   branch    Offer, with the branch taken.  A Commit, Roll or Abort
//             label is also recorded as its checkpoint kind, Passive.
//   close     Close.
//   detach    Detach, with the reason kind of the tag.
//   crash     Stop, LocalAbort, with the cause.
//
// The kind of a hand-off comes from the payload type in one place,
// event_for_send and event_for_recv below, so a delegation cannot be
// recorded as a plain send.  The ported permissioned recorder recorded
// its epoched hand-off as a plain Delegate and lost both thresholds
// (crucible/bridges/RecordingPermissionedSessionHandle.h).  The new tree
// has no epoched delegation payload yet.  When one lands, it must join
// those two functions, or the recorder writes it as a plain Send.

#include <fixy/session/Checkpoint.h>
#include <fixy/session/CrashTransport.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Payload.h>

#include <foundation/reflect/Hash.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::session {

template <typename Inner>
class Recorded;

namespace detail::recording {

template <typename H>
struct crash_watched_shape : std::false_type {};
template <typename H, typename Self, typename Peer, typename Reliable>
struct crash_watched_shape<CrashWatched<H, Self, Peer, Reliable>> : std::true_type {};

template <typename H>
struct checkpoint_shape : std::false_type {};
template <typename Inner, typename Head, typename Loop, typename Frame>
struct checkpoint_shape<CheckpointHandle<Inner, Head, Loop, Frame>> : std::true_type {};

template <typename R>
struct crash_send_shape : std::false_type {};
template <typename Next, typename T>
struct crash_send_shape<CrashSend<Next, T>> : std::true_type {};

// The payload families a recorder knows.  A delegated session is a
// hand-off.  Every other payload that Payload.h accepts is a message.
template <typename T>
struct delegation_shape : std::false_type {};
template <typename InnerProto, typename InnerPS>
struct delegation_shape<DelegatedSession<InnerProto, InnerPS>> : std::true_type {
    using protocol = InnerProto;
    using perm_set = InnerPS;
};

template <typename T>
[[nodiscard]] constexpr SessionEvent event_for_send(RoleTagId self, RoleTagId peer, DeliveryFate fate) noexcept {
    if constexpr (delegation_shape<T>::value) {
        return SessionEvent::delegate_handoff(
            self, peer, default_proto_hash<typename delegation_shape<T>::protocol>,
            InnerPermSetHash{::foundation::reflect::stable_type_id<typename delegation_shape<T>::perm_set>});
    } else {
        return SessionEvent::send(self, peer, default_schema_hash<T>, PayloadHash{}, fate);
    }
}

template <typename T>
[[nodiscard]] constexpr SessionEvent event_for_recv(RoleTagId self, RoleTagId peer) noexcept {
    if constexpr (delegation_shape<T>::value) {
        return SessionEvent::accept_handoff(
            self, peer, default_proto_hash<typename delegation_shape<T>::protocol>,
            InnerPermSetHash{::foundation::reflect::stable_type_id<typename delegation_shape<T>::perm_set>});
    } else {
        return SessionEvent::recv(self, peer, default_schema_hash<T>);
    }
}

template <typename Reason>
[[nodiscard]] constexpr DetachReasonKind detach_kind() noexcept {
    if constexpr (std::is_same_v<Reason, detach_reason::InfiniteLoopProtocol>) return DetachReasonKind::InfiniteLoopProtocol;
    else if constexpr (std::is_same_v<Reason, detach_reason::TransportClosedOutOfBand>)
        return DetachReasonKind::TransportClosedOutOfBand;
    else if constexpr (std::is_same_v<Reason, detach_reason::TestInstrumentation>)
        return DetachReasonKind::TestInstrumentation;
    else if constexpr (std::is_same_v<Reason, detach_reason::AsyncCancellation>)
        return DetachReasonKind::AsyncCancellation;
    else if constexpr (std::is_same_v<Reason, detach_reason::OwnerLifetimeBoundEarlyExit>)
        return DetachReasonKind::OwnerLifetimeBoundEarlyExit;
    else
        return DetachReasonKind::Unknown;
}

// The checkpoint kind of a branch, or no event.
template <typename Branch>
[[nodiscard]] constexpr std::optional<SessionEvent> checkpoint_event(RoleTagId self, RoleTagId peer,
                                                                     CheckpointRole role) noexcept {
    if constexpr (std::is_same_v<Branch, Roll>) {
        return SessionEvent::checkpoint_roll(self, peer, role);
    } else if constexpr (std::is_same_v<Branch, Abort>) {
        return SessionEvent::checkpoint_abort(self, peer, role);
    } else if constexpr (is_checkpoint_primitive<Branch>::value) {
        return SessionEvent::checkpoint_commit(self, peer, role, default_proto_hash<typename Branch::next>);
    } else {
        return std::nullopt;
    }
}

template <typename Inner>
[[nodiscard]] constexpr auto make_recorded(Inner inner, SessionEventLog& log, RoleTagId self, RoleTagId peer) noexcept
    -> Recorded<Inner>;

}  // namespace detail::recording

// What a recorder can wrap: a handle that names its protocol and its
// Resource.
template <typename H>
concept RecordableHandle = requires {
    typename H::protocol;
    typename H::resource_type;
} && !std::is_reference_v<H>;

template <typename Inner>
class [[nodiscard]] Recorded {
    Inner inner_;
    SessionEventLog* log_;
    RoleTagId self_;
    RoleTagId peer_;

    template <typename>
    friend class Recorded;

    template <typename F>
    friend constexpr auto detail::recording::make_recorded(F, SessionEventLog&, RoleTagId, RoleTagId) noexcept
        -> Recorded<F>;

    constexpr Recorded(Inner inner, SessionEventLog& log, RoleTagId self, RoleTagId peer) noexcept
        : inner_{std::move(inner)}, log_{&log}, self_{self}, peer_{peer} {}

    template <typename Next>
    [[nodiscard]] constexpr Recorded<Next> wrap_(Next next) const noexcept {
        return Recorded<Next>{std::move(next), *log_, self_, peer_};
    }

    void record_(SessionEvent event) const { log_->record_now(event); }

public:
    using inner_type = Inner;
    using protocol = typename Inner::protocol;
    using resource_type = typename Inner::resource_type;

    constexpr Recorded(Recorded&&) noexcept = default;
    constexpr Recorded& operator=(Recorded&&) noexcept = default;
    Recorded(const Recorded&) = delete("a recorded handle is linear, like the handle it wraps");
    Recorded& operator=(const Recorded&) = delete("a recorded handle is linear, like the handle it wraps");
    ~Recorded() = default;

    [[nodiscard]] SessionEventLog& event_log() const noexcept { return *log_; }

    template <typename T, typename Transport>
        requires is_send_v<protocol> && requires(Inner&& inner, T value, Transport transport) {
            std::move(inner).send(std::move(value), std::move(transport));
        }
    [[nodiscard]] constexpr auto send(T value, Transport transport) && {
        using Message = typename protocol::message_type;
        constexpr bool is_nothrow = std::is_nothrow_invocable_v<Transport, resource_type&, Message&&>;
        bool is_delivered = false;
        auto marked = [&transport, &is_delivered](resource_type& resource, Message&& payload) noexcept(is_nothrow) {
            is_delivered = true;
            std::invoke(transport, resource, std::move(payload));
        };
        auto result = std::move(inner_).send(std::move(value), marked);
        record_(detail::recording::event_for_send<Message>(
            self_, peer_, is_delivered ? DeliveryFate::Delivered : DeliveryFate::LostToCrashedPeer));
        if constexpr (detail::recording::crash_send_shape<decltype(result)>::value) {
            using Wrapped = decltype(wrap_(std::move(result.next)));
            return CrashSend<Wrapped, Message>{wrap_(std::move(result.next)), std::move(result.undelivered)};
        } else {
            return wrap_(std::move(result));
        }
    }

    template <typename Transport>
        requires is_recv_v<protocol> && requires(Inner&& inner, Transport transport) {
            std::move(inner).recv(std::move(transport));
        }
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        auto [value, next] = std::move(inner_).recv(std::move(transport));
        record_(detail::recording::event_for_recv<typename protocol::message_type>(self_, peer_));
        return std::pair{std::move(value), wrap_(std::move(next))};
    }

    // The crash record of a crash branch.
    template <typename P = protocol>
        requires is_recv_v<P> && requires(Inner&& inner) { std::move(inner).recv(); }
    [[nodiscard]] constexpr auto recv() && {
        auto [record, next] = std::move(inner_).recv();
        record_(SessionEvent::stop(self_, peer_, peer_, StopReasonKind::PeerCrashed, record.cause));
        return std::pair{record, wrap_(std::move(next))};
    }

    template <std::size_t I, typename Transport>
        requires is_select_v<protocol> && std::is_invocable_v<Transport, resource_type&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && {
        constexpr bool is_nothrow = std::is_nothrow_invocable_v<Transport, resource_type&, std::size_t>;
        bool is_delivered = false;
        auto marked = [&transport, &is_delivered](resource_type& resource, std::size_t label) noexcept(is_nothrow) {
            is_delivered = true;
            std::invoke(transport, resource, label);
        };
        auto next = std::move(inner_).template select<I>(marked);
        record_(SessionEvent::select(self_, peer_, static_cast<std::uint8_t>(I),
                                     is_delivered ? DeliveryFate::Delivered : DeliveryFate::LostToCrashedPeer));
        using Branch = std::tuple_element_t<I, typename protocol::branches_tuple>;
        if (const auto event = detail::recording::checkpoint_event<Branch>(self_, peer_, CheckpointRole::Active))
            record_(*event);
        return wrap_(std::move(next));
    }

    // The label reader is whatever the inner handle takes: a transport
    // for a plain or checkpoint handle, a poll for a crash-watched one.
    template <typename Reader, typename Handler>
        requires is_offer_v<protocol>
    constexpr auto branch(Reader reader, Handler handler) && {
        using Branches = typename protocol::branches_tuple;
        constexpr std::size_t count = std::tuple_size_v<Branches>;
        constexpr std::size_t no_label = count;
        constexpr bool is_nothrow = std::is_nothrow_invocable_v<Reader, resource_type&>;
        std::size_t label = no_label;
        auto marked = [&reader, &label](resource_type& resource) noexcept(is_nothrow) {
            auto read = std::invoke(reader, resource);
            if constexpr (std::is_same_v<decltype(read), std::size_t>) {
                label = read;
            } else if (read) {
                label = *read;
            }
            return read;
        };
        return std::move(inner_).branch(marked, [this, &handler, &label](auto next) {
            // No label was read only when a crash-watched handle took the
            // crash branch.
            std::size_t taken = label;
            if constexpr (detail::recording::crash_watched_shape<Inner>::value) {
                using Head = typename decltype(next)::protocol;
                if constexpr (is_crash_branch_v<Head>) taken = crash_branch_index_v<protocol, typename Inner::peer_role>;
            }
            record_(SessionEvent::offer(self_, peer_, static_cast<std::uint8_t>(taken)));
            record_passive_checkpoint_<Branches>(taken, std::make_index_sequence<count>{});
            return std::invoke(handler, wrap_(std::move(next)));
        });
    }

    template <typename P = protocol>
        requires is_terminal_state_v<P> && requires(Inner&& inner) { std::move(inner).close(); }
    [[nodiscard]] constexpr resource_type close() && {
        record_(SessionEvent::close(self_, peer_));
        return std::move(inner_).close();
    }

    template <typename Reason>
        requires DetachReason<Reason>
    void detach(Reason reason) && {
        record_(SessionEvent::detach(self_, peer_, detail::recording::detach_kind<Reason>(), default_schema_hash<Reason>));
        std::move(inner_).detach(reason);
    }

    template <typename P = protocol>
        requires(!is_terminal_state_v<P>)
                && requires(Inner&& inner, CrashCause cause, PeerCrashCell& cell) { std::move(inner).crash(cause, cell); }
    [[nodiscard]] resource_type crash(CrashCause cause, PeerCrashCell& announce) && {
        record_(SessionEvent::stop(self_, peer_, self_, StopReasonKind::LocalAbort, cause));
        return std::move(inner_).crash(cause, announce);
    }

    [[nodiscard]] constexpr resource_type& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const resource_type& resource() const& noexcept { return inner_.resource(); }

private:
    template <typename Branches, std::size_t... Is>
    void record_passive_checkpoint_(std::size_t taken, std::index_sequence<Is...>) const {
        (
            [&] {
                using Branch = std::tuple_element_t<Is, Branches>;
                if (taken == Is) {
                    if (const auto event = detail::recording::checkpoint_event<Branch>(self_, peer_, CheckpointRole::Passive))
                        record_(*event);
                }
            }(),
            ...);
    }
};

namespace detail::recording {

template <typename Inner>
[[nodiscard]] constexpr auto make_recorded(Inner inner, SessionEventLog& log, RoleTagId self, RoleTagId peer) noexcept
    -> Recorded<Inner> {
    return Recorded<Inner>{std::move(inner), log, self, peer};
}

}  // namespace detail::recording

// ── The mint ─────────────────────────────────────────────────────────

template <typename H>
    requires RecordableHandle<H>
[[nodiscard]] constexpr auto mint_recorded_session(H handle, SessionEventLog& log, RoleTagId self,
                                                   RoleTagId peer) noexcept {
    return detail::recording::make_recorded(std::move(handle), log, self, peer);
}

}  // namespace fixy::session
