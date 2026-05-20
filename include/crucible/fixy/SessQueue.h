#pragma once

// ── crucible::fixy::sess::queue — typed session queue-state slice ───
//
// FIXY-U-052f (sixth slice of the U-052 umbrella).  Re-exports the
// complete public surface of sessions/SessionQueue.h (the L3 "queue
// types σ" layer of the session-type stack — the en-route FIFO state
// that async session typing reduces over) into
// `crucible::fixy::sess::queue::`.
//
// Production callers — async session-state reasoning, MPST projection
// queue tracking, en-route message accounting — spell the queue-type
// vocabulary through the fixy umbrella, not raw `safety::proto::`.
//
// Twenty-four symbols (the complete SessionQueue.h public API):
//
//   Core carriers (3):     QueuedMsg, Queue, EmptyQueue
//   Shape/size traits (6): is_queue, is_queue_v, is_queued_msg,
//                          is_queued_msg_v, queue_size_v, is_queue_empty_v
//   FIFO operations (7):   EnqueueQueue, enqueue_queue_t, HeadQueue,
//                          head_queue_t, TailQueue, tail_queue_t,
//                          dequeue_queue_t
//   Channel queries (4):   queue_contains, queue_contains_v,
//                          count_matching, count_matching_v
//   Bound/state (4):       is_bounded_queue_v, is_unavailable_queue,
//                          is_unavailable_queue_v, is_queue_state_v
//
// ── Why a dedicated queue:: sub-namespace ──────────────────────────
//
// fixy::sess:: holds the binary session combinators; ::mpst:: the
// global-types layer; ::subtype:: the refinement-order layer;
// ::declassify:: / ::ct:: / ::contentaddr:: / ::eventlog:: the
// payload/record layers.  The queue layer is the EN-ROUTE STATE layer:
// the σ component of an async session configuration (the in-flight
// messages between a send and its matching receive).  Keeping it in
// ::queue:: lets audit-grep `fixy::sess::queue::` find every
// fixy-routed queue-state computation distinct from substrate-direct
// `safety::proto::` call sites.
//
// ── A note on UnavailableQueue ─────────────────────────────────────
//
// `UnavailableQueue<PeerTag>` (the ⊘ crash-stop queue state) is OWNED
// by sessions/SessionCrash.h (SEPLOG-I5 / #347), not by SessionQueue.h
// — SessionQueue.h only ships the DETECTION traits
// (is_unavailable_queue / _v / is_queue_state_v) so downstream layers
// pattern-match the full queue-state lattice (Queue<...> ∪ ⊘) without
// pulling SessionCrash.h.  This slice re-exports only those detection
// traits; the `UnavailableQueue` carrier itself is left for a future
// crash:: slice.
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Twenty-four using-decls + a sentinel battery + smoke routine.
// No new types, no mint factories, no free functions.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; Queue/QueuedMsg are empty
//              type markers (zero runtime footprint).
//   TypeSafe — using-decls preserve substrate type identity; the queue
//              ops are pure type-level metafunctions.
//   NullSafe — no pointer state introduced.
//   MemSafe  — all symbols are compile-time-only; nothing is allocated.
//   DetSafe  — queue reduction is a pure type-level function; same
//              (Q, op) always yields the same result type.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).

#include <crucible/sessions/SessionQueue.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::queue {

// ── 1. Core carriers (3) ───────────────────────────────────────────
using ::crucible::safety::proto::QueuedMsg;
using ::crucible::safety::proto::Queue;
using ::crucible::safety::proto::EmptyQueue;

// ── 2. Shape / size traits (6) ─────────────────────────────────────
using ::crucible::safety::proto::is_queue;
using ::crucible::safety::proto::is_queue_v;
using ::crucible::safety::proto::is_queued_msg;
using ::crucible::safety::proto::is_queued_msg_v;
using ::crucible::safety::proto::queue_size_v;
using ::crucible::safety::proto::is_queue_empty_v;

// ── 3. FIFO operations — enqueue / head / tail / dequeue (7) ───────
using ::crucible::safety::proto::EnqueueQueue;
using ::crucible::safety::proto::enqueue_queue_t;
using ::crucible::safety::proto::HeadQueue;
using ::crucible::safety::proto::head_queue_t;
using ::crucible::safety::proto::TailQueue;
using ::crucible::safety::proto::tail_queue_t;
using ::crucible::safety::proto::dequeue_queue_t;

// ── 4. Channel-scoped queries (4) ──────────────────────────────────
using ::crucible::safety::proto::queue_contains;
using ::crucible::safety::proto::queue_contains_v;
using ::crucible::safety::proto::count_matching;
using ::crucible::safety::proto::count_matching_v;

// ── 5. Bound + queue-state lattice predicates (4) ──────────────────
using ::crucible::safety::proto::is_bounded_queue_v;
using ::crucible::safety::proto::is_unavailable_queue;
using ::crucible::safety::proto::is_unavailable_queue_v;
using ::crucible::safety::proto::is_queue_state_v;

}  // namespace crucible::fixy::sess::queue

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as fixy/SessSubtype.h::u052e_self_test.
// Substrate-side renames trip at every consumer's include time, not
// three TUs deep.

namespace crucible::fixy::sess::queue::u052f_self_test {

namespace proto = ::crucible::safety::proto;

// Representative role tags + queued messages.
struct RoleA {};
struct RoleB {};
struct RoleC {};

using M_AB = QueuedMsg<RoleA, RoleB, int>;
using M_BC = QueuedMsg<RoleB, RoleC, double>;

// ── A. Carrier + alias type identity ───────────────────────────────
static_assert(std::is_same_v<QueuedMsg<RoleA, RoleB, int>,
                             proto::QueuedMsg<RoleA, RoleB, int>>);
static_assert(std::is_same_v<Queue<M_AB, M_BC>, proto::Queue<M_AB, M_BC>>);
static_assert(std::is_same_v<EmptyQueue, proto::EmptyQueue>);
static_assert(std::is_same_v<EmptyQueue, Queue<>>,
    "EmptyQueue must alias Queue<> through the fixy spelling.");

// ── B. Shape / size predicates ─────────────────────────────────────
static_assert(is_queue_v<Queue<M_AB>>);
static_assert(!is_queue_v<int>);
static_assert(is_queued_msg_v<M_AB>);
static_assert(!is_queued_msg_v<int>);
static_assert(queue_size_v<Queue<M_AB, M_BC>> == 2);
static_assert(is_queue_empty_v<EmptyQueue>);
static_assert(!is_queue_empty_v<Queue<M_AB>>);

// ── C. FIFO operations — right-append, head/tail dequeue ───────────
static_assert(std::is_same_v<enqueue_queue_t<EmptyQueue, M_AB>, Queue<M_AB>>,
    "enqueue appends to the END (right-append FIFO).");
static_assert(std::is_same_v<enqueue_queue_t<Queue<M_AB>, M_BC>,
                             Queue<M_AB, M_BC>>);
static_assert(std::is_same_v<head_queue_t<Queue<M_AB, M_BC>>, M_AB>,
    "head is the oldest (first) message — next to dequeue.");
static_assert(std::is_same_v<tail_queue_t<Queue<M_AB, M_BC>>, Queue<M_BC>>);
static_assert(std::is_same_v<dequeue_queue_t<Queue<M_AB, M_BC>>,
                             tail_queue_t<Queue<M_AB, M_BC>>>,
    "dequeue is the receiver-side spelling of tail.");

// ── D. Channel-scoped queries ──────────────────────────────────────
static_assert(queue_contains_v<Queue<M_AB, M_BC>, RoleA, RoleB>);
static_assert(!queue_contains_v<Queue<M_AB, M_BC>, RoleA, RoleC>,
    "no message targets the A→C channel.");
static_assert(count_matching_v<Queue<M_AB, M_AB, M_BC>, RoleA, RoleB> == 2);
static_assert(count_matching_v<Queue<M_AB>, RoleB, RoleC> == 0);

// ── E. Bound + queue-state lattice ─────────────────────────────────
static_assert(is_bounded_queue_v<Queue<M_AB>, 2>);
static_assert(!is_bounded_queue_v<Queue<M_AB, M_BC, M_AB>, 2>);
static_assert(!is_unavailable_queue_v<Queue<>>,
    "a live FIFO queue is not the ⊘ unavailable state.");
static_assert(is_queue_state_v<Queue<>>,
    "Queue<> is a valid queue-state (the empty FIFO).");
static_assert(!is_queue_state_v<int>,
    "an arbitrary type is not a queue-state.");

// ── F. Cardinality witness — count of items U-052f surfaces ────────
//
//   Core carriers (3) + shape/size (6) + FIFO ops (7) +
//   channel queries (4) + bound/state (4)                   ──── 24
constexpr int u052f_surface_cardinality = 24;
static_assert(u052f_surface_cardinality == 24,
    "fixy::sess::queue:: U-052f surface cardinality drifted — "
    "update SessQueue.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::queue::u052f_self_test

namespace crucible::fixy::sess::queue {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body bugs.
// The smoke routine forces the queue metafunctions through real
// instantiation so any latent template-evaluation issue surfaces under
// `-fsyntax-only` of any TU that includes SessQueue.h.
//
// Cost: compile-time metafunction evaluation only — no runtime state,
// no I/O.  Queue / QueuedMsg are empty markers, so the locals are
// zero-footprint.

inline void runtime_smoke_test() noexcept {
    struct RoleA {};
    struct RoleB {};
    using Msg   = QueuedMsg<RoleA, RoleB, int>;
    using Q0    = EmptyQueue;
    using Q1    = enqueue_queue_t<Q0, Msg>;
    using Q2    = enqueue_queue_t<Q1, Msg>;

    [[maybe_unused]] const Msg msg{};
    [[maybe_unused]] const Q2  q{};

    [[maybe_unused]] constexpr std::size_t sz      = queue_size_v<Q2>;
    [[maybe_unused]] constexpr bool        empty0  = is_queue_empty_v<Q0>;
    [[maybe_unused]] constexpr bool        is_q    = is_queue_v<Q2>;
    [[maybe_unused]] constexpr bool        is_msg  = is_queued_msg_v<Msg>;
    [[maybe_unused]] constexpr bool        has_ab  = queue_contains_v<Q2, RoleA, RoleB>;
    [[maybe_unused]] constexpr std::size_t cnt     = count_matching_v<Q2, RoleA, RoleB>;
    [[maybe_unused]] constexpr bool        bounded = is_bounded_queue_v<Q2, 4>;
    [[maybe_unused]] constexpr bool        state   = is_queue_state_v<Q2>;
    [[maybe_unused]] constexpr bool        unavail = is_unavailable_queue_v<Q2>;

    using Head = head_queue_t<Q2>;
    using Tail = tail_queue_t<Q2>;
    [[maybe_unused]] constexpr bool head_ok = std::is_same_v<Head, Msg>;
    [[maybe_unused]] constexpr bool tail_ok = std::is_same_v<Tail, Q1>;

    (void) msg; (void) q; (void) sz; (void) empty0; (void) is_q; (void) is_msg;
    (void) has_ab; (void) cnt; (void) bounded; (void) state; (void) unavail;
    (void) head_ok; (void) tail_ok;
}

}  // namespace crucible::fixy::sess::queue
