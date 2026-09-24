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
//
// A Select and an Offer record the index of the branch in the local
// protocol, and in a keyed choice the label word of the branch too.  The
// word is the one on the wire, so the recorder reads it from the
// protocol of the plain handle at the bottom of the decorators.
// replayed_branch checks one recorded event against a choice, and it
// refuses an event of a keyed choice that carries no label word.
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
struct is_crash_watched_shape : std::false_type {};
template <typename H, typename Self, typename Peer, typename Reliable, typename Position>
struct is_crash_watched_shape<CrashWatched<H, Self, Peer, Reliable, Position>> : std::true_type {};

template <typename H>
struct is_checkpoint_shape : std::false_type {};
template <typename Inner, typename Head, typename Loop, typename Frame>
struct is_checkpoint_shape<CheckpointHandle<Inner, Head, Loop, Frame>> : std::true_type {};

// The protocol of the plain handle under the decorators, whose wire words
// the transport carries.  A handle of an unknown shape has no entry, so
// the build stops instead of reading a word against the wrong choice.
template <typename H>
struct wire_protocol;
template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
struct wire_protocol<SessionHandle<Proto, Resource, LoopCtx, Policy, PS>> {
    using type = Proto;
};
template <typename H, typename Self, typename Peer, typename Reliable, typename Position>
struct wire_protocol<CrashWatched<H, Self, Peer, Reliable, Position>> : wire_protocol<H> {};
template <typename Inner, typename Head, typename Loop, typename Frame>
struct wire_protocol<CheckpointHandle<Inner, Head, Loop, Frame>> : wire_protocol<Inner> {};

template <typename H>
using wire_protocol_t = typename wire_protocol<H>::type;

// The event lane of a branch index holds one byte.
inline constexpr std::size_t recordable_branch_limit = 256;

// The label word that an event records for branch I of Choice: the word
// of a keyed choice, and zero otherwise.
template <typename Choice, std::size_t I>
[[nodiscard]] consteval LabelWord label_word_for_select() noexcept {
    if constexpr (is_keyed_choice_v<Choice>) {
        return LabelWord{branch_wire_word_v<Choice, I>};
    } else {
        return LabelWord{};
    }
}

template <typename R>
struct is_crash_send_shape : std::false_type {};
template <typename Next, typename T>
struct is_crash_send_shape<CrashSend<Next, T>> : std::true_type {};

// The payload families a recorder knows.  A delegated session is a
// hand-off.  Every other payload that Payload.h accepts is a message.
template <typename T>
struct is_delegation_shape : std::false_type {};
template <typename InnerProto, typename Resource, typename Policy, typename InnerPS>
struct is_delegation_shape<DelegatedSession<InnerProto, Resource, Policy, InnerPS>> : std::true_type {
    using protocol = InnerProto;
    using perm_set = InnerPS;
};

template <typename T>
[[nodiscard]] constexpr SessionEvent event_for_send(RoleTagId self, RoleTagId peer, DeliveryFate fate) noexcept {
    if constexpr (is_delegation_shape<T>::value) {
        return SessionEvent::delegate_handoff(
            self, peer, default_proto_hash<typename is_delegation_shape<T>::protocol>,
            InnerPermSetHash{::foundation::reflect::stable_type_id<typename is_delegation_shape<T>::perm_set>});
    } else {
        return SessionEvent::send(self, peer, default_schema_hash<T>, PayloadHash{}, fate);
    }
}

template <typename T>
[[nodiscard]] constexpr SessionEvent event_for_recv(RoleTagId self, RoleTagId peer) noexcept {
    if constexpr (is_delegation_shape<T>::value) {
        return SessionEvent::accept_handoff(
            self, peer, default_proto_hash<typename is_delegation_shape<T>::protocol>,
            InnerPermSetHash{::foundation::reflect::stable_type_id<typename is_delegation_shape<T>::perm_set>});
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

// The operations of the inner handle that the recorder forwards with no
// transport.  Each such member of Recorded exists only when the inner
// handle has it.  A member that takes a transport exists when the
// transport has a shape of fixy/session/Handle.h, and a shape that the
// inner handle refuses reaches the refusal of the inner handle.
template <typename Inner>
concept inner_can_detect_crash = requires(Inner&& inner) { std::move(inner).recv(); };

template <typename Inner>
concept inner_can_close = requires(Inner&& inner) { std::move(inner).close(); };

template <typename Inner>
concept inner_can_crash = requires(Inner&& inner, CrashCause cause, CrashReporter&& announce) {
    std::move(inner).crash(cause, std::move(announce));
};

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

    // The recorder hands the inner handle a write of the same shape as the
    // transport, which notes when the transport took the payload.  A payload
    // that no transport took, because the peer had crashed, is recorded as
    // lost to the crashed peer.  A write of a shape that the inner handle
    // refuses reaches the refusal of the inner handle.
    template <typename T, typename Transport>
        requires is_send_v<protocol> && (!is_keyed_step_v<protocol>)
                 && WriteTransport<Transport, resource_type, typename protocol::message_type>
    [[nodiscard]] constexpr auto send(T value, Transport transport) && {
        using Message = typename protocol::message_type;
        bool is_delivered = false;
        auto marked =
            detail::observed_write<Message, resource_type>(transport, [&is_delivered]() noexcept { is_delivered = true; });
        auto result = std::move(inner_).send(std::move(value), marked);
        record_(detail::recording::event_for_send<Message>(
            self_, peer_, is_delivered ? DeliveryFate::Delivered : DeliveryFate::LostToCrashedPeer));
        if constexpr (detail::recording::is_crash_send_shape<decltype(result)>::value) {
            using Wrapped = decltype(wrap_(std::move(result.next)));
            return CrashSend<Wrapped, Message>{wrap_(std::move(result.next)), std::move(result.undelivered)};
        } else {
            return wrap_(std::move(result));
        }
    }

    template <typename T, typename Transport>
        requires is_send_v<protocol> && (!is_keyed_step_v<protocol>)
                 && (!WriteTransport<Transport, resource_type, typename protocol::message_type>)
    void send(T, Transport) && = delete("[Transport_Shape] a write either tries, as bool(Resource&, T&), and returns "
                                        "false with the value unchanged while it has no room, or declares its wait, "
                                        "as void(Resource&, T&&, fixy::session::watch::wait_scope&).  A write of "
                                        "another shape can wait where the watch of fixy/session/Watch.h does not see "
                                        "it.");

    template <typename Transport>
        requires is_recv_v<protocol> && (!is_keyed_step_v<protocol>)
                 && ReadTransport<Transport, resource_type, typename protocol::message_type>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        auto [value, next] = std::move(inner_).recv(std::move(transport));
        record_(detail::recording::event_for_recv<typename protocol::message_type>(self_, peer_));
        return std::pair{std::move(value), wrap_(std::move(next))};
    }

    template <typename Transport>
        requires is_recv_v<protocol> && (!is_keyed_step_v<protocol>)
                 && (!ReadTransport<Transport, resource_type, typename protocol::message_type>)
    void recv(Transport) && = delete("[Transport_Shape] a read either polls, as std::optional<T>(Resource&), and "
                                     "returns no value while nothing is there, or declares its wait, as "
                                     "T(Resource&, fixy::session::watch::wait_scope&).  A read of another shape can "
                                     "wait where the watch of fixy/session/Watch.h does not see it.");

    // A keyed message is its label word.  The transport of the send is a
    // write of the word, and the event records the message as a send,
    // delivered unless the peer had crashed.
    template <typename Transport>
        requires is_send_v<protocol> && is_keyed_step_v<protocol>
                 && WriteTransport<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto send(Transport transport) && {
        using Message = typename protocol::message_type;
        bool is_delivered = false;
        auto marked = detail::observed_write<std::size_t, resource_type>(
            transport, [&is_delivered]() noexcept { is_delivered = true; });
        auto result = std::move(inner_).send(marked);
        record_(detail::recording::event_for_send<Message>(
            self_, peer_, is_delivered ? DeliveryFate::Delivered : DeliveryFate::LostToCrashedPeer));
        if constexpr (detail::recording::is_crash_send_shape<decltype(result)>::value) {
            using Wrapped = decltype(wrap_(std::move(result.next)));
            return CrashSend<Wrapped, Message>{wrap_(std::move(result.next)), std::move(result.undelivered)};
        } else {
            return wrap_(std::move(result));
        }
    }

    template <typename Transport>
        requires is_send_v<protocol> && is_keyed_step_v<protocol>
                 && (!WriteTransport<Transport, resource_type, std::size_t>)
    void send(Transport) && = delete("[Transport_Shape] a write of a word either tries, as "
                                     "bool(Resource&, std::size_t), or declares its wait, as "
                                     "void(Resource&, std::size_t, fixy::session::watch::wait_scope&).  A write of "
                                     "another shape can wait where the watch of fixy/session/Watch.h does not see it.");

    // The keyed receive gives the handle at the continuation and no value.
    template <typename Transport>
        requires is_recv_v<protocol> && is_keyed_step_v<protocol>
                 && ReadTransport<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        auto next = std::move(inner_).recv(std::move(transport));
        record_(detail::recording::event_for_recv<typename protocol::message_type>(self_, peer_));
        return wrap_(std::move(next));
    }

    template <typename Transport>
        requires is_recv_v<protocol> && is_keyed_step_v<protocol>
                 && (!ReadTransport<Transport, resource_type, std::size_t>)
    void recv(Transport) && = delete("[Transport_Shape] a read of a word either polls, as "
                                     "std::optional<std::size_t>(Resource&), or declares its wait, as "
                                     "std::size_t(Resource&, fixy::session::watch::wait_scope&).  A read of another "
                                     "shape can wait where the watch of fixy/session/Watch.h does not see it.");

    // The crash record of a crash branch.
    template <typename P = protocol>
        requires is_recv_v<P> && detail::recording::inner_can_detect_crash<Inner>
    [[nodiscard]] constexpr auto recv() && {
        auto [record, next] = std::move(inner_).recv();
        record_(SessionEvent::stop(self_, peer_, peer_, StopReasonKind::PeerCrashed, record.cause));
        return std::pair{record, wrap_(std::move(next))};
    }

    template <std::size_t I, typename Transport>
        requires is_select_v<protocol> && WriteTransport<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && {
        using Wire = detail::recording::wire_protocol_t<Inner>;
        static_assert(Wire::branch_count <= detail::recording::recordable_branch_limit,
                      "fixy::session::diagnostic [Recording_Branch_Lane_Too_Small]: the event records the branch "
                      "index in one byte, and this Select has more than 256 branches.");
        bool is_delivered = false;
        auto marked = detail::observed_write<std::size_t, resource_type>(
            transport, [&is_delivered]() noexcept { is_delivered = true; });
        auto next = std::move(inner_).template select<I>(marked);
        record_(SessionEvent::select(self_, peer_, static_cast<std::uint8_t>(I),
                                     is_delivered ? DeliveryFate::Delivered : DeliveryFate::LostToCrashedPeer,
                                     detail::recording::label_word_for_select<Wire, I>()));
        using Branch = std::tuple_element_t<I, typename protocol::branches_tuple>;
        if (const auto event = detail::recording::checkpoint_event<Branch>(self_, peer_, CheckpointRole::Active))
            record_(*event);
        return wrap_(std::move(next));
    }

    template <std::size_t I, typename Transport>
        requires is_select_v<protocol> && (!WriteTransport<Transport, resource_type, std::size_t>)
    void select(Transport) && = delete("[Transport_Shape] a write of a word either tries, as "
                                       "bool(Resource&, std::size_t), or declares its wait, as "
                                       "void(Resource&, std::size_t, fixy::session::watch::wait_scope&).  A write of "
                                       "another shape can wait where the watch of fixy/session/Watch.h does not see "
                                       "it.");

    // The label reader is whatever the inner handle takes: a polling read
    // or a declared read of a word for a plain or checkpoint handle, and a
    // polling read for a crash-watched one.  The recorder hands the inner
    // handle a read of the same shape, keeps the word it read, and names
    // the branch that the word names in the protocol of the wire.
    template <typename Reader, typename Handler>
        requires is_offer_v<protocol> && ReadTransport<Reader, resource_type, std::size_t>
    constexpr auto branch(Reader reader, Handler handler) && {
        using Wire = detail::recording::wire_protocol_t<Inner>;
        using Branches = typename protocol::branches_tuple;
        constexpr std::size_t count = std::tuple_size_v<Branches>;
        static_assert(count <= detail::recording::recordable_branch_limit,
                      "fixy::session::diagnostic [Recording_Branch_Lane_Too_Small]: the event records the branch "
                      "index in one byte, and this Offer has more than 256 branches.");
        std::optional<std::uint64_t> word;
        auto marked = detail::observed_read<std::size_t, resource_type>(
            reader, [&word](std::size_t read) noexcept { word = read; });
        return std::move(inner_).branch(marked, [this, &handler, &word](auto next) {
            // No word was read only when a crash-watched handle took the
            // crash branch.
            std::size_t taken = word ? branch_of_wire_word<Wire>(*word) : no_branch;
            LabelWord label{};
            if (word && is_keyed_choice_v<Wire>) label = LabelWord{*word};
            if constexpr (detail::recording::is_crash_watched_shape<Inner>::value) {
                using Head = typename decltype(next)::protocol;
                if constexpr (is_crash_branch_v<Head>) {
                    taken = crash_branch_index_v<protocol, typename Inner::peer_role>;
                    label = LabelWord{};
                }
            }
            record_(SessionEvent::offer(self_, peer_, static_cast<std::uint8_t>(taken), label));
            record_passive_checkpoint_<Branches>(taken, std::make_index_sequence<count>{});
            return std::invoke(handler, wrap_(std::move(next)));
        });
    }

    template <typename Reader, typename Handler>
        requires is_offer_v<protocol> && (!ReadTransport<Reader, resource_type, std::size_t>)
    void branch(Reader, Handler) && = delete("[Transport_Shape] a read of a word either polls, as "
                                             "std::optional<std::size_t>(Resource&), or declares its wait, as "
                                             "std::size_t(Resource&, fixy::session::watch::wait_scope&).  A read of "
                                             "another shape can wait where the watch of fixy/session/Watch.h does not "
                                             "see it.");

    template <typename P = protocol>
        requires is_terminal_state_v<P> && detail::recording::inner_can_close<Inner>
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
        requires(!is_terminal_state_v<P>) && detail::recording::inner_can_crash<Inner>
    [[nodiscard]] resource_type crash(CrashCause cause, CrashReporter&& announce) && {
        record_(SessionEvent::stop(self_, peer_, self_, StopReasonKind::LocalAbort, cause));
        return std::move(inner_).crash(cause, std::move(announce));
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

// ── Replay of a choice ───────────────────────────────────────────────

// The branch of Choice that a recorded Select or Offer names, or no value
// when the event does not fit Choice.  The kind of the event must match
// the side of the choice, and the index must name a branch.  In a keyed
// choice the event must carry the label word of that branch, and zero for
// a branch that is no label.  In a positional choice the word must be
// zero.  An event that a recorder wrote before label words existed carries
// zero, so it does not replay against a keyed choice.  Complexity: linear
// in the branches of Choice.
template <typename Choice>
    requires(is_select_v<Choice> || is_offer_v<Choice>)
[[nodiscard]] constexpr std::optional<std::size_t> replayed_branch(const SessionEvent& event) noexcept {
    constexpr SessionOp expected = is_select_v<Choice> ? SessionOp::Select : SessionOp::Offer;
    if (event.op() != expected) return std::nullopt;
    const std::size_t index = event.branch_index();
    if (index >= Choice::branch_count) return std::nullopt;
    const auto& words = detail::wire_words_v<Choice>;
    if constexpr (is_keyed_choice_v<Choice>) {
        const std::uint64_t expected_word = words[index].is_wired ? words[index].value : 0;
        if (event.label_word().value != expected_word) return std::nullopt;
    } else {
        if (event.label_word().value != 0) return std::nullopt;
    }
    return index;
}

}  // namespace fixy::session
