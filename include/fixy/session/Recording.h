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
// recorded as a plain send.  No payload is an epoched delegation, so no
// factory of fixy/session/EventLog.h writes an epoched hand-off.  An
// epoched delegation payload must join those two functions, or the
// recorder writes it as a plain Send and loses its two thresholds.

#include <fixy/session/Checkpoint.h>
#include <fixy/session/CrashTransport.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Payload.h>

#include <foundation/NoObject.h>
#include <foundation/reflect/Hash.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <utility>

// The row-hash identity of a recorded handle, declared and never defined.
// foundation/diag/RowHash.h folds it with the inner handle, so a recorded
// handle and the handle that it wraps take two cache slots.
namespace fixy::row_discipline {
struct recorded;
}  // namespace fixy::row_discipline

namespace fixy::session {

template <typename Inner>
class Recorded;

namespace detail::recording {

template <typename H>
struct is_crash_watched_shape : std::false_type {};
template <typename H, typename Self, typename Peer, typename Reliable, typename Ctx, typename Position>
struct is_crash_watched_shape<CrashWatched<H, Self, Peer, Reliable, Ctx, Position>> : std::true_type {};

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
template <typename H, typename Self, typename Peer, typename Reliable, typename Ctx, typename Position>
struct wire_protocol<CrashWatched<H, Self, Peer, Reliable, Ctx, Position>> : wire_protocol<H> {};
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
        return LabelWord{::fixy::session::detail::branch_wire_word_of<Choice, I>()};
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
    if constexpr (std::is_same_v<Reason, detach_reason::InfiniteLoopProtocol>)
        return DetachReasonKind::InfiniteLoopProtocol;
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
concept inner_can_crash = requires(Inner&& inner, CrashCause cause) { std::move(inner).crash(cause); };

}  // namespace detail::recording

// What a recorder accepts: a plain handle, a crash-watched handle or a
// checkpoint handle.  The recorder reads the label words of each from the
// plain handle at the bottom of the decorators, and a handle of another
// shape has no such protocol.  A recorded handle is not one of the three,
// so the recorder does not record one session two times.
template <typename H>
concept RecordableHandle = requires {
    typename H::protocol;
    typename H::resource_type;
    typename detail::recording::wire_protocol<H>::type;
};

// ── The door of the recorder ────────────────────────────────────────
//
// The one class that builds a recorder and steps it.  The recorder
// befriends this class and no other, and no class template, so no
// specialization of the recorder reaches the constructor or the inner
// handle of another.  The class is final, and it is defined in this
// header, so a second definition in a translation unit that includes the
// header is a redefinition error.
//
// Each public member either states the whole gate of
// mint_recorded_session (make), or takes a live recorder and does one
// complete step on it: it steps the inner handle, records what happened,
// and wraps the successor.  The recorder forwards each step here, so a
// direct call is the same operation as the method.  No object of the
// class exists.
class RecordingDoor final : ::foundation::NoObject<RecordingDoor> {
    template <typename Inner, typename Next>
    [[nodiscard]] static constexpr Recorded<Next> wrap_(const Recorded<Inner>& from, Next next) noexcept {
        return Recorded<Next>{std::move(next), *from.log_, from.self_, from.peer_};
    }

    template <typename Inner>
    static void record_(const Recorded<Inner>& recorder, SessionEvent event) {
        recorder.record_(event);
    }

    template <typename Branches, typename Inner, std::size_t... Is>
    static void record_passive_checkpoint_(const Recorded<Inner>& recorder, std::size_t taken,
                                           std::index_sequence<Is...>) {
        (
            [&] {
                using Branch = std::tuple_element_t<Is, Branches>;
                if (taken == Is) {
                    if (const auto event = detail::recording::checkpoint_event<Branch>(recorder.self_, recorder.peer_,
                                                                                       CheckpointRole::Passive))
                        record_(recorder, *event);
                }
            }(),
            ...);
    }

public:
    // mint_recorded_session: puts the recorder around the handle.  The
    // recorder writes each step of the handle to the log, with self and
    // peer as the two roles.
    template <typename H>
        requires RecordableHandle<H>
    [[nodiscard]] static constexpr Recorded<H> make(H handle, SessionEventLog& log, RoleTagId self,
                                                    RoleTagId peer) noexcept {
        return Recorded<H>{std::move(handle), log, self, peer};
    }

    // The recorder hands the inner handle a write of the same shape as the
    // transport, which notes when the transport took the payload.  A payload
    // that no transport took, because the peer had crashed, is recorded as
    // lost to the crashed peer.
    //
    // The value goes by reference to the send of the inner handle, which
    // takes it by value.  So the recorder moves a payload one time, as a
    // plain send does.  It keeps no moved-from copy of the payload.
    template <typename Inner, typename T, typename Transport>
        requires is_send_v<typename Inner::protocol> && (!is_keyed_step_v<typename Inner::protocol>)
              && WriteTransport<Transport, typename Inner::resource_type, typename Inner::protocol::message_type>
    [[nodiscard]] static constexpr auto send(Recorded<Inner>&& recorder, T&& value, Transport transport) {
        using Message = typename Inner::protocol::message_type;
        bool is_delivered = false;
        auto marked = detail::observed_write<Message, typename Inner::resource_type>(
            transport, [&is_delivered]() noexcept { is_delivered = true; });
        auto result = std::move(recorder.inner_).send(std::forward<T>(value), marked);
        record_(recorder, detail::recording::event_for_send<Message>(recorder.self_, recorder.peer_,
                                                                     is_delivered ? DeliveryFate::Delivered
                                                                                  : DeliveryFate::LostToCrashedPeer));
        if constexpr (detail::recording::is_crash_send_shape<decltype(result)>::value) {
            using Wrapped = decltype(wrap_(recorder, std::move(result.next)));
            return CrashSend<Wrapped, Message>{wrap_(recorder, std::move(result.next)), std::move(result.undelivered)};
        } else {
            return wrap_(recorder, std::move(result));
        }
    }

    template <typename Inner, typename Transport>
        requires is_recv_v<typename Inner::protocol> && (!is_keyed_step_v<typename Inner::protocol>)
              && ReadTransport<Transport, typename Inner::resource_type, typename Inner::protocol::message_type>
    [[nodiscard]] static constexpr auto recv(Recorded<Inner>&& recorder, Transport transport) {
        auto [value, next] = std::move(recorder.inner_).recv(std::move(transport));
        record_(recorder, detail::recording::event_for_recv<typename Inner::protocol::message_type>(recorder.self_,
                                                                                                    recorder.peer_));
        return std::pair{std::move(value), wrap_(recorder, std::move(next))};
    }

    // A keyed message is its label word and then the value of its payload.
    // The transport of this send is a write of the word, and the event
    // records the message as a send, delivered unless the peer had
    // crashed.  The value step that follows is a plain send, and it records
    // one more Send event, of the payload.
    template <typename Inner, typename Transport>
        requires is_send_v<typename Inner::protocol> && is_keyed_step_v<typename Inner::protocol>
              && WriteTransport<Transport, typename Inner::resource_type, std::size_t>
    [[nodiscard]] static constexpr auto send(Recorded<Inner>&& recorder, Transport transport) {
        using Message = typename Inner::protocol::message_type;
        bool is_delivered = false;
        auto marked = detail::observed_write<std::size_t, typename Inner::resource_type>(
            transport, [&is_delivered]() noexcept { is_delivered = true; });
        auto result = std::move(recorder.inner_).send(marked);
        record_(recorder, detail::recording::event_for_send<Message>(recorder.self_, recorder.peer_,
                                                                     is_delivered ? DeliveryFate::Delivered
                                                                                  : DeliveryFate::LostToCrashedPeer));
        if constexpr (detail::recording::is_crash_send_shape<decltype(result)>::value) {
            using Wrapped = decltype(wrap_(recorder, std::move(result.next)));
            return CrashSend<Wrapped, Message>{wrap_(recorder, std::move(result.next)), std::move(result.undelivered)};
        } else {
            return wrap_(recorder, std::move(result));
        }
    }

    // The keyed receive reads the label word.  It gives the handle at the
    // value step, or past the message when the payload is void, and no
    // value.
    template <typename Inner, typename Transport>
        requires is_recv_v<typename Inner::protocol> && is_keyed_step_v<typename Inner::protocol>
              && ReadTransport<Transport, typename Inner::resource_type, std::size_t>
    [[nodiscard]] static constexpr auto recv(Recorded<Inner>&& recorder, Transport transport) {
        auto next = std::move(recorder.inner_).recv(std::move(transport));
        record_(recorder, detail::recording::event_for_recv<typename Inner::protocol::message_type>(recorder.self_,
                                                                                                    recorder.peer_));
        return wrap_(recorder, std::move(next));
    }

    // The crash record of a crash branch.
    template <typename Inner>
        requires is_recv_v<typename Inner::protocol> && detail::recording::inner_can_detect_crash<Inner>
    [[nodiscard]] static constexpr auto recv(Recorded<Inner>&& recorder) {
        auto [record, next] = std::move(recorder.inner_).recv();
        record_(recorder, SessionEvent::stop(recorder.self_, recorder.peer_, recorder.peer_,
                                             StopReasonKind::PeerCrashed, record.cause));
        return std::pair{record, wrap_(recorder, std::move(next))};
    }

    template <std::size_t I, typename Inner, typename Transport>
        requires is_select_v<typename Inner::protocol>
              && WriteTransport<Transport, typename Inner::resource_type, std::size_t>
    [[nodiscard]] static constexpr auto select(Recorded<Inner>&& recorder, Transport transport) {
        using Wire = detail::recording::wire_protocol_t<Inner>;
        static_assert(Wire::branch_count <= detail::recording::recordable_branch_limit,
                      "fixy::session::diagnostic [Recording_Branch_Lane_Too_Small]: the event records the branch "
                      "index in one byte, and this Select has more than 256 branches.");
        bool is_delivered = false;
        auto marked = detail::observed_write<std::size_t, typename Inner::resource_type>(
            transport, [&is_delivered]() noexcept { is_delivered = true; });
        auto next = std::move(recorder.inner_).template select<I>(marked);
        record_(recorder, SessionEvent::select(recorder.self_, recorder.peer_, static_cast<std::uint8_t>(I),
                                               is_delivered ? DeliveryFate::Delivered : DeliveryFate::LostToCrashedPeer,
                                               detail::recording::label_word_for_select<Wire, I>()));
        using Branch = std::tuple_element_t<I, typename Inner::protocol::branches_tuple>;
        if (const auto event =
                detail::recording::checkpoint_event<Branch>(recorder.self_, recorder.peer_, CheckpointRole::Active))
            record_(recorder, *event);
        return wrap_(recorder, std::move(next));
    }

    // The label reader is whatever the inner handle takes: a polling read
    // or a declared read of a word for a plain or checkpoint handle, and a
    // polling read for a crash-watched one.  The recorder hands the inner
    // handle a read of the same shape, keeps the word it read, and names
    // the branch that the word names in the protocol of the wire.
    template <typename Inner, typename Reader, typename Handler>
        requires is_offer_v<typename Inner::protocol>
              && ReadTransport<Reader, typename Inner::resource_type, std::size_t>
    static constexpr auto branch(Recorded<Inner>&& recorder, Reader reader, Handler handler) {
        using P = typename Inner::protocol;
        using Wire = detail::recording::wire_protocol_t<Inner>;
        using Branches = typename P::branches_tuple;
        constexpr std::size_t count = std::tuple_size_v<Branches>;
        static_assert(count <= detail::recording::recordable_branch_limit,
                      "fixy::session::diagnostic [Recording_Branch_Lane_Too_Small]: the event records the branch "
                      "index in one byte, and this Offer has more than 256 branches.");
        std::optional<std::uint64_t> word;
        auto marked = detail::observed_read<std::size_t, typename Inner::resource_type>(
            reader, [&word](std::size_t read) noexcept { word = read; });
        return std::move(recorder.inner_).branch(marked, [&recorder, &handler, &word](auto next) {
            // No word was read only when a crash-watched handle took the
            // crash branch.
            std::size_t taken = word ? branch_of_wire_word<Wire>(*word) : no_branch;
            LabelWord label{};
            if (word && is_keyed_choice_v<Wire>) label = LabelWord{*word};
            if constexpr (detail::recording::is_crash_watched_shape<Inner>::value) {
                using Head = typename decltype(next)::protocol;
                if constexpr (is_crash_branch_v<Head>) {
                    using PeerRole = typename Inner::peer_role;
                    taken = detail::crash::crash_branch_position(^^P, ^^PeerRole);
                    label = LabelWord{};
                }
            }
            record_(recorder,
                    SessionEvent::offer(recorder.self_, recorder.peer_, static_cast<std::uint8_t>(taken), label));
            record_passive_checkpoint_<Branches>(recorder, taken, std::make_index_sequence<count>{});
            return std::invoke(handler, wrap_(recorder, std::move(next)));
        });
    }
};

template <typename Inner>
class [[nodiscard]] Recorded {
    Inner inner_;
    SessionEventLog* log_;
    RoleTagId self_;
    RoleTagId peer_;

    // The one friend.  The door builds each recorder and does each step, so
    // no specialization of this template reaches another.
    friend class RecordingDoor;

    constexpr Recorded(Inner inner, SessionEventLog& log, RoleTagId self, RoleTagId peer) noexcept
        : inner_{std::move(inner)}, log_{&log}, self_{self}, peer_{peer} {}

    void record_(SessionEvent event) const { log_->record_now(event); }

public:
    using inner_type = Inner;
    using protocol = typename Inner::protocol;
    using resource_type = typename Inner::resource_type;

    // The row-hash shape of a discipline carrier.  The inner handle folds
    // its own claim, so a recorder over a crash-watched handle keeps the
    // crash claim in its hash.
    using row_discipline = ::fixy::row_discipline::recorded;
    using row_payload = Inner;

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
    [[nodiscard]] constexpr auto send(T&& value, Transport transport) && {
        return RecordingDoor::send(std::move(*this), std::forward<T>(value), std::move(transport));
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
        return RecordingDoor::recv(std::move(*this), std::move(transport));
    }

    template <typename Transport>
        requires is_recv_v<protocol> && (!is_keyed_step_v<protocol>)
                  && (!ReadTransport<Transport, resource_type, typename protocol::message_type>)
    void recv(Transport) && = delete("[Transport_Shape] a read either polls, as std::optional<T>(Resource&), and "
                                     "returns no value while nothing is there, or declares its wait, as "
                                     "T(Resource&, fixy::session::watch::wait_scope&).  A read of another shape can "
                                     "wait where the watch of fixy/session/Watch.h does not see it.");

    // A keyed message is its label word and then the value of its payload.
    // This send writes the word, and the value step that follows is a plain
    // send.
    template <typename Transport>
        requires is_send_v<protocol> && is_keyed_step_v<protocol>
              && WriteTransport<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto send(Transport transport) && {
        return RecordingDoor::send(std::move(*this), std::move(transport));
    }

    template <typename Transport>
        requires is_send_v<protocol> && is_keyed_step_v<protocol>
                  && (!WriteTransport<Transport, resource_type, std::size_t>)
    void send(Transport) && = delete("[Transport_Shape] a write of a word either tries, as "
                                     "bool(Resource&, std::size_t), or declares its wait, as "
                                     "void(Resource&, std::size_t, fixy::session::watch::wait_scope&).  A write of "
                                     "another shape can wait where the watch of fixy/session/Watch.h does not see it.");

    // The keyed receive reads the label word.  It gives the handle at the
    // value step, or past the message when the payload is void, and no
    // value.
    template <typename Transport>
        requires is_recv_v<protocol> && is_keyed_step_v<protocol>
              && ReadTransport<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        return RecordingDoor::recv(std::move(*this), std::move(transport));
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
        return RecordingDoor::recv(std::move(*this));
    }

    template <std::size_t I, typename Transport>
        requires is_select_v<protocol> && WriteTransport<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && {
        return RecordingDoor::template select<I>(std::move(*this), std::move(transport));
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
        return RecordingDoor::branch(std::move(*this), std::move(reader), std::move(handler));
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
        record_(
            SessionEvent::detach(self_, peer_, detail::recording::detach_kind<Reason>(), default_schema_hash<Reason>));
        std::move(inner_).detach(reason);
    }

    template <typename P = protocol>
        requires(!is_terminal_state_v<P>) && detail::recording::inner_can_crash<Inner>
    [[nodiscard]] resource_type crash(CrashCause cause) && {
        record_(SessionEvent::stop(self_, peer_, self_, StopReasonKind::LocalAbort, cause));
        return std::move(inner_).crash(cause);
    }

    [[nodiscard]] constexpr resource_type& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const resource_type& resource() const& noexcept { return inner_.resource(); }
};

// ── The mint ─────────────────────────────────────────────────────────

template <typename H>
    requires RecordableHandle<H>
[[nodiscard]] constexpr auto mint_recorded_session(H handle, SessionEventLog& log, RoleTagId self,
                                                   RoleTagId peer) noexcept {
    return RecordingDoor::make<H>(std::move(handle), log, self, peer);
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
