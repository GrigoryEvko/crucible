#pragma once

#include <crucible/sessions/SessionQueue.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::queue {

using ::crucible::safety::proto::QueuedMsg;
using ::crucible::safety::proto::Queue;
using ::crucible::safety::proto::EmptyQueue;

using ::crucible::safety::proto::is_queue;
using ::crucible::safety::proto::is_queue_v;
using ::crucible::safety::proto::is_queued_msg;
using ::crucible::safety::proto::is_queued_msg_v;
using ::crucible::safety::proto::queue_size_v;
using ::crucible::safety::proto::is_queue_empty_v;

using ::crucible::safety::proto::EnqueueQueue;
using ::crucible::safety::proto::enqueue_queue_t;
using ::crucible::safety::proto::HeadQueue;
using ::crucible::safety::proto::head_queue_t;
using ::crucible::safety::proto::TailQueue;
using ::crucible::safety::proto::tail_queue_t;
using ::crucible::safety::proto::dequeue_queue_t;

using ::crucible::safety::proto::queue_contains;
using ::crucible::safety::proto::queue_contains_v;
using ::crucible::safety::proto::count_matching;
using ::crucible::safety::proto::count_matching_v;

using ::crucible::safety::proto::is_bounded_queue_v;
using ::crucible::safety::proto::is_unavailable_queue;
using ::crucible::safety::proto::is_unavailable_queue_v;
using ::crucible::safety::proto::is_queue_state_v;

}  // namespace crucible::fixy::sess::queue

namespace crucible::fixy::sess::queue::u052f_self_test {

namespace proto = ::crucible::safety::proto;

struct RoleA {};
struct RoleB {};
struct RoleC {};

using M_AB = QueuedMsg<RoleA, RoleB, int>;
using M_BC = QueuedMsg<RoleB, RoleC, double>;

static_assert(std::is_same_v<QueuedMsg<RoleA, RoleB, int>, proto::QueuedMsg<RoleA, RoleB, int>>);
static_assert(std::is_same_v<Queue<M_AB, M_BC>, proto::Queue<M_AB, M_BC>>);
static_assert(std::is_same_v<EmptyQueue, proto::EmptyQueue>);
static_assert(std::is_same_v<EmptyQueue, Queue<>>, "EmptyQueue must alias Queue<> through the fixy spelling.");

static_assert(is_queue_v<Queue<M_AB>>);
static_assert(!is_queue_v<int>);
static_assert(is_queued_msg_v<M_AB>);
static_assert(!is_queued_msg_v<int>);
static_assert(queue_size_v<Queue<M_AB, M_BC>> == 2);
static_assert(is_queue_empty_v<EmptyQueue>);
static_assert(!is_queue_empty_v<Queue<M_AB>>);

static_assert(std::is_same_v<enqueue_queue_t<EmptyQueue, M_AB>, Queue<M_AB>>,
              "enqueue appends to the END (right-append FIFO).");
static_assert(std::is_same_v<enqueue_queue_t<Queue<M_AB>, M_BC>, Queue<M_AB, M_BC>>);
static_assert(std::is_same_v<head_queue_t<Queue<M_AB, M_BC>>, M_AB>,
              "head is the oldest (first) message — next to dequeue.");
static_assert(std::is_same_v<tail_queue_t<Queue<M_AB, M_BC>>, Queue<M_BC>>);
static_assert(std::is_same_v<dequeue_queue_t<Queue<M_AB, M_BC>>, tail_queue_t<Queue<M_AB, M_BC>>>,
              "dequeue is the receiver-side spelling of tail.");

static_assert(queue_contains_v<Queue<M_AB, M_BC>, RoleA, RoleB>);
static_assert(!queue_contains_v<Queue<M_AB, M_BC>, RoleA, RoleC>, "no message targets the A→C channel.");
static_assert(count_matching_v<Queue<M_AB, M_AB, M_BC>, RoleA, RoleB> == 2);
static_assert(count_matching_v<Queue<M_AB>, RoleB, RoleC> == 0);

static_assert(is_bounded_queue_v<Queue<M_AB>, 2>);
static_assert(!is_bounded_queue_v<Queue<M_AB, M_BC, M_AB>, 2>);
static_assert(!is_unavailable_queue_v<Queue<>>, "a live FIFO queue is not the ⊘ unavailable state.");
static_assert(is_queue_state_v<Queue<>>, "Queue<> is a valid queue-state (the empty FIFO).");
static_assert(!is_queue_state_v<int>, "an arbitrary type is not a queue-state.");

constexpr int u052f_surface_cardinality = 24;
static_assert(u052f_surface_cardinality == 24, "The re-exported surface cardinality drifted — update the "
                                               "using-decls and this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::queue::u052f_self_test

namespace crucible::fixy::sess::queue {

inline void runtime_smoke_test() noexcept {
    struct RoleA {};
    struct RoleB {};
    using Msg = QueuedMsg<RoleA, RoleB, int>;
    using Q0 = EmptyQueue;
    using Q1 = enqueue_queue_t<Q0, Msg>;
    using Q2 = enqueue_queue_t<Q1, Msg>;

    [[maybe_unused]] const Msg msg{};
    [[maybe_unused]] const Q2 q{};

    [[maybe_unused]] constexpr std::size_t sz = queue_size_v<Q2>;
    [[maybe_unused]] constexpr bool empty0 = is_queue_empty_v<Q0>;
    [[maybe_unused]] constexpr bool is_q = is_queue_v<Q2>;
    [[maybe_unused]] constexpr bool is_msg = is_queued_msg_v<Msg>;
    [[maybe_unused]] constexpr bool has_ab = queue_contains_v<Q2, RoleA, RoleB>;
    [[maybe_unused]] constexpr std::size_t cnt = count_matching_v<Q2, RoleA, RoleB>;
    [[maybe_unused]] constexpr bool bounded = is_bounded_queue_v<Q2, 4>;
    [[maybe_unused]] constexpr bool state = is_queue_state_v<Q2>;
    [[maybe_unused]] constexpr bool unavail = is_unavailable_queue_v<Q2>;

    using Head = head_queue_t<Q2>;
    using Tail = tail_queue_t<Q2>;
    [[maybe_unused]] constexpr bool head_ok = std::is_same_v<Head, Msg>;
    [[maybe_unused]] constexpr bool tail_ok = std::is_same_v<Tail, Q1>;

    (void)msg;
    (void)q;
    (void)sz;
    (void)empty0;
    (void)is_q;
    (void)is_msg;
    (void)has_ab;
    (void)cnt;
    (void)bounded;
    (void)state;
    (void)unavail;
    (void)head_ok;
    (void)tail_ok;
}

}  // namespace crucible::fixy::sess::queue
