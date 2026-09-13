// Queue types are pure compile-time structures with no runtime footprint.
// The body of main exists only so the harness has something to execute.
// The scenario below is a two-peer asynchronous channel.

#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionQueue.h>

#include <cstdio>
#include <type_traits>

namespace {

using namespace crucible::safety::proto;

struct Producer {};
struct Consumer {};

struct Job {
    int id;
};
struct Result {
    int value;
};

using JobMsg = QueuedMsg<Producer, Consumer, Job>;
using ResultMsg = QueuedMsg<Consumer, Producer, Result>;

using Q0 = EmptyQueue;
using Q1 = enqueue_queue_t<Q0, JobMsg>;  // Producer sent Job
using Q2 = enqueue_queue_t<Q1, JobMsg>;  // Producer sent another Job
using Q3 = dequeue_queue_t<Q2>;  // Consumer consumed oldest Job
using Q4 = enqueue_queue_t<Q3, ResultMsg>;  // Consumer sent Result back

static_assert(queue_size_v<Q0> == 0);
static_assert(queue_size_v<Q1> == 1);
static_assert(queue_size_v<Q2> == 2);
static_assert(queue_size_v<Q3> == 1);
static_assert(queue_size_v<Q4> == 2);

static_assert(count_matching_v<Q2, Producer, Consumer> == 2);

static_assert(count_matching_v<Q3, Producer, Consumer> == 1);

// The queue now holds one message per direction, not two in one.
static_assert(count_matching_v<Q4, Producer, Consumer> == 1);
static_assert(count_matching_v<Q4, Consumer, Producer> == 1);

// The head is still the Job. The oldest message wins.
static_assert(std::is_same_v<head_queue_t<Q4>, JobMsg>);

static_assert(is_bounded_queue_v<Q2, 2>);
static_assert(!is_bounded_queue_v<Q2, 1>);
static_assert(is_bounded_queue_v<Q4, 2>);

// A third enqueue overflows a capacity-2 bound.
using Q2_plus_one = enqueue_queue_t<Q2, JobMsg>;
static_assert(queue_size_v<Q2_plus_one> == 3);
static_assert(!is_bounded_queue_v<Q2_plus_one, 2>);
static_assert(is_bounded_queue_v<Q2_plus_one, 3>);

// When a peer crashes, its incoming queue becomes unavailable rather than
// empty. The queue-state predicate admits both forms.

using CrashedChannelState = UnavailableQueue<Consumer>;
static_assert(is_unavailable_queue_v<CrashedChannelState>);
static_assert(is_queue_state_v<CrashedChannelState>);
static_assert(is_queue_state_v<Q2>);
static_assert(is_queue_state_v<EmptyQueue>);
static_assert(!is_queue_state_v<JobMsg>);  // not a state, just a message

static_assert(!is_unavailable_queue_v<Q2>);
static_assert(!is_unavailable_queue_v<EmptyQueue>);

static_assert(queue_contains_v<Q4, Producer, Consumer>);
static_assert(queue_contains_v<Q4, Consumer, Producer>);

static_assert(!queue_contains_v<EmptyQueue, Producer, Consumer>);

int run_queue_invariants() {
    if (queue_size_v<Q4> != 2) return 1;
    if (is_queue_empty_v<Q4>) return 1;
    if (!is_queue_empty_v<Q0>) return 1;
    if (count_matching_v<Q4, Producer, Consumer> != 1) return 1;
    if (!is_bounded_queue_v<Q4, 8>) return 1;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_queue_invariants(); rc != 0) return rc;
    std::puts("session_queue: QueuedMsg + Queue + enqueue/dequeue + bounded + unavailable OK");
    return 0;
}
