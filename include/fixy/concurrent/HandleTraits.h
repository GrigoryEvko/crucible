#pragma once

// Which pole of a channel a handle is, read off the member it exposes.
//
// Four shapes, one per pole.  A consumer drains with try_pop, a producer
// fills with try_push, a single-writer publisher writes with publish and
// its reader reads with load.  Each predicate also recovers the payload
// type, which is why detection goes through the address of the member
// rather than through a call expression: a call needs the payload type up
// front, and recovering it is the point.
//
// The predicates sit here, beside the channels whose handles they
// examine, under the plain names.
//
// The four decompositions are deliberately not folded into one
// parameterized shape.  What they share is the void_t detection and the
// mutual exclusion; what differs is the signature each pole must match,
// which is the whole content.  A fold would parameterize over exactly
// the part that carries the meaning.

#include <foundation/ChannelBinding.h>

#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace fixy::concurrent {

namespace detail {

// ── the consumer pole: std::optional<P> try_pop() ───────────────────

template <typename M>
struct consumer_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct consumer_signature_decomp<std::optional<P> (C::*)()> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct consumer_signature_decomp<std::optional<P> (C::*)() noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename T, typename = void>
struct try_pop_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct try_pop_shape<T, std::void_t<decltype(&T::try_pop)>> {
    using mptr_t = decltype(&T::try_pop);
    using decomp = consumer_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches;
    using payload = typename decomp::payload;
};

// ── the producer pole: bool try_push(P const&) ──────────────────────

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
struct load_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct load_shape<T, std::void_t<decltype(&T::load)>> {
    using mptr_t = decltype(&T::load);
    using decomp = load_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches && !std::is_void_v<typename decomp::payload>;
    using payload = typename decomp::payload;
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

template <typename T, typename = void>
struct has_load : std::false_type {};

template <typename T>
struct has_load<T, std::void_t<decltype(&T::load)>> : std::true_type {};

}  // namespace detail

// The optional return is what carries the empty signal, so a try_pop that
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

}  // namespace fixy::concurrent
