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
#include <fixy/session/Classification.h>
#include <fixy/session/CrashTransport.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Handle.h>

#include <foundation/NoObject.h>
#include <foundation/core/Record.h>
#include <foundation/reflect/Hash.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
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

// ── Hashes of types ─────────────────────────────────────────────────
//
// An event names a payload, a protocol or a permission set by the stable id
// of its spelling with each brand erased (brand_erased of
// fixy/session/Classification.h).  A fresh brand is a closure type, whose
// name depends on its unit, so no brand enters a stored hash.  Two units give
// one hash for one payload, and a type with no brand keeps the stable id of
// its own spelling.

template <typename T>
inline constexpr SchemaHash default_schema_hash{detail::brand_erased_type_id(^^T)};

template <typename T>
inline constexpr StateHash default_proto_hash{detail::brand_erased_type_id(^^T)};

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
struct is_crash_send_shape<CrashSend<Next, T>> : std::true_type {
    // The type of the payload that a send gives back when it did not go.
    using payload = T;
};

// The payload families a recorder knows.  A payload whose rule has the
// role hand_off is a hand-off (fixy/session/Registry.h).  Every other
// payload that Classification.h accepts is a message.
[[nodiscard]] consteval bool is_hand_off_payload(std::meta::info type) {
    const payload_lookup rule = payload_rule_of(type);
    return rule.is_found && rule.entry.role == payload_role::hand_off;
}

template <typename T>
struct is_delegation_shape : std::bool_constant<is_hand_off_payload(^^T)> {};

// The protocol and the permission set of the endpoint of a hand-off, as
// its rule names them.
template <typename T>
using hand_off_protocol_t = [:hand_off_protocol_of(^^T):];
template <typename T>
using hand_off_set_t = [:rule_argument_of(^^T, payload_rule_of(^^T).entry.set_argument):];

template <typename T>
[[nodiscard]] constexpr SessionEvent event_for_send(RoleTagId self, RoleTagId peer, DeliveryFate fate) noexcept {
    if constexpr (is_delegation_shape<T>::value) {
        return SessionEvent::delegate_handoff(self, peer, default_proto_hash<hand_off_protocol_t<T>>,
                                              InnerPermSetHash{brand_erased_type_id(^^hand_off_set_t<T>)});
    } else {
        return SessionEvent::send(self, peer, default_schema_hash<T>, PayloadHash{}, fate);
    }
}

template <typename T>
[[nodiscard]] constexpr SessionEvent event_for_recv(RoleTagId self, RoleTagId peer) noexcept {
    if constexpr (is_delegation_shape<T>::value) {
        return SessionEvent::accept_handoff(self, peer, default_proto_hash<hand_off_protocol_t<T>>,
                                            InnerPermSetHash{brand_erased_type_id(^^hand_off_set_t<T>)});
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

// The checkpoint kind of a branch, or no event.  The branch is read under
// its vendor pins and its names, as the checkpoint door reads it.
template <typename Branch>
[[nodiscard]] constexpr std::optional<SessionEvent> checkpoint_event(RoleTagId self, RoleTagId peer,
                                                                     CheckpointRole role) noexcept {
    using Bare = ::fixy::session::detail::checkpoint::bare_t<Branch>;
    if constexpr (std::is_same_v<Bare, Roll>) {
        return SessionEvent::checkpoint_roll(self, peer, role);
    } else if constexpr (std::is_same_v<Bare, Abort>) {
        return SessionEvent::checkpoint_abort(self, peer, role);
    } else if constexpr (is_checkpoint_primitive<Bare>::value) {
        return SessionEvent::checkpoint_commit(self, peer, role, default_proto_hash<typename Bare::next>);
    } else {
        return std::nullopt;
    }
}

// The operations of the inner handle that the recorder forwards with no
// transport.  Each such member of Recorded exists only when the inner
// handle has it.  A member that takes a transport exists when the
// transport has a shape of fixy/session/Transport.h, and a shape that the
// inner handle refuses reaches the refusal of the inner handle.
template <typename Inner>
concept inner_can_detect_crash = requires(Inner&& inner) { std::move(inner).recv(); };

template <typename Inner>
concept inner_can_close = requires(Inner&& inner) { std::move(inner).close(); };

// What the close of the inner handle gives: its Resource alone, or, for an End
// handle that holds a token, the Record of the Resource and the token of each
// tag (closed_of of fixy/session/Handle.h).  A decorator states no set and
// closes to its Resource.
template <typename Inner>
struct close_of {
    using type = typename Inner::resource_type;
};

template <typename Inner>
    requires requires { typename Inner::perm_set; }
struct close_of<Inner> {
    using type =
        typename ::fixy::session::detail::closed_of<typename Inner::resource_type, typename Inner::perm_set>::type;
};

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
    // Moves the writer of the log from the recorder of a step to the
    // recorder of its successor, so each step records before it wraps.
    template <typename Inner, typename Next>
    [[nodiscard]] static constexpr Recorded<Next> wrap_(Recorded<Inner>& from, Next next) noexcept {
        return Recorded<Next>{std::move(next), std::move(from.writer_), from.self_, from.peer_};
    }

    template <typename Inner>
    static void record_(Recorded<Inner>& recorder, SessionEvent event) {
        recorder.record_(event);
    }

    // Records the receive of a value, and gives the value and the recorder
    // of the successor in one Record.  The transport form and the pole form
    // of a receive both end here.  A structured binding gives the two out.
    template <typename Inner, typename Value, typename Next>
    [[nodiscard]] static constexpr auto received_(Recorded<Inner>& recorder, Value value, Next next) {
        record_(recorder, detail::recording::event_for_recv<typename Inner::protocol::message_type>(recorder.self_,
                                                                                                    recorder.peer_));
        return ::foundation::core::Record{std::move(value), wrap_(recorder, std::move(next))};
    }

    // Records the select of branch I with its fate, and the checkpoint of the
    // branch when it opens one, and gives the recorder of the successor.  The
    // transport form and the form of a pole of a select both end here.
    template <std::size_t I, typename Inner, typename Next>
    [[nodiscard]] static constexpr auto selected_(Recorded<Inner>& recorder, Next next, DeliveryFate fate) {
        using Wire = detail::recording::wire_protocol_t<Inner>;
        record_(recorder, SessionEvent::select(recorder.self_, recorder.peer_, static_cast<std::uint8_t>(I), fate,
                                               detail::recording::label_word_for_select<Wire, I>()));
        using Branch = std::tuple_element_t<I, typename Inner::protocol::branches_tuple>;
        if (const auto event =
                detail::recording::checkpoint_event<Branch>(recorder.self_, recorder.peer_, CheckpointRole::Active))
            record_(recorder, *event);
        return wrap_(recorder, std::move(next));
    }

    template <typename Branches, typename Inner, std::size_t... Is>
    static void record_passive_checkpoint_(Recorded<Inner>& recorder, std::size_t taken, std::index_sequence<Is...>) {
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
    // recorder writes each step of the handle to the log of the writer, with
    // self and peer as the two roles.
    template <typename H>
        requires RecordableHandle<H>
    [[nodiscard]] static constexpr Recorded<H> make(H handle, SessionEventWriter writer, RoleTagId self,
                                                    RoleTagId peer) noexcept {
        return Recorded<H>{std::move(handle), std::move(writer), self, peer};
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
              && (!ChannelPole<typename Inner::resource_type>)
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
              && (!ChannelPole<typename Inner::resource_type>)
              && ReadTransport<Transport, typename Inner::resource_type, typename Inner::protocol::message_type>
    [[nodiscard]] static constexpr auto recv(Recorded<Inner>&& recorder, Transport transport) {
        auto [value, next] = std::move(recorder.inner_).recv(std::move(transport));
        return received_(recorder, std::move(value), std::move(next));
    }

    // A recorded session over a pole of a channel steps through the pole form
    // of the inner handle, which uses the transport of the pole table.  No
    // mint wraps a pole in a crash watch (fixy/channels/EndpointMint.h), so a
    // send of a pole returns only when the write of its table took the
    // payload, and the event records the payload as delivered.
    template <typename Inner, typename Ctx, typename T>
        requires is_send_v<typename Inner::protocol> && (!is_keyed_step_v<typename Inner::protocol>)
              && PoleWritesIn<typename Inner::resource_type, Ctx, typename Inner::protocol::message_type>
    [[nodiscard]] static constexpr auto send(Recorded<Inner>&& recorder, Ctx const& ctx, T&& value) {
        using Message = typename Inner::protocol::message_type;
        auto next = std::move(recorder.inner_).send(ctx, std::forward<T>(value));
        record_(recorder,
                detail::recording::event_for_send<Message>(recorder.self_, recorder.peer_, DeliveryFate::Delivered));
        return wrap_(recorder, std::move(next));
    }

    template <typename Inner, typename Ctx>
        requires is_recv_v<typename Inner::protocol> && (!is_keyed_step_v<typename Inner::protocol>)
              && PoleReadsIn<typename Inner::resource_type, Ctx, typename Inner::protocol::message_type>
    [[nodiscard]] static constexpr auto recv(Recorded<Inner>&& recorder, Ctx const& ctx) {
        auto [value, next] = std::move(recorder.inner_).recv(ctx);
        return received_(recorder, std::move(value), std::move(next));
    }

    // A keyed message is its label word and then the value of its payload.
    // The transport of this send is a write of the word, and the event
    // records the message as a send, delivered unless the peer had
    // crashed.  The value step that follows is a plain send, and it records
    // one more Send event, of the payload.
    template <typename Inner, typename Transport>
        requires is_send_v<typename Inner::protocol> && is_keyed_step_v<typename Inner::protocol>
              && (!ChannelPole<typename Inner::resource_type>)
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
            // The crash decorator gives back the label word, not the message.
            using Wrapped = decltype(wrap_(recorder, std::move(result.next)));
            using Undelivered = typename detail::recording::is_crash_send_shape<decltype(result)>::payload;
            return CrashSend<Wrapped, Undelivered>{wrap_(recorder, std::move(result.next)),
                                                   std::move(result.undelivered)};
        } else {
            return wrap_(recorder, std::move(result));
        }
    }

    // The keyed receive reads the label word.  It gives the handle at the
    // value step, or past the message when the payload is void, and no
    // value.
    template <typename Inner, typename Transport>
        requires is_recv_v<typename Inner::protocol> && is_keyed_step_v<typename Inner::protocol>
              && (!ChannelPole<typename Inner::resource_type>)
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
        return ::foundation::core::Record{record, wrap_(recorder, std::move(next))};
    }

    // A select over a pole of a channel takes only no_label, and the inner
    // handle takes no_label itself: the choice puts no word on the wire, so
    // it is delivered.
    template <std::size_t I, typename Inner, typename Transport>
        requires is_select_v<typename Inner::protocol>
              && WriteTransport<Transport, typename Inner::resource_type, std::size_t>
              && (!ChannelPole<typename Inner::resource_type> || std::is_same_v<Transport, no_label_t>)
    [[nodiscard]] static constexpr auto select(Recorded<Inner>&& recorder, Transport transport) {
        using Wire = detail::recording::wire_protocol_t<Inner>;
        static_assert(Wire::branch_count <= detail::recording::recordable_branch_limit,
                      "fixy::session::diagnostic [Recording_Branch_Lane_Too_Small]: the event records the branch "
                      "index in one byte, and this Select has more than 256 branches.");
        if constexpr (ChannelPole<typename Inner::resource_type>) {
            auto next = std::move(recorder.inner_).template select<I>(transport);
            return selected_<I, Inner>(recorder, std::move(next), DeliveryFate::Delivered);
        } else {
            bool is_delivered = false;
            auto marked = detail::observed_write<std::size_t, typename Inner::resource_type>(
                transport, [&is_delivered]() noexcept { is_delivered = true; });
            auto next = std::move(recorder.inner_).template select<I>(marked);
            return selected_<I, Inner>(recorder, std::move(next),
                                       is_delivered ? DeliveryFate::Delivered : DeliveryFate::LostToCrashedPeer);
        }
    }

    // The label reader is whatever the inner handle takes: a polling read
    // or a declared read of a word for a plain or checkpoint handle, and a
    // polling read for a crash-watched one.  The recorder hands the inner
    // handle a read of the same shape, keeps the word it read, and names
    // the branch that the word names in the protocol of the wire.
    template <typename Inner, typename Reader, typename Handler>
        requires is_offer_v<typename Inner::protocol> && (!ChannelPole<typename Inner::resource_type>)
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
    // The one writer of the log.  Each step moves it to the recorder of
    // the successor.
    SessionEventWriter writer_;
    RoleTagId self_;
    RoleTagId peer_;

    // The one friend.  The door builds each recorder and does each step, so
    // no specialization of this template reaches another.
    friend class RecordingDoor;

    constexpr Recorded(Inner inner, SessionEventWriter writer, RoleTagId self, RoleTagId peer) noexcept
        : inner_{std::move(inner)}, writer_{std::move(writer)}, self_{self}, peer_{peer} {}

    void record_(SessionEvent event) { writer_.record_now(event); }

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
        requires is_send_v<protocol> && (!is_keyed_step_v<protocol>) && (!ChannelPole<resource_type>)
              && WriteTransport<Transport, resource_type, typename protocol::message_type>
    [[nodiscard]] constexpr auto send(T&& value, Transport transport) && {
        return RecordingDoor::send(std::move(*this), std::forward<T>(value), std::move(transport));
    }

    template <typename T, typename Transport>
        requires is_send_v<protocol> && (!is_keyed_step_v<protocol>) && (!ChannelPole<resource_type>)
                  && (!WriteShape<Transport, resource_type, typename protocol::message_type>)
    void send(T, Transport) && = delete("[Transport_Shape] a write either tries, as bool(Resource&, T&), and returns "
                                        "false with the value unchanged while it has no room, or declares its wait, "
                                        "as void(Resource&, T&&, fixy::session::watch::wait_scope&).  A write of "
                                        "another shape can wait where the watch of fixy/session/Watch.h does not see "
                                        "it.");

    template <typename Transport>
        requires is_recv_v<protocol> && (!is_keyed_step_v<protocol>) && (!ChannelPole<resource_type>)
              && ReadTransport<Transport, resource_type, typename protocol::message_type>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        return RecordingDoor::recv(std::move(*this), std::move(transport));
    }

    template <typename Transport>
        requires is_recv_v<protocol> && (!is_keyed_step_v<protocol>) && (!ChannelPole<resource_type>)
                  && (!ReadShape<Transport, resource_type, typename protocol::message_type>)
    void recv(Transport) && = delete("[Transport_Shape] a read either polls, as fixy::Option<T>(Resource&), and "
                                     "returns no value while nothing is there, or declares its wait, as "
                                     "T(Resource&, fixy::session::watch::wait_scope&).  A read of another shape can "
                                     "wait where the watch of fixy/session/Watch.h does not see it.");

    // A recorded session over a pole of a channel steps in the context of
    // the step, through the transport of the pole table, as the inner handle
    // does.
    template <typename Ctx, typename T>
        requires is_send_v<protocol> && (!is_keyed_step_v<protocol>)
              && PoleWritesIn<resource_type, Ctx, typename protocol::message_type>
    [[nodiscard]] constexpr auto send(Ctx const& ctx, T&& value) && {
        return RecordingDoor::send(std::move(*this), ctx, std::forward<T>(value));
    }

    template <typename First, typename Second>
        requires is_send_v<protocol> && (!is_keyed_step_v<protocol>) && ChannelPole<resource_type>
                  && (!PoleWritesIn<resource_type, std::remove_cvref_t<First>, typename protocol::message_type>)
    void send(First&&, Second&&) && = delete(
        "[Pole_Owns_Its_Transport] a session over a pole of a channel sends through the write that the pole table of "
        "its channel names (fixy/session/Admission.h).  Call send(ctx, value) with a context that the write of the "
        "table admits.  A transport of the caller would move the values of one channel in two ways.");

    template <typename Ctx>
        requires is_recv_v<protocol> && (!is_keyed_step_v<protocol>)
              && PoleReadsIn<resource_type, Ctx, typename protocol::message_type>
    [[nodiscard]] constexpr auto recv(Ctx const& ctx) && {
        return RecordingDoor::recv(std::move(*this), ctx);
    }

    template <typename Arg>
        requires is_recv_v<protocol> && (!is_keyed_step_v<protocol>) && ChannelPole<resource_type>
                  && (!PoleReadsIn<resource_type, std::remove_cvref_t<Arg>, typename protocol::message_type>)
    void recv(Arg&&) && = delete(
        "[Pole_Owns_Its_Transport] a session over a pole of a channel receives through the read that the pole table "
        "of its channel names (fixy/session/Admission.h).  Call recv(ctx) with a context that the read of the table "
        "admits.  A transport of the caller would move the values of one channel in two ways.");

    // A keyed message is its label word and then the value of its payload.
    // This send writes the word, and the value step that follows is a plain
    // send.
    template <typename Transport>
        requires is_send_v<protocol> && is_keyed_step_v<protocol> && (!ChannelPole<resource_type>)
              && WriteTransport<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto send(Transport transport) && {
        return RecordingDoor::send(std::move(*this), std::move(transport));
    }

    template <typename Arg>
        requires is_send_v<protocol> && is_keyed_step_v<protocol> && ChannelPole<resource_type>
    void send(Arg&&) && = delete(
        "[Pole_Owns_Its_Transport] a keyed send over a pole of a channel writes a label word, and the pole table of "
        "its channel (fixy/session/Admission.h) moves no label word.  The send needs a pole table that moves the "
        "label word.  A write of the caller would move the words of one channel in two ways.");

    template <typename Transport>
        requires is_send_v<protocol> && is_keyed_step_v<protocol> && (!ChannelPole<resource_type>)
                  && (!WriteShape<Transport, resource_type, std::size_t>)
    void send(Transport) && = delete("[Transport_Shape] a write of a word either tries, as "
                                     "bool(Resource&, std::size_t), or declares its wait, as "
                                     "void(Resource&, std::size_t, fixy::session::watch::wait_scope&).  A write of "
                                     "another shape can wait where the watch of fixy/session/Watch.h does not see it.");

    // The keyed receive reads the label word.  It gives the handle at the
    // value step, or past the message when the payload is void, and no
    // value.
    template <typename Transport>
        requires is_recv_v<protocol> && is_keyed_step_v<protocol> && (!ChannelPole<resource_type>)
              && ReadTransport<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        return RecordingDoor::recv(std::move(*this), std::move(transport));
    }

    template <typename Arg>
        requires is_recv_v<protocol> && is_keyed_step_v<protocol> && ChannelPole<resource_type>
    void recv(Arg&&) && = delete(
        "[Pole_Owns_Its_Transport] a keyed receive over a pole of a channel reads a label word, and the pole table of "
        "its channel (fixy/session/Admission.h) moves no label word.  The receive needs a pole table that moves the "
        "label word.  A read of the caller would move the words of one channel in two ways.");

    template <typename Transport>
        requires is_recv_v<protocol> && is_keyed_step_v<protocol> && (!ChannelPole<resource_type>)
                  && (!ReadShape<Transport, resource_type, std::size_t>)
    void recv(Transport) && = delete("[Transport_Shape] a read of a word either polls, as "
                                     "fixy::Option<std::size_t>(Resource&), or declares its wait, as "
                                     "std::size_t(Resource&, fixy::session::watch::wait_scope&).  A read of another "
                                     "shape can wait where the watch of fixy/session/Watch.h does not see it.");

    // The crash record of a crash branch.
    template <typename P = protocol>
        requires is_recv_v<P> && detail::recording::inner_can_detect_crash<Inner>
    [[nodiscard]] constexpr auto recv() && {
        return RecordingDoor::recv(std::move(*this));
    }

    // A select over a pole of a channel takes only no_label, as the select of
    // the inner handle does.
    template <std::size_t I, typename Transport>
        requires is_select_v<protocol> && WriteTransport<Transport, resource_type, std::size_t>
              && (!ChannelPole<resource_type> || std::is_same_v<Transport, no_label_t>)
    [[nodiscard]] constexpr auto select(Transport transport) && {
        return RecordingDoor::template select<I>(std::move(*this), std::move(transport));
    }

    template <std::size_t I, typename Transport>
        requires is_select_v<protocol> && WriteTransport<Transport, resource_type, std::size_t>
                  && ChannelPole<resource_type> && (!std::is_same_v<Transport, no_label_t>)
    void select(Transport) && = delete(
        "[Pole_Owns_Its_Transport] a select over a pole of a channel puts no label on the wire, because the pole "
        "table of its channel (fixy/session/Admission.h) moves no label word.  Call select<I>(no_label) on a pole that "
        "states the Local network.  A select with a label needs a pole table that moves the label word.");

    template <std::size_t I, typename Transport>
        requires is_select_v<protocol> && (!WriteShape<Transport, resource_type, std::size_t>)
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
        requires is_offer_v<protocol> && (!ChannelPole<resource_type>)
              && ReadTransport<Reader, resource_type, std::size_t>
    constexpr auto branch(Reader reader, Handler handler) && {
        return RecordingDoor::branch(std::move(*this), std::move(reader), std::move(handler));
    }

    template <typename Reader, typename Handler>
        requires is_offer_v<protocol> && ChannelPole<resource_type>
    void branch(Reader, Handler) && = delete(
        "[Pole_Owns_Its_Transport] a branch over a pole of a channel reads a label word, and the pole table of its "
        "channel (fixy/session/Admission.h) moves no label word: a select over a pole puts no label on the wire.  The "
        "branch needs a pole table that moves the label word.  A test picks a branch with pick_local<I>(ctx).");

    template <typename Reader, typename Handler>
        requires is_offer_v<protocol> && (!ChannelPole<resource_type>)
                  && (!ReadShape<Reader, resource_type, std::size_t>)
    void branch(Reader, Handler) && = delete("[Transport_Shape] a read of a word either polls, as "
                                             "fixy::Option<std::size_t>(Resource&), or declares its wait, as "
                                             "std::size_t(Resource&, fixy::session::watch::wait_scope&).  A read of "
                                             "another shape can wait where the watch of fixy/session/Watch.h does not "
                                             "see it.");

    // The close gives what the close of the inner handle gives: the Resource,
    // or a Record of the Resource and the token of each tag that the End
    // handle holds.
    template <typename P = protocol>
        requires is_terminal_state_v<P> && detail::recording::inner_can_close<Inner>
    [[nodiscard]] constexpr typename detail::recording::close_of<Inner>::type close() && {
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

// The mint takes the one writer of a log (mint_session_event_writer of
// fixy/session/EventLog.h), so each recorded endpoint writes its own log.
template <typename H>
    requires RecordableHandle<H>
[[nodiscard]] constexpr auto mint_recorded_session(H handle, SessionEventWriter writer, RoleTagId self,
                                                   RoleTagId peer) noexcept {
    return RecordingDoor::make<H>(std::move(handle), std::move(writer), self, peer);
}

// A log alone gives no right to append: two recorders on one log append
// from two threads with no order.
template <typename H>
void mint_recorded_session(H, SessionEventLog&, RoleTagId, RoleTagId) =
    delete("[Event_Log_Needs_Its_Writer] mint_recorded_session takes the one writer of a log, not the log.  Mint the "
           "writer with mint_session_event_writer, and give each recorded endpoint a log of its own.");

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
