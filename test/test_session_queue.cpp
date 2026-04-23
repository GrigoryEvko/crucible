// Runtime harness for L3 queue types σ (task #344, SEPLOG-I2).
// Queue types are pure compile-time structures (zero runtime
// footprint); this harness wraps the compile-time invariants in a
// main() so ctest has an executable, and exercises queue operations
// against a realistic two-peer async channel scenario.

#include <crucible/safety/SessionCrash.h>
#include <crucible/safety/SessionQueue.h>

#include <cstdio>
#include <type_traits>

namespace {

using namespace crucible::safety::proto;

// ── Fixture tags ────────────────────────────────────────────────

struct Producer {};
struct Consumer {};

// ── Fixture payloads ────────────────────────────────────────────

struct Job    { int id; };
struct Result { int value; };

// ── Message types ───────────────────────────────────────────────

using JobMsg    = QueuedMsg<Producer, Consumer, Job>;
using ResultMsg = QueuedMsg<Consumer, Producer, Result>;

// ── Simulating enqueue/dequeue on a channel ────────────────────

using Q0 = EmptyQueue;
using Q1 = enqueue_queue_t<Q0, JobMsg>;        // Producer sent Job
using Q2 = enqueue_queue_t<Q1, JobMsg>;        // Producer sent another Job
using Q3 = dequeue_queue_t<Q2>;                // Consumer consumed oldest Job
using Q4 = enqueue_queue_t<Q3, ResultMsg>;     // Consumer sent Result back

// Verify the FIFO timeline.
static_assert(queue_size_v<Q0> == 0);
static_assert(queue_size_v<Q1> == 1);
static_assert(queue_size_v<Q2> == 2);
static_assert(queue_size_v<Q3> == 1);
static_assert(queue_size_v<Q4> == 2);

// After both enqueues, we have [Job, Job].
static_assert(count_matching_v<Q2, Producer, Consumer> == 2);

// After dequeue, we have [Job] (still Producer→Consumer).
static_assert(count_matching_v<Q3, Producer, Consumer> == 1);

// After Consumer sends Result back, we have [Job, Result] — one on
// each channel.
static_assert(count_matching_v<Q4, Producer, Consumer> == 1);
static_assert(count_matching_v<Q4, Consumer, Producer> == 1);

// Head of Q4 is still the Job (FIFO: oldest message wins).
static_assert(std::is_same_v<head_queue_t<Q4>, JobMsg>);

// ── Bounded-queue check: capacity gate ─────────────────────────

// A queue of capacity 2 accepts Q2 but NOT Q4 (after one more
// enqueue, Q4 still has 2 — bound ok).
static_assert( is_bounded_queue_v<Q2, 2>);
static_assert(!is_bounded_queue_v<Q2, 1>);
static_assert( is_bounded_queue_v<Q4, 2>);

// A third Job enqueue would overflow a capacity-2 bound.
using Q2_plus_one = enqueue_queue_t<Q2, JobMsg>;
static_assert(queue_size_v<Q2_plus_one> == 3);
static_assert(!is_bounded_queue_v<Q2_plus_one, 2>);
static_assert( is_bounded_queue_v<Q2_plus_one, 3>);

// ── Crash simulation: queue becomes UnavailableQueue ───────────

// Suppose Consumer crashes.  The queue Producer→Consumer transitions
// from Queue<JobMsg, ...> to UnavailableQueue<Consumer>.  The queue-
// state predicate admits both forms.

using CrashedChannelState = UnavailableQueue<Consumer>;
static_assert(is_unavailable_queue_v<CrashedChannelState>);
static_assert( is_queue_state_v<CrashedChannelState>);
static_assert( is_queue_state_v<Q2>);
static_assert( is_queue_state_v<EmptyQueue>);
static_assert(!is_queue_state_v<JobMsg>);  // not a state, just a message

// is_unavailable_queue_v discriminates between FIFO and ⊘.
static_assert(!is_unavailable_queue_v<Q2>);
static_assert(!is_unavailable_queue_v<EmptyQueue>);

// ── queue_contains_v for direction discrimination ──────────────

// A queue in Q4 has messages in BOTH directions.
static_assert(queue_contains_v<Q4, Producer, Consumer>);
static_assert(queue_contains_v<Q4, Consumer, Producer>);

// An empty queue contains no messages on any channel.
static_assert(!queue_contains_v<EmptyQueue, Producer, Consumer>);

// ── Runtime smoke ──────────────────────────────────────────────

int run_queue_invariants() {
    // Trivial runtime exercise — the real coverage is static_asserts.
    if (queue_size_v<Q4> != 2) return 1;
    if (is_queue_empty_v<Q4>)  return 1;
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
