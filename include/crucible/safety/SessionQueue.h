#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — L3 queue types σ (SEPLOG-I2, task #344)
//
// Under asynchronous session-type semantics, every point-to-point
// channel carries a BUFFER of pending messages: the sender enqueues
// (non-blocking) while the receiver dequeues.  The buffer's state is
// itself type-level information — a QUEUE TYPE σ — and reasoning
// about σ is what distinguishes asynchronous from synchronous session
// types.  PMY25 (async multiparty, precise subtyping) and GPPSY23
// (SISO decomposition) both rely on this layer as their runtime-state
// model.
//
// This header ships the STRUCTURE of queue types:
//
//     QueuedMsg<From, To, Payload>          a single in-flight message
//     Queue<Msgs...>                         a FIFO of pending messages
//     EmptyQueue = Queue<>
//
//     is_queue_v<T>                          shape predicate
//     queue_size_v<Q>                        message count
//     is_queue_empty_v<Q>                    queue_size == 0
//     enqueue_queue_t<Q, M>                  right-append
//     head_queue_t<Q>                        first message (compile
//                                             error if empty)
//     tail_queue_t<Q>                        all but first (compile
//                                             error if empty)
//     dequeue_queue_t<Q>                     alias for tail — the
//                                             consumer's view of
//                                             "pop the head"
//     queue_contains_v<Q, From, To>          any message from→to?
//     count_matching_v<Q, From, To>          how many from→to?
//     is_bounded_queue_v<Q, MaxCap>          queue_size <= MaxCap
//     is_unavailable_queue_v<T>              is T UnavailableQueue<_>?
//     is_queue_state_v<T>                    Queue<_> ∨ UnavailableQueue<_>
//
// ─── Positional encoding (no labels) ──────────────────────────────
//
// Our L1 Select/Offer combinators use POSITIONAL branches (branch
// index = implicit label).  QueuedMsg matches that encoding:  no
// Label parameter.  A QueuedMsg is identified by its (From, To,
// Payload) triple.  Protocol evolution that wants labels can encode
// them via distinct Payload types per "label".
//
// ─── Queue state: Queue<...> ∨ UnavailableQueue<Peer> ─────────────
//
// The full queue-state lattice is:
//
//     QueueState ::= Queue<M0, M1, ...>     ordinary FIFO of messages
//                  | UnavailableQueue<Peer>  receiver crashed (BHYZ23 ⊘)
//
// UnavailableQueue<Peer> lives in SessionCrash.h (task #347, already
// shipped).  We expose is_unavailable_queue_v<T> and is_queue_state_v<T>
// here so L7 async reasoning can discriminate between the two forms
// without needing both headers.
//
// ─── Bounded queues for decidability ──────────────────────────────
//
// Lange-Yoshida 2017 proved async subtyping is UNDECIDABLE with
// unbounded queues.  Crucible's runtime queues are all bounded
// (MpmcRing has 2N slots for N-capacity logical queue; CNTP posted-
// work queues have firmware caps).  is_bounded_queue_v<Q, MaxCap>
// is the type-level expression of that invariant — used by L7 to
// refuse protocols whose reachable states would overflow.
//
// ─── What this layer ships; what's deferred ───────────────────────
//
// Shipped:  structural queue operations (enqueue, head, tail, size,
// contains, count, bounded, unavailable detection).
//
// Deferred (come with L7 and L6.5):
//   * AsyncContext<Γ, QueueEntries...>       Γ + per-channel queue
//     states — composed type for L7 reduction.  Requires event and
//     reduction machinery not yet in place.
//   * ReduceAsyncContext<AsyncΓ, Event>      queue-aware reduction.
//     Splits Send into enqueue-only (sender's local type advances;
//     receiver's does not; σ gains a message) and Recv into dequeue-
//     only (receiver's local type advances; σ loses its head).
//   * is_balanced_plus_v<AsyncΓ, Cap>        PMY25 Def 6.1 — for
//     every reachable state, every queue's size ≤ Cap.  Requires
//     reachable-states BFS (L7).
//   * en-route transmissions                 PMY25 §3 — global-type
//     runtime nodes representing "sender committed but receiver
//     hasn't received".  Requires L4 global types.
//
// ─── References ───────────────────────────────────────────────────
//
//   Honda-Yoshida-Carbone 2008 / JACM16 — classical asynchronous
//     MPST with per-channel queue types σ; causality relations
//     ≺_II / ≺_IO / ≺_OO defined over queue-enqueue-dequeue events.
//   Lange-Yoshida 2017, "On the undecidability of asynchronous
//     session subtyping" — bounded buffers are what makes decidable
//     async subtyping possible.
//   Ghilezan-Pantović-Prokić-Scalas-Yoshida 2023 — SISO decomposition
//     on queue-typed runtime states.
//   Pischke-Masters-Yoshida 2025 — balanced+ invariant (Def 6.1)
//     over all reachable AsyncContext states.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/SessionCrash.h>

#include <cstddef>
#include <type_traits>

namespace crucible::safety::proto {

namespace detail::queue {

// "dependent_false" — always false, dependent on a template parameter
// so it fires only on instantiation (not at definition time).  Used
// in the empty-queue specialisations of head_queue_t / tail_queue_t
// to give a clear diagnostic instead of "no type named 'type'".
template <typename...>
inline constexpr bool dependent_false_v = false;

}  // namespace detail::queue

// ═════════════════════════════════════════════════════════════════════
// ── QueuedMsg<From, To, Payload> ───────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// A single in-flight message on the channel from `From` to `To`,
// carrying a Payload value-type.  Pure type marker; zero runtime
// footprint.  The nested aliases expose each field for metaprogramming.

template <typename From, typename To, typename Payload>
struct QueuedMsg {
    using from    = From;
    using to      = To;
    using payload = Payload;
};

// ═════════════════════════════════════════════════════════════════════
// ── Queue<Msgs...> ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// A FIFO sequence of queued messages.  Right-append semantics:
// new messages go to the END of the Msgs... pack; the HEAD is the
// first Msg (the oldest, the next to be dequeued).

template <typename... Msgs>
struct Queue {
    static constexpr std::size_t size = sizeof...(Msgs);
};

using EmptyQueue = Queue<>;

// ═════════════════════════════════════════════════════════════════════
// ── Shape and size traits ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T> struct is_queue : std::false_type {};
template <typename... Ms> struct is_queue<Queue<Ms...>> : std::true_type {};
template <typename T> inline constexpr bool is_queue_v = is_queue<T>::value;

template <typename T> struct is_queued_msg : std::false_type {};
template <typename F, typename T, typename P>
struct is_queued_msg<QueuedMsg<F, T, P>> : std::true_type {};
template <typename T>
inline constexpr bool is_queued_msg_v = is_queued_msg<T>::value;

template <typename Q>
inline constexpr std::size_t queue_size_v = Q::size;

template <typename Q>
inline constexpr bool is_queue_empty_v = (queue_size_v<Q> == 0);

// ═════════════════════════════════════════════════════════════════════
// ── enqueue_queue_t<Q, M> ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Append M to the END of Q.  Right-append — the FIFO sender's side.

template <typename Q, typename M>
struct EnqueueQueue;

template <typename... Ms, typename M>
struct EnqueueQueue<Queue<Ms...>, M> {
    using type = Queue<Ms..., M>;
};

template <typename Q, typename M>
using enqueue_queue_t = typename EnqueueQueue<Q, M>::type;

// ═════════════════════════════════════════════════════════════════════
// ── head_queue_t<Q> ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The FIRST message in Q (the oldest pending — the next one a
// receiver will dequeue).  Compile error on empty queue with a clean
// dependent-false diagnostic.
//
// Pattern: the PRIMARY template carries the dependent-false
// static_assert (its template parameter defers the assert to
// instantiation time).  The partial specialisation for non-empty
// Queue<Head, Rest...> is the success path and shadows the primary
// when applicable.  Instantiation with Queue<> matches no partial
// spec, falls back to primary, fires the diagnostic.

template <typename Q>
struct HeadQueue {
    static_assert(detail::queue::dependent_false_v<Q>,
        "crucible::session::diagnostic [Queue_Empty_Dequeue]: "
        "head_queue_t<Q>: Q is empty or is not a Queue<>.  Use "
        "is_queue_empty_v<Q> to test before dequeue.  The receiver's "
        "typing rules (L7) should have rejected this path already — "
        "a receive on an empty queue corresponds to a blocked state, "
        "not an error, at runtime.");
};

template <typename Head, typename... Rest>
struct HeadQueue<Queue<Head, Rest...>> {
    using type = Head;
};

template <typename Q>
using head_queue_t = typename HeadQueue<Q>::type;

// ═════════════════════════════════════════════════════════════════════
// ── tail_queue_t<Q> ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// All messages in Q EXCEPT the head — i.e., the queue after one
// dequeue.  Compile error on empty queue.  dequeue_queue_t is an
// alias that names the intent on the receiver's side.

template <typename Q>
struct TailQueue {
    static_assert(detail::queue::dependent_false_v<Q>,
        "crucible::session::diagnostic [Queue_Empty_Dequeue]: "
        "tail_queue_t<Q>: Q is empty or is not a Queue<>.  Use "
        "is_queue_empty_v<Q> to test before dequeue.");
};

template <typename Head, typename... Rest>
struct TailQueue<Queue<Head, Rest...>> {
    using type = Queue<Rest...>;
};

template <typename Q>
using tail_queue_t = typename TailQueue<Q>::type;

// Alias — the receiver's action is "pop the head", producing the
// new queue state (which is the tail of the old queue).
template <typename Q>
using dequeue_queue_t = tail_queue_t<Q>;

// ═════════════════════════════════════════════════════════════════════
// ── queue_contains_v<Q, From, To> ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Does Q have any message for the channel From → To?  Fold-expression
// disjunction; O(|Q|) compile-time instantiations.

template <typename Q, typename From, typename To>
struct queue_contains : std::false_type {};

template <typename... Ms, typename From, typename To>
struct queue_contains<Queue<Ms...>, From, To>
    : std::bool_constant<
          ((std::is_same_v<typename Ms::from, From> &&
            std::is_same_v<typename Ms::to,   To>) || ...)
      > {};

template <typename Q, typename From, typename To>
inline constexpr bool queue_contains_v =
    queue_contains<Q, From, To>::value;

// ═════════════════════════════════════════════════════════════════════
// ── count_matching_v<Q, From, To> ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// How many messages in Q target the channel From → To?  Used for
// per-channel bound checks (e.g., "this channel's queue must have
// at most K pending messages at any reachable state").

template <typename Q, typename From, typename To>
struct count_matching;

template <typename From, typename To>
struct count_matching<Queue<>, From, To>
    : std::integral_constant<std::size_t, 0> {};

template <typename... Ms, typename From, typename To>
struct count_matching<Queue<Ms...>, From, To>
    : std::integral_constant<std::size_t,
        ((std::is_same_v<typename Ms::from, From> &&
          std::is_same_v<typename Ms::to,   To> ? std::size_t{1}
                                               : std::size_t{0}) + ...
         + std::size_t{0})> {};

template <typename Q, typename From, typename To>
inline constexpr std::size_t count_matching_v =
    count_matching<Q, From, To>::value;

// ═════════════════════════════════════════════════════════════════════
// ── is_bounded_queue_v<Q, MaxCap> ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// queue_size_v<Q> ≤ MaxCap.  A single-state bound check.  The full
// "bounded at every REACHABLE state" variant (balanced+, PMY25
// Def 6.1) requires reachable-states enumeration — deferred to L7.

template <typename Q, std::size_t MaxCap>
inline constexpr bool is_bounded_queue_v = (queue_size_v<Q> <= MaxCap);

// ═════════════════════════════════════════════════════════════════════
// ── is_unavailable_queue_v / is_queue_state_v ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The full queue-state lattice is Queue<...> OR UnavailableQueue<Peer>.
// UnavailableQueue<PeerTag> is defined in SessionCrash.h (#347);
// we expose detection traits here so downstream layers can pattern-
// match without needing both headers.

template <typename T>
struct is_unavailable_queue : std::false_type {};

template <typename PeerTag>
struct is_unavailable_queue<UnavailableQueue<PeerTag>> : std::true_type {};

template <typename T>
inline constexpr bool is_unavailable_queue_v =
    is_unavailable_queue<T>::value;

// Combined predicate: T is a valid queue-state (either FIFO or ⊘).
template <typename T>
inline constexpr bool is_queue_state_v =
    is_queue_v<T> || is_unavailable_queue_v<T>;

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verify every queue operation at compile time.  Runs at header-
// inclusion time; any regression to the metafunctions above fails
// at the first TU that includes us.

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::queue::queue_self_test {

// Fixture tags for channel endpoints.
struct Alice {};
struct Bob   {};
struct Carol {};

// Fixture payload types.
struct Ping {};
struct Ack  {};
struct Data {};

// Fixture messages.
using PingAliceBob = QueuedMsg<Alice, Bob, Ping>;
using AckBobAlice  = QueuedMsg<Bob,   Alice, Ack>;
using DataBobCarol = QueuedMsg<Bob,   Carol, Data>;

// ─── QueuedMsg field exposure ──────────────────────────────────────

static_assert(is_queued_msg_v<PingAliceBob>);
static_assert(!is_queued_msg_v<int>);
static_assert(!is_queued_msg_v<Queue<>>);

static_assert(std::is_same_v<typename PingAliceBob::from,    Alice>);
static_assert(std::is_same_v<typename PingAliceBob::to,      Bob>);
static_assert(std::is_same_v<typename PingAliceBob::payload, Ping>);

// ─── Queue shape + size ────────────────────────────────────────────

static_assert(is_queue_v<EmptyQueue>);
static_assert(is_queue_v<Queue<PingAliceBob>>);
static_assert(is_queue_v<Queue<PingAliceBob, AckBobAlice>>);
static_assert(!is_queue_v<PingAliceBob>);
static_assert(!is_queue_v<int>);

static_assert(queue_size_v<EmptyQueue>                        == 0);
static_assert(queue_size_v<Queue<PingAliceBob>>               == 1);
static_assert(queue_size_v<Queue<PingAliceBob, AckBobAlice>>  == 2);

static_assert( is_queue_empty_v<EmptyQueue>);
static_assert(!is_queue_empty_v<Queue<PingAliceBob>>);

// ─── enqueue_queue_t ───────────────────────────────────────────────

// Enqueue onto empty.
static_assert(std::is_same_v<
    enqueue_queue_t<EmptyQueue, PingAliceBob>,
    Queue<PingAliceBob>>);

// Enqueue preserves order (right-append).
static_assert(std::is_same_v<
    enqueue_queue_t<Queue<PingAliceBob>, AckBobAlice>,
    Queue<PingAliceBob, AckBobAlice>>);

// Enqueue multiple times — composes associatively.
static_assert(std::is_same_v<
    enqueue_queue_t<
        enqueue_queue_t<EmptyQueue, PingAliceBob>,
        AckBobAlice>,
    Queue<PingAliceBob, AckBobAlice>>);

// ─── head_queue_t / tail_queue_t ───────────────────────────────────

static_assert(std::is_same_v<
    head_queue_t<Queue<PingAliceBob, AckBobAlice>>,
    PingAliceBob>);

static_assert(std::is_same_v<
    tail_queue_t<Queue<PingAliceBob, AckBobAlice>>,
    Queue<AckBobAlice>>);

// dequeue_queue_t is an alias for tail_queue_t.
static_assert(std::is_same_v<
    dequeue_queue_t<Queue<PingAliceBob, AckBobAlice>>,
    tail_queue_t<Queue<PingAliceBob, AckBobAlice>>>);

// Head of a single-element queue.
static_assert(std::is_same_v<
    head_queue_t<Queue<PingAliceBob>>,
    PingAliceBob>);

// Tail of a single-element queue is empty.
static_assert(std::is_same_v<
    tail_queue_t<Queue<PingAliceBob>>,
    EmptyQueue>);

// Enqueue + dequeue invariant:  a FIFO round trip on an empty queue
// recovers the empty queue.
static_assert(std::is_same_v<
    dequeue_queue_t<enqueue_queue_t<EmptyQueue, PingAliceBob>>,
    EmptyQueue>);

// ─── queue_contains_v ──────────────────────────────────────────────

static_assert(!queue_contains_v<EmptyQueue, Alice, Bob>);

static_assert( queue_contains_v<Queue<PingAliceBob>, Alice, Bob>);
static_assert(!queue_contains_v<Queue<PingAliceBob>, Bob,   Alice>);
static_assert(!queue_contains_v<Queue<PingAliceBob>, Alice, Carol>);

// Multi-message queue: all three channels present.
using MultiQ = Queue<PingAliceBob, AckBobAlice, DataBobCarol>;
static_assert(queue_contains_v<MultiQ, Alice, Bob>);
static_assert(queue_contains_v<MultiQ, Bob,   Alice>);
static_assert(queue_contains_v<MultiQ, Bob,   Carol>);
static_assert(!queue_contains_v<MultiQ, Alice, Carol>);
static_assert(!queue_contains_v<MultiQ, Carol, Alice>);

// ─── count_matching_v ──────────────────────────────────────────────

static_assert(count_matching_v<EmptyQueue, Alice, Bob> == 0);

// Queue with two messages on the same channel.
using TwoOnSameChannel = Queue<
    QueuedMsg<Alice, Bob, Ping>,
    QueuedMsg<Alice, Bob, Ping>>;
static_assert(count_matching_v<TwoOnSameChannel, Alice, Bob> == 2);
static_assert(count_matching_v<TwoOnSameChannel, Bob,   Alice> == 0);

// Multi-message queue: each channel has exactly one message.
static_assert(count_matching_v<MultiQ, Alice, Bob>   == 1);
static_assert(count_matching_v<MultiQ, Bob,   Alice> == 1);
static_assert(count_matching_v<MultiQ, Bob,   Carol> == 1);

// ─── is_bounded_queue_v ────────────────────────────────────────────

static_assert(is_bounded_queue_v<EmptyQueue, 0>);
static_assert(is_bounded_queue_v<EmptyQueue, 10>);
static_assert(is_bounded_queue_v<Queue<PingAliceBob>,             1>);
static_assert(is_bounded_queue_v<Queue<PingAliceBob, AckBobAlice>, 2>);
static_assert(!is_bounded_queue_v<Queue<PingAliceBob>,            0>);
static_assert(!is_bounded_queue_v<Queue<PingAliceBob, AckBobAlice>, 1>);

// ─── is_unavailable_queue_v / is_queue_state_v ─────────────────────

static_assert( is_unavailable_queue_v<UnavailableQueue<Alice>>);
static_assert( is_unavailable_queue_v<UnavailableQueue<Bob>>);
static_assert(!is_unavailable_queue_v<EmptyQueue>);
static_assert(!is_unavailable_queue_v<Queue<PingAliceBob>>);
static_assert(!is_unavailable_queue_v<PingAliceBob>);
static_assert(!is_unavailable_queue_v<int>);

// is_queue_state_v unions Queue and UnavailableQueue.
static_assert(is_queue_state_v<EmptyQueue>);
static_assert(is_queue_state_v<Queue<PingAliceBob>>);
static_assert(is_queue_state_v<UnavailableQueue<Alice>>);
static_assert(!is_queue_state_v<PingAliceBob>);
static_assert(!is_queue_state_v<int>);

// ─── Invariants ───────────────────────────────────────────────────

// Enqueue adds exactly one: size increases by 1.
static_assert(
    queue_size_v<enqueue_queue_t<Queue<PingAliceBob>, AckBobAlice>> ==
    queue_size_v<Queue<PingAliceBob>> + 1);

// Dequeue removes exactly one when non-empty.
static_assert(
    queue_size_v<dequeue_queue_t<Queue<PingAliceBob, AckBobAlice>>> ==
    queue_size_v<Queue<PingAliceBob, AckBobAlice>> - 1);

// Enqueue preserves existing message count.
static_assert(
    count_matching_v<
        enqueue_queue_t<Queue<PingAliceBob>, AckBobAlice>,
        Alice, Bob> == 1);
static_assert(
    count_matching_v<
        enqueue_queue_t<Queue<PingAliceBob>, AckBobAlice>,
        Bob, Alice> == 1);

// Dequeueing the head of a matching message decrements its channel's
// count by one.
static_assert(
    count_matching_v<
        dequeue_queue_t<Queue<PingAliceBob, AckBobAlice>>,
        Alice, Bob> == 0);
static_assert(
    count_matching_v<
        dequeue_queue_t<Queue<PingAliceBob, AckBobAlice>>,
        Bob, Alice> == 1);

// A long FIFO chain: 5 enqueues, head is the oldest.
using FiveChain = enqueue_queue_t<
    enqueue_queue_t<
    enqueue_queue_t<
    enqueue_queue_t<
    enqueue_queue_t<EmptyQueue,
                    QueuedMsg<Alice, Bob, Ping>>,
                    QueuedMsg<Alice, Bob, Ping>>,
                    QueuedMsg<Bob,   Alice, Ack>>,
                    QueuedMsg<Alice, Carol, Data>>,
                    QueuedMsg<Alice, Bob, Ping>>;
static_assert(queue_size_v<FiveChain> == 5);
static_assert(count_matching_v<FiveChain, Alice, Bob>   == 3);
static_assert(count_matching_v<FiveChain, Bob,   Alice> == 1);
static_assert(count_matching_v<FiveChain, Alice, Carol> == 1);
static_assert(is_bounded_queue_v<FiveChain, 5>);
static_assert(is_bounded_queue_v<FiveChain, 100>);
static_assert(!is_bounded_queue_v<FiveChain, 4>);

// ─── Round-trip invariant (enqueue-then-dequeue on non-head) ──────
//
// Enqueuing a new message and then dequeuing the OLD head preserves
// the FIFO ordering: the new message is at the end of the resulting
// queue.
using Round = dequeue_queue_t<enqueue_queue_t<
    Queue<PingAliceBob, AckBobAlice>,
    DataBobCarol>>;
static_assert(std::is_same_v<
    Round,
    Queue<AckBobAlice, DataBobCarol>>);

}  // namespace detail::queue::queue_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
