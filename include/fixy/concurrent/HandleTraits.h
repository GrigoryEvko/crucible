#pragma once

// Which pole of a channel a handle is, read off the member it exposes.
//
// Four shapes, one per pole.  A consumer drains with try_pop, a producer
// fills with try_push, a single-writer publisher writes with publish and
// its reader reads with load.  Each predicate also recovers the payload
// type, which is why detection goes through the address of the member
// rather than through a call expression: a call needs the payload type up
// front, and recovering it is the point.  A load that waits takes only a
// context, so a call with a witness context names no payload, and the reader
// pole also reads that shape from such a call.
//
// The predicates sit here, beside the channels whose handles they
// examine, under the plain names.
//
// The four decompositions are deliberately not folded into one
// parameterized shape.  What they share is the void_t detection and the
// mutual exclusion; what differs is the signature each pole must match,
// which is the whole content.  A fold would parameterize over exactly
// the part that carries the meaning.

#include <fixy/Ctx.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Linear.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

namespace detail {

// The class whose doors a shape trait reads: the end of a frame handle,
// which names it as frame_end_type, or the type itself.  A frame handle shows
// the doors of its end through forwarders, which are templates, so the
// address of a door of the handle names no one function.
template <typename T, typename = void>
struct door_source {
    using type = T;
};

template <typename T>
struct door_source<T, std::void_t<typename T::frame_end_type>> {
    using type = typename T::frame_end_type;
};

template <typename T>
using door_source_t = typename door_source<T>::type;

// ── the consumer pole: Option<P> try_pop() ──────────────────────────
//
// A merge pole, such as the consumer of an MPSC channel, takes the bare
// tag of Nondet: Option<P> try_pop(cap::Nondet).  The order of its items
// depends on the schedule, so a driver of the pole gives the tag from a
// context whose row holds Nondet.

template <typename M>
struct consumer_signature_decomp {
    static constexpr bool matches = false;
    static constexpr bool is_merge = false;
    using payload = void;
};

template <typename C, typename P>
struct consumer_signature_decomp<::foundation::core::Option<P> (C::*)()> {
    static constexpr bool matches = true;
    static constexpr bool is_merge = false;
    using payload = P;
};

template <typename C, typename P>
struct consumer_signature_decomp<::foundation::core::Option<P> (C::*)() noexcept> {
    static constexpr bool matches = true;
    static constexpr bool is_merge = false;
    using payload = P;
};

template <typename C, typename P>
struct consumer_signature_decomp<::foundation::core::Option<P> (C::*)(::foundation::effects::cap::Nondet) noexcept> {
    static constexpr bool matches = true;
    static constexpr bool is_merge = true;
    using payload = P;
};

template <typename T, typename = void>
struct try_pop_shape {
    static constexpr bool matches = false;
    static constexpr bool is_merge = false;
    using payload = void;
};

template <typename T>
struct try_pop_shape<T, std::void_t<decltype(&door_source_t<T>::try_pop)>> {
    using mptr_t = decltype(&door_source_t<T>::try_pop);
    using decomp = consumer_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches;
    static constexpr bool is_merge = decomp::is_merge;
    using payload = typename decomp::payload;
};

// ── the producer pole: bool try_push(P const&) or Option<P> try_push(P&&) ─

template <typename M>
struct producer_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct producer_signature_decomp<bool (C::*)(P const&)> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct producer_signature_decomp<bool (C::*)(P const&) noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

// The producer of an owner, such as a Box: Option<P> try_push(P&&).  The push
// takes the owner, and a full channel gives it back in the Option.
template <typename C, typename P>
struct producer_signature_decomp<::foundation::core::Option<P> (C::*)(P&&) noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename T, typename = void>
struct try_push_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct try_push_shape<T, std::void_t<decltype(&door_source_t<T>::try_push)>> {
    using mptr_t = decltype(&door_source_t<T>::try_push);
    using decomp = producer_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches;
    using payload = typename decomp::payload;
};

// ── the single-writer poles: void publish(P const&) / P load() const ─

template <typename M>
struct publish_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct publish_signature_decomp<void (C::*)(P const&)> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct publish_signature_decomp<void (C::*)(P const&) noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename T, typename = void>
struct publish_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct publish_shape<T, std::void_t<decltype(&door_source_t<T>::publish)>> {
    using mptr_t = decltype(&door_source_t<T>::publish);
    using decomp = publish_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches;
    using payload = typename decomp::payload;
};

// ── the reader pole: P load() const, or P load(ctx) const ───────────
//
// A reader whose load waits for nothing has the plain shape.  The load of a
// seqlock spins with no exit while a write is in flight
// (fixy/concurrent/AtomicSnapshot.h), so that reader takes a context that
// owns Div, and its load is a template over that context.  The address of a
// template names no one function, so the second shape is read from a call on
// a const reader with a witness context that owns Div.  The payload is the
// type of the result.

template <typename M>
struct load_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct load_signature_decomp<P (C::*)() const> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct load_signature_decomp<P (C::*)() const noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename T, typename = void>
struct plain_load_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct plain_load_shape<T, std::void_t<decltype(&door_source_t<T>::load)>> {
    using decomp = load_signature_decomp<decltype(&door_source_t<T>::load)>;
    static constexpr bool matches = decomp::matches && !std::is_void_v<typename decomp::payload>;
    using payload = typename decomp::payload;
};

using load_witness_ctx = ::fixy::BgLoadCtx;

template <typename T, typename = void>
struct ctx_load_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct ctx_load_shape<T,
                      std::void_t<decltype(std::declval<T const&>().load(std::declval<load_witness_ctx const&>()))>> {
    using payload = decltype(std::declval<T const&>().load(std::declval<load_witness_ctx const&>()));
    static constexpr bool matches = !std::is_void_v<payload>;
};

template <typename T>
struct load_shape {
    static constexpr bool matches = plain_load_shape<T>::matches || ctx_load_shape<T>::matches;
    using payload = std::conditional_t<plain_load_shape<T>::matches, typename plain_load_shape<T>::payload,
                                       typename ctx_load_shape<T>::payload>;
};

// ── the exclusions ──────────────────────────────────────────────────
//
// A type carrying both members of a pair has no determined pole, so
// each predicate names the member that would contradict it.

template <typename T, typename = void>
struct has_try_push : std::false_type {};

template <typename T>
struct has_try_push<T, std::void_t<decltype(&door_source_t<T>::try_push)>> : std::true_type {};

template <typename T, typename = void>
struct has_try_pop : std::false_type {};

template <typename T>
struct has_try_pop<T, std::void_t<decltype(&door_source_t<T>::try_pop)>> : std::true_type {};

template <typename T, typename = void>
struct has_publish : std::false_type {};

template <typename T>
struct has_publish<T, std::void_t<decltype(&door_source_t<T>::publish)>> : std::true_type {};

// A member named load of any shape: a plain member, or a load of the reader
// pole that is a template over its context.  The probe is part of has_load,
// and the armed cell of has_load covers it.
template <typename T, typename = void>
struct load_member_probe {
    static constexpr bool found = false;
};

template <typename T>
struct load_member_probe<T, std::void_t<decltype(&door_source_t<T>::load)>> {
    static constexpr bool found = true;
};

template <typename T>
struct has_load : std::bool_constant<load_member_probe<T>::found || ctx_load_shape<T>::matches> {};

}  // namespace detail

// The Option return is what carries the empty signal, so a try_pop that
// returns bool and fills an out-parameter is a different endpoint shape and is
// deliberately not admitted. A type exposing both try_pop and try_push is also
// rejected, because nothing then decides which pole it is.

template <typename T>
inline constexpr bool is_consumer_handle_v =
    detail::try_pop_shape<std::remove_cvref_t<T>>::matches && !detail::has_try_push<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsConsumerHandle = is_consumer_handle_v<T>;

template <typename T>
    requires is_consumer_handle_v<T>
using consumer_handle_value_t = typename detail::try_pop_shape<std::remove_cvref_t<T>>::payload;

// True for a consumer whose pop is a merge and takes the bare tag of Nondet.
template <typename T>
inline constexpr bool is_merge_consumer_handle_v =
    is_consumer_handle_v<T> && detail::try_pop_shape<std::remove_cvref_t<T>>::is_merge;

// Detection goes through the address of the member rather than through an
// expression such as `t.try_push(v)`, because that form needs the payload type
// up front and this predicate exists to recover it. The cost is that an
// overloaded try_push is rejected: the address of an overload set is
// ill-formed. A type exposing both try_push and try_pop is also rejected,
// because nothing then decides which pole it is.

template <typename T>
inline constexpr bool is_producer_handle_v =
    detail::try_push_shape<std::remove_cvref_t<T>>::matches && !detail::has_try_pop<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsProducerHandle = is_producer_handle_v<T>;

template <typename T>
    requires is_producer_handle_v<T>
using producer_handle_value_t = typename detail::try_push_shape<std::remove_cvref_t<T>>::payload;

// The void return on publish is part of the shape, not an accident. A publish
// that returns bool belongs to a capacity-limited endpoint that can refuse a
// value, which routes differently, so it is not admitted here. The two
// predicates exclude each other: a type carrying both members has no
// determined pole.

template <typename T>
inline constexpr bool is_swmr_writer_v =
    detail::publish_shape<std::remove_cvref_t<T>>::matches && !detail::has_load<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSwmrWriter = is_swmr_writer_v<T>;

template <typename T>
    requires is_swmr_writer_v<T>
using swmr_writer_value_t = typename detail::publish_shape<std::remove_cvref_t<T>>::payload;

template <typename T>
inline constexpr bool is_swmr_reader_v =
    detail::load_shape<std::remove_cvref_t<T>>::matches && !detail::has_publish<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSwmrReader = is_swmr_reader_v<T>;

template <typename T>
    requires is_swmr_reader_v<T>
using swmr_reader_value_t = typename detail::load_shape<std::remove_cvref_t<T>>::payload;

// The channel a handle acts on.  Each channel handle names it, so a place
// that joins two handles can ask that they meet on one channel: a stage
// that writes channel A and a stage that drains channel B agree in payload
// and never exchange a value.  A handle that names no channel meets no
// other handle, so the question fails closed.
template <typename T>
concept NamesItsChannel = requires { typename std::remove_cvref_t<T>::channel_type; };

template <typename T>
    requires NamesItsChannel<T>
using handle_channel_t = typename std::remove_cvref_t<T>::channel_type;

// A handle that reports the identity of the channel instance it acts on.
// The type of a channel carries the brand of its root, so two channels of
// two root sites are two types.  One root site that runs two times makes
// two channels of one type, and only the identity tells them apart.
template <typename T>
concept ReportsChannelIdentity = NamesItsChannel<T> && requires(std::remove_cvref_t<T> const& handle) {
    { handle.channel_identity() } noexcept -> std::same_as<::foundation::ChannelIdentity<handle_channel_t<T>>>;
};

// The two handles name one channel type.  Each of them reports its
// identity, or none of them does.  A pipeline mint then compares the two
// identities.
template <typename Producer, typename Consumer>
concept HandlesShareChannel = NamesItsChannel<Producer> && NamesItsChannel<Consumer>
                           && std::is_same_v<handle_channel_t<Producer>, handle_channel_t<Consumer>>
                           && ReportsChannelIdentity<Producer> == ReportsChannelIdentity<Consumer>;

// True when a handle type states the capacity of its channel: the count of
// the messages that the channel holds.
template <class Handle>
concept StatesHandleCapacity = requires { static_cast<std::size_t>(Handle::channel_capacity); };

// The two handles of one channel agree on its capacity: each of them states
// the same capacity.  A handle that states none agrees with no handle, so no
// capacity of a channel comes from one of its handles alone, and no channel
// between two handles has an unknown capacity.  A rendezvous states 0.
template <typename Producer, typename Consumer>
concept HandlesAgreeOnCapacity =
    StatesHandleCapacity<Producer> && StatesHandleCapacity<Consumer>
    && static_cast<std::size_t>(Producer::channel_capacity) == static_cast<std::size_t>(Consumer::channel_capacity);

// The gates of a frame door of a channel, which gives its body each handle
// by value on the scope brand of the call (the frame doors of
// fixy/concurrent/PermissionedChannel.h and fixy/channels/SwmrSession.h).

// A body of a frame door: a closure that the caller gives up, which takes each
// handle by value and does not throw.  A frame door runs its body one time,
// so the door owns the body and calls it through the once door call_once of
// foundation/Linear.h.  A body can then hold a linear value by value, such as
// a handle or a token of an outer frame, and the rule must-consume reads the
// consume of each such value in the body.  Body is the deduced type of a
// forwarding parameter, so a named body deduces a reference.
template <class Body, class... Handles>
concept HandleFrameBody =
    ::foundation::linear::OnceClosure<Body, Handles...> && std::is_nothrow_invocable_v<Body&&, Handles...>;

// A body of a frame door that the caller names: a second name of the closure
// could call it again.  The named twin of each door binds to it.
template <class Body, class... Handles>
concept HandleNamedFrameBody =
    std::is_lvalue_reference_v<Body> && std::is_invocable_v<std::remove_reference_t<Body>&, Handles...>;

// A body whose result leaves the frame: void, or a value that holds no borrow
// and names no brand of the frame (FrameResult of foundation/core/Choice.h).
// A handle in the result names the scope brand, also inside a closure or a
// sum, so such a result does not leave.
template <class Body, class Scope, class... Handles>
concept HandleFrameLeaves = HandleFrameBody<Body, Handles...>
                         && ::foundation::core::FrameResult<std::invoke_result_t<Body&&, Handles...>, Scope>;

// A body that a door can end with no call: it holds no linear value, or each
// linear value that it holds states its own end (end_unrun_body of
// foundation/Linear.h).
template <class Body>
concept HandleLendFrameBody =
    ::foundation::linear::HoldsNoLinearValue<Body> || ::foundation::linear::UnrunBody<std::remove_cvref_t<Body>>;

// True when a lend door gives the body back on a refused lend (the lends): the
// body holds a linear value and no borrow, so a Result can hold it and the
// caller can keep it.
template <class Body>
concept LendGivesBodyBack =
    !::foundation::linear::HoldsNoLinearValue<Body> && ::foundation::core::HoldsNoBorrow<std::remove_cvref_t<Body>>;

// A body of a lend door, such as the producer door of a merge channel (the
// lends).  The lend fails while the root of the end is out of the pool, and
// then the door calls no body.  A body that holds a linear value and no
// borrow comes back to the caller (LendGivesBodyBack).  Each other body ends
// in the door (HandleLendFrameBody): a body that holds no linear value, or a
// borrowing body whose linear values each state their own end and end
// through their own drop door.  A value that owes a consume, such as a live
// fixy::Linear, states no end of its own (owes_consume of
// foundation/Linear.h), so no such value ends in the door, and a borrowing
// body that holds one does not compile.
template <class Body>
concept LendDoorBody = LendGivesBodyBack<Body> || HandleLendFrameBody<Body>;

// The refusal of a lend door to a body that it gives back (LendGivesBodyBack):
// the body, which no door called, so no door drops a linear value in silence.
// The caller calls the body at a later lend, or ends it through end_unrun_body
// of foundation/Linear.h with a reason.  A body need not have a default
// state, so the door gives the member its value.
template <class Body>
struct RefusedLend {
    Body body;
};

// A frame handle of the end of a role of a channel: the handle that a frame
// door of the channel gives on the scope brand that the handle names.  A
// channel family names its handles through the member alias templates
// producer_handle and consumer_handle.
template <class Pole, class Channel>
concept FrameProducerHandleOf = requires {
    typename Pole::scope_type;
} && std::same_as<Pole, typename Channel::template producer_handle<typename Pole::scope_type>>;
template <class Pole, class Channel>
concept FrameConsumerHandleOf = requires {
    typename Pole::scope_type;
} && std::same_as<Pole, typename Channel::template consumer_handle<typename Pole::scope_type>>;

// The producer pole of a channel: its producer end, or a frame handle of that
// end.  A gate of a session reads the end, which runs the protocol of each of
// its handles, and a session runs over a handle.  No code outside the frame
// doors gets a value of an end.
template <class Pole, class Channel>
concept ProducerPoleOf = std::same_as<Pole, typename Channel::ProducerEnd> || FrameProducerHandleOf<Pole, Channel>;
template <class Pole, class Channel>
concept ConsumerPoleOf = std::same_as<Pole, typename Channel::ConsumerEnd> || FrameConsumerHandleOf<Pole, Channel>;

namespace detail {

// The value of a frame door that can open no end: the result of the body, or
// Unit for a body that gives void.
template <class Given>
using handle_frame_value_t = std::conditional_t<std::is_void_v<Given>, ::foundation::core::Unit, Given>;

// The end of a body that no door called (HandleLendFrameBody).  A body that
// holds a linear value ends at end_unrun_body, so each such value ends at its
// own end.  Each other body ends with no work.
template <class Body>
    requires HandleLendFrameBody<Body>
constexpr void end_unlent_body_(Body&& body) noexcept {
    if constexpr (::foundation::linear::LinearValue<std::remove_cvref_t<Body>>) {
        ::foundation::linear::end_unrun_body(static_cast<Body&&>(body),
                                             ::foundation::linear::UnrunReason::lend_refused);
    } else {
        static_cast<void>(body);
    }
}

// The result of a lend door (LendDoorBody).  For a body that the door gives
// back (LendGivesBodyBack), a Result whose error is a RefusedLend with the
// body.  For each other body, an Option of the value of the body, which is
// none when the door lent no end.  A Result refuses an error that holds a
// borrow, so the choice is a specialization, not a conditional of the two
// types, and the Result names only a body that holds no borrow.
template <bool GivesBodyBack, class Body, class Value>
struct lend_result_ {
    using type = ::foundation::core::Option<Value>;
};

template <class Body, class Value>
struct lend_result_<true, Body, Value> {
    using type = ::foundation::core::Result<Value, RefusedLend<std::remove_cvref_t<Body>>>;
};

template <class Body, class Given>
using lend_result_t = typename lend_result_<LendGivesBodyBack<Body>, Body, handle_frame_value_t<Given>>::type;

// The result of a lend door that lent no end: the body back, or none after
// the end of the body in the door (end_unlent_body_).
template <class Body, class Given>
[[nodiscard]] constexpr lend_result_t<Body, Given> refuse_lend_(Body&& body) noexcept {
    if constexpr (LendGivesBodyBack<Body>) {
        return ::foundation::core::err(RefusedLend<std::remove_cvref_t<Body>>{static_cast<Body&&>(body)});
    } else {
        end_unlent_body_(static_cast<Body&&>(body));
        return ::foundation::core::none;
    }
}

// The result of a lend door that called the body: the value of the body.
template <class Body, class Given, class Value>
[[nodiscard]] constexpr lend_result_t<Body, Given> lent_value_(Value&& value) noexcept {
    if constexpr (LendGivesBodyBack<Body>) {
        return lend_result_t<Body, Given>{static_cast<Value&&>(value)};
    } else {
        return ::foundation::core::Option<handle_frame_value_t<Given>>::some(static_cast<Value&&>(value));
    }
}

}  // namespace detail

}  // namespace fixy::concurrent

// The reason of the named twin of each frame door.
#define CRUCIBLE_HANDLE_FRAME_NAMED_BODY_REFUSAL                                                           \
    "a frame door of a channel owns its body and calls it one time, and a named body keeps a second name " \
    "that can call it again: pass the body with std::move, or write it at the call"
