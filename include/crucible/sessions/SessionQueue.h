#pragma once

// The contents of a channel buffer, as a type.
//
// Once a send no longer blocks until its receive, the messages already
// in flight are part of the protocol's state.  Two peers at the same
// pair of local types are in different situations depending on what is
// still sitting in the buffer between them, so that buffer has to be
// tracked alongside the local types rather than left implicit.
//
// A queued message is identified by its sender, its recipient and its
// payload type.  There is no label field, matching the choice
// combinators, which identify a branch by position.  A protocol that
// wants named messages gives each name its own payload type.
//
// A queue state is either a buffer of messages or the mark left when
// the recipient has crashed and nothing more can be delivered.
//
// Bounding a queue is what keeps questions about it answerable.
// Subtyping between asynchronous protocols is undecidable once buffers
// may grow without limit, and every buffer that actually exists in this
// system has a fixed capacity, so the bound is both true and necessary.

#include <crucible/Platform.h>
#include <crucible/sessions/SessionCrash.h>

#include <cstddef>
#include <type_traits>

namespace crucible::safety::proto {

namespace detail::queue {

// Templated so that the assertions below fire when a caller asks for
// the empty case, rather than the moment the header is read.
template <typename...>
inline constexpr bool dependent_false_v = false;

}  // namespace detail::queue

template <typename From, typename To, typename Payload>
struct QueuedMsg {
    using from = From;
    using to = To;
    using payload = Payload;
};

template <typename... Msgs>
struct Queue {
    static constexpr std::size_t size = sizeof...(Msgs);
};

using EmptyQueue = Queue<>;

template <typename T>
struct is_queue : std::false_type {};
template <typename... Ms>
struct is_queue<Queue<Ms...>> : std::true_type {};
template <typename T>
inline constexpr bool is_queue_v = is_queue<T>::value;

template <typename T>
struct is_queued_msg : std::false_type {};
template <typename F, typename T, typename P>
struct is_queued_msg<QueuedMsg<F, T, P>> : std::true_type {};
template <typename T>
inline constexpr bool is_queued_msg_v = is_queued_msg<T>::value;

template <typename Q>
inline constexpr std::size_t queue_size_v = Q::size;

template <typename Q>
inline constexpr bool is_queue_empty_v = (queue_size_v<Q> == 0);

template <typename Q, typename M>
struct EnqueueQueue;

template <typename... Ms, typename M>
struct EnqueueQueue<Queue<Ms...>, M> {
    using type = Queue<Ms..., M>;
};

template <typename Q, typename M>
using enqueue_queue_t = typename EnqueueQueue<Q, M>::type;

template <typename Q>
struct HeadQueue {
    static_assert(detail::queue::dependent_false_v<Q>, "crucible::session::diagnostic [Queue_Empty_Dequeue]: "
                                                       "head_queue_t<Q>: Q is empty or is not a Queue<>.  Use "
                                                       "is_queue_empty_v<Q> to test before dequeue.  A receive on an "
                                                       "empty queue is a peer that has yet to send, which blocks at "
                                                       "runtime rather than failing, so the protocol rules should "
                                                       "have refused this path before it reached here.");
};

template <typename Head, typename... Rest>
struct HeadQueue<Queue<Head, Rest...>> {
    using type = Head;
};

template <typename Q>
using head_queue_t = typename HeadQueue<Q>::type;

template <typename Q>
struct TailQueue {
    static_assert(detail::queue::dependent_false_v<Q>, "crucible::session::diagnostic [Queue_Empty_Dequeue]: "
                                                       "tail_queue_t<Q>: Q is empty or is not a Queue<>.  Use "
                                                       "is_queue_empty_v<Q> to test before dequeue.");
};

template <typename Head, typename... Rest>
struct TailQueue<Queue<Head, Rest...>> {
    using type = Queue<Rest...>;
};

template <typename Q>
using tail_queue_t = typename TailQueue<Q>::type;

template <typename Q>
using dequeue_queue_t = tail_queue_t<Q>;

template <typename Q, typename From, typename To>
struct queue_contains : std::false_type {};

template <typename... Ms, typename From, typename To>
struct queue_contains<Queue<Ms...>, From, To>
    : std::bool_constant<((std::is_same_v<typename Ms::from, From> && std::is_same_v<typename Ms::to, To>) || ...)> {};

template <typename Q, typename From, typename To>
inline constexpr bool queue_contains_v = queue_contains<Q, From, To>::value;

template <typename Q, typename From, typename To>
struct count_matching;

template <typename From, typename To>
struct count_matching<Queue<>, From, To> : std::integral_constant<std::size_t, 0> {};

template <typename... Ms, typename From, typename To>
struct count_matching<Queue<Ms...>, From, To>
    : std::integral_constant<std::size_t,
                             ((std::is_same_v<typename Ms::from, From> && std::is_same_v<typename Ms::to, To>
                                   ? std::size_t{1}
                                   : std::size_t{0})
                              + ... + std::size_t{0})> {};

template <typename Q, typename From, typename To>
inline constexpr std::size_t count_matching_v = count_matching<Q, From, To>::value;

// This bounds one queue state, not a protocol.  Showing that a
// protocol never exceeds a capacity means checking every state it can
// reach, which this predicate alone does not do.

template <typename Q, std::size_t MaxCap>
inline constexpr bool is_bounded_queue_v = (queue_size_v<Q> <= MaxCap);

template <typename T>
struct is_unavailable_queue : std::false_type {};

template <typename PeerTag>
struct is_unavailable_queue<UnavailableQueue<PeerTag>> : std::true_type {};

template <typename T>
inline constexpr bool is_unavailable_queue_v = is_unavailable_queue<T>::value;

// The two forms above are the whole state space for a channel buffer.
template <typename T>
inline constexpr bool is_queue_state_v = is_queue_v<T> || is_unavailable_queue_v<T>;

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::queue::queue_self_test {

struct Alice {};
struct Bob {};
struct Carol {};

struct Ping {};
struct Ack {};
struct Data {};

using PingAliceBob = QueuedMsg<Alice, Bob, Ping>;
using AckBobAlice = QueuedMsg<Bob, Alice, Ack>;
using DataBobCarol = QueuedMsg<Bob, Carol, Data>;

static_assert(is_queued_msg_v<PingAliceBob>);
static_assert(!is_queued_msg_v<int>);
static_assert(!is_queued_msg_v<Queue<>>);

static_assert(std::is_same_v<typename PingAliceBob::from, Alice>);
static_assert(std::is_same_v<typename PingAliceBob::to, Bob>);
static_assert(std::is_same_v<typename PingAliceBob::payload, Ping>);

static_assert(is_queue_v<EmptyQueue>);
static_assert(is_queue_v<Queue<PingAliceBob>>);
static_assert(is_queue_v<Queue<PingAliceBob, AckBobAlice>>);
static_assert(!is_queue_v<PingAliceBob>);
static_assert(!is_queue_v<int>);

static_assert(queue_size_v<EmptyQueue> == 0);
static_assert(queue_size_v<Queue<PingAliceBob>> == 1);
static_assert(queue_size_v<Queue<PingAliceBob, AckBobAlice>> == 2);

static_assert(is_queue_empty_v<EmptyQueue>);
static_assert(!is_queue_empty_v<Queue<PingAliceBob>>);

static_assert(std::is_same_v<enqueue_queue_t<EmptyQueue, PingAliceBob>, Queue<PingAliceBob>>);

static_assert(std::is_same_v<enqueue_queue_t<Queue<PingAliceBob>, AckBobAlice>, Queue<PingAliceBob, AckBobAlice>>);

static_assert(std::is_same_v<enqueue_queue_t<enqueue_queue_t<EmptyQueue, PingAliceBob>, AckBobAlice>,
                             Queue<PingAliceBob, AckBobAlice>>);

static_assert(std::is_same_v<head_queue_t<Queue<PingAliceBob, AckBobAlice>>, PingAliceBob>);

static_assert(std::is_same_v<tail_queue_t<Queue<PingAliceBob, AckBobAlice>>, Queue<AckBobAlice>>);

static_assert(
    std::is_same_v<dequeue_queue_t<Queue<PingAliceBob, AckBobAlice>>, tail_queue_t<Queue<PingAliceBob, AckBobAlice>>>);

static_assert(std::is_same_v<head_queue_t<Queue<PingAliceBob>>, PingAliceBob>);

static_assert(std::is_same_v<tail_queue_t<Queue<PingAliceBob>>, EmptyQueue>);

static_assert(std::is_same_v<dequeue_queue_t<enqueue_queue_t<EmptyQueue, PingAliceBob>>, EmptyQueue>);

static_assert(!queue_contains_v<EmptyQueue, Alice, Bob>);

static_assert(queue_contains_v<Queue<PingAliceBob>, Alice, Bob>);
static_assert(!queue_contains_v<Queue<PingAliceBob>, Bob, Alice>);
static_assert(!queue_contains_v<Queue<PingAliceBob>, Alice, Carol>);

using MultiQ = Queue<PingAliceBob, AckBobAlice, DataBobCarol>;
static_assert(queue_contains_v<MultiQ, Alice, Bob>);
static_assert(queue_contains_v<MultiQ, Bob, Alice>);
static_assert(queue_contains_v<MultiQ, Bob, Carol>);
static_assert(!queue_contains_v<MultiQ, Alice, Carol>);
static_assert(!queue_contains_v<MultiQ, Carol, Alice>);

static_assert(count_matching_v<EmptyQueue, Alice, Bob> == 0);

using TwoOnSameChannel = Queue<QueuedMsg<Alice, Bob, Ping>, QueuedMsg<Alice, Bob, Ping>>;
static_assert(count_matching_v<TwoOnSameChannel, Alice, Bob> == 2);
static_assert(count_matching_v<TwoOnSameChannel, Bob, Alice> == 0);

static_assert(count_matching_v<MultiQ, Alice, Bob> == 1);
static_assert(count_matching_v<MultiQ, Bob, Alice> == 1);
static_assert(count_matching_v<MultiQ, Bob, Carol> == 1);

static_assert(is_bounded_queue_v<EmptyQueue, 0>);
static_assert(is_bounded_queue_v<EmptyQueue, 10>);
static_assert(is_bounded_queue_v<Queue<PingAliceBob>, 1>);
static_assert(is_bounded_queue_v<Queue<PingAliceBob, AckBobAlice>, 2>);
static_assert(!is_bounded_queue_v<Queue<PingAliceBob>, 0>);
static_assert(!is_bounded_queue_v<Queue<PingAliceBob, AckBobAlice>, 1>);

static_assert(is_unavailable_queue_v<UnavailableQueue<Alice>>);
static_assert(is_unavailable_queue_v<UnavailableQueue<Bob>>);
static_assert(!is_unavailable_queue_v<EmptyQueue>);
static_assert(!is_unavailable_queue_v<Queue<PingAliceBob>>);
static_assert(!is_unavailable_queue_v<PingAliceBob>);
static_assert(!is_unavailable_queue_v<int>);

static_assert(is_queue_state_v<EmptyQueue>);
static_assert(is_queue_state_v<Queue<PingAliceBob>>);
static_assert(is_queue_state_v<UnavailableQueue<Alice>>);
static_assert(!is_queue_state_v<PingAliceBob>);
static_assert(!is_queue_state_v<int>);

static_assert(queue_size_v<enqueue_queue_t<Queue<PingAliceBob>, AckBobAlice>> == queue_size_v<Queue<PingAliceBob>> + 1);

static_assert(queue_size_v<dequeue_queue_t<Queue<PingAliceBob, AckBobAlice>>> == queue_size_v < Queue < PingAliceBob,
              AckBobAlice >> -1);

static_assert(count_matching_v<enqueue_queue_t<Queue<PingAliceBob>, AckBobAlice>, Alice, Bob> == 1);
static_assert(count_matching_v<enqueue_queue_t<Queue<PingAliceBob>, AckBobAlice>, Bob, Alice> == 1);

static_assert(count_matching_v<dequeue_queue_t<Queue<PingAliceBob, AckBobAlice>>, Alice, Bob> == 0);
static_assert(count_matching_v<dequeue_queue_t<Queue<PingAliceBob, AckBobAlice>>, Bob, Alice> == 1);

using FiveChain = enqueue_queue_t<
    enqueue_queue_t<enqueue_queue_t<enqueue_queue_t<enqueue_queue_t<EmptyQueue, QueuedMsg<Alice, Bob, Ping>>,
                                                    QueuedMsg<Alice, Bob, Ping>>,
                                    QueuedMsg<Bob, Alice, Ack>>,
                    QueuedMsg<Alice, Carol, Data>>,
    QueuedMsg<Alice, Bob, Ping>>;
static_assert(queue_size_v<FiveChain> == 5);
static_assert(count_matching_v<FiveChain, Alice, Bob> == 3);
static_assert(count_matching_v<FiveChain, Bob, Alice> == 1);
static_assert(count_matching_v<FiveChain, Alice, Carol> == 1);
static_assert(is_bounded_queue_v<FiveChain, 5>);
static_assert(is_bounded_queue_v<FiveChain, 100>);
static_assert(!is_bounded_queue_v<FiveChain, 4>);

using Round = dequeue_queue_t<enqueue_queue_t<Queue<PingAliceBob, AckBobAlice>, DataBobCarol>>;
static_assert(std::is_same_v<Round, Queue<AckBobAlice, DataBobCarol>>);

}  // namespace detail::queue::queue_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
