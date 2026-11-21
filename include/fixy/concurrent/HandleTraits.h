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
#include <foundation/core/Choice.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

namespace detail {

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
struct try_pop_shape<T, std::void_t<decltype(&T::try_pop)>> {
    using mptr_t = decltype(&T::try_pop);
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
struct try_push_shape<T, std::void_t<decltype(&T::try_push)>> {
    using mptr_t = decltype(&T::try_push);
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
struct publish_shape<T, std::void_t<decltype(&T::publish)>> {
    using mptr_t = decltype(&T::publish);
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
struct plain_load_shape<T, std::void_t<decltype(&T::load)>> {
    using decomp = load_signature_decomp<decltype(&T::load)>;
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
struct has_try_push<T, std::void_t<decltype(&T::try_push)>> : std::true_type {};

template <typename T, typename = void>
struct has_try_pop : std::false_type {};

template <typename T>
struct has_try_pop<T, std::void_t<decltype(&T::try_pop)>> : std::true_type {};

template <typename T, typename = void>
struct has_publish : std::false_type {};

template <typename T>
struct has_publish<T, std::void_t<decltype(&T::publish)>> : std::true_type {};

// A member named load of any shape: a plain member, or a load of the reader
// pole that is a template over its context.  The probe is part of has_load,
// and the armed cell of has_load covers it.
template <typename T, typename = void>
struct load_member_probe {
    static constexpr bool found = false;
};

template <typename T>
struct load_member_probe<T, std::void_t<decltype(&T::load)>> {
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
// the same capacity, or none of them states one.  A handle that states a
// capacity and a handle that states none do not agree, so no capacity of a
// channel comes from one of its handles alone.
template <typename Producer, typename Consumer>
concept HandlesAgreeOnCapacity =
    (StatesHandleCapacity<Producer> && StatesHandleCapacity<Consumer>
     && static_cast<std::size_t>(Producer::channel_capacity) == static_cast<std::size_t>(Consumer::channel_capacity))
    || (!StatesHandleCapacity<Producer> && !StatesHandleCapacity<Consumer>);

}  // namespace fixy::concurrent
