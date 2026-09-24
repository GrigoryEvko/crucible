#include <crucible/concurrent/scheduler/Policies.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <type_traits>

namespace cs = crucible::concurrent::scheduler;
namespace ct = crucible::concurrent::traits;

// The job type must be a lock-free atomic type, which is what the
// work-stealing deque's value concept demands.  A real scheduler pulls
// the priority out of a richer job, but only the structural contract
// matters here, so the key is the value itself.

using Job = std::uint64_t;

struct PriorityKey {
    static std::uint64_t key(const Job& j) noexcept { return j; }
};

using DeadlinePolicy = cs::Deadline<PriorityKey,
                                    /*NumProducers=*/4,
                                    /*NumBuckets=*/64,
                                    /*BucketCap=*/16,
                                    /*Quantum=*/1000000ULL>;

using CfsPolicy = cs::Cfs<PriorityKey,
                          /*NumProducers=*/4,
                          /*NumBuckets=*/64,
                          /*BucketCap=*/16,
                          /*Quantum=*/100000ULL>;

using EevdfPolicy = cs::Eevdf<PriorityKey,
                              /*NumProducers=*/4,
                              /*NumBuckets=*/64,
                              /*BucketCap=*/16,
                              /*Quantum=*/100000ULL>;

// A per-shard variant gives each shard its own calendar grid, so a
// producer push reads nothing another thread wrote.  It trades global
// priority ordering for tail latency.
using DeadlinePerShardPolicy = cs::DeadlinePerShard<PriorityKey,
                                                    /*NumShards=*/4,
                                                    /*NumBuckets=*/64,
                                                    /*BucketCap=*/16,
                                                    /*QuantumNs=*/1000000ULL>;

using CfsPerShardPolicy = cs::CfsPerShard<PriorityKey,
                                          /*NumShards=*/4,
                                          /*NumBuckets=*/64,
                                          /*BucketCap=*/16,
                                          /*Quantum=*/100000ULL>;

using EevdfPerShardPolicy = cs::EevdfPerShard<PriorityKey,
                                              /*NumShards=*/4,
                                              /*NumBuckets=*/64,
                                              /*BucketCap=*/16,
                                              /*Quantum=*/100000ULL>;

static_assert(cs::SchedulerPolicy<cs::Fifo, Job>);
static_assert(cs::SchedulerPolicy<cs::Lifo, Job>);
static_assert(cs::SchedulerPolicy<cs::RoundRobin, Job>);
static_assert(cs::SchedulerPolicy<cs::LocalityAware, Job>);
static_assert(cs::SchedulerPolicy<DeadlinePolicy, Job>);
static_assert(cs::SchedulerPolicy<CfsPolicy, Job>);
static_assert(cs::SchedulerPolicy<EevdfPolicy, Job>);
static_assert(cs::SchedulerPolicy<DeadlinePerShardPolicy, Job>);
static_assert(cs::SchedulerPolicy<CfsPerShardPolicy, Job>);
static_assert(cs::SchedulerPolicy<EevdfPerShardPolicy, Job>);

static_assert(ct::PermissionedChannel<cs::Fifo::queue_template<Job>>);
static_assert(ct::PermissionedChannel<cs::Lifo::queue_template<Job>>);
static_assert(ct::PermissionedChannel<cs::RoundRobin::queue_template<Job>>);
static_assert(ct::PermissionedChannel<cs::LocalityAware::queue_template<Job>>);
static_assert(ct::PermissionedChannel<DeadlinePolicy::queue_template<Job>>);
static_assert(ct::PermissionedChannel<CfsPolicy::queue_template<Job>>);
static_assert(ct::PermissionedChannel<EevdfPolicy::queue_template<Job>>);
static_assert(ct::PermissionedChannel<DeadlinePerShardPolicy::queue_template<Job>>);
static_assert(ct::PermissionedChannel<CfsPerShardPolicy::queue_template<Job>>);
static_assert(ct::PermissionedChannel<EevdfPerShardPolicy::queue_template<Job>>);

// Each queue falls on exactly one side of the pool-based and
// linear-only split.  Fifo rides a multi-producer multi-consumer queue,
// Lifo a work-stealing deque, and RoundRobin a multi-producer queue.
static_assert(ct::PoolBasedChannel<cs::Fifo::queue_template<Job>>);
static_assert(!ct::LinearOnlyChannel<cs::Fifo::queue_template<Job>>);

static_assert(ct::PoolBasedChannel<cs::Lifo::queue_template<Job>>);
static_assert(!ct::LinearOnlyChannel<cs::Lifo::queue_template<Job>>);

static_assert(ct::PoolBasedChannel<cs::RoundRobin::queue_template<Job>>);
static_assert(!ct::LinearOnlyChannel<cs::RoundRobin::queue_template<Job>>);

// LocalityAware rides a sharded grid, and the three priority policies
// ride a calendar grid.
static_assert(!ct::PoolBasedChannel<cs::LocalityAware::queue_template<Job>>);
static_assert(ct::LinearOnlyChannel<cs::LocalityAware::queue_template<Job>>);

static_assert(!ct::PoolBasedChannel<DeadlinePolicy::queue_template<Job>>);
static_assert(ct::LinearOnlyChannel<DeadlinePolicy::queue_template<Job>>);

static_assert(!ct::PoolBasedChannel<CfsPolicy::queue_template<Job>>);
static_assert(ct::LinearOnlyChannel<CfsPolicy::queue_template<Job>>);

static_assert(!ct::PoolBasedChannel<EevdfPolicy::queue_template<Job>>);
static_assert(ct::LinearOnlyChannel<EevdfPolicy::queue_template<Job>>);

// A per-shard variant is linear-only too: each shard is its own
// calendar, and there is no shared pool to drain.
static_assert(!ct::PoolBasedChannel<DeadlinePerShardPolicy::queue_template<Job>>);
static_assert(ct::LinearOnlyChannel<DeadlinePerShardPolicy::queue_template<Job>>);

static_assert(!ct::PoolBasedChannel<CfsPerShardPolicy::queue_template<Job>>);
static_assert(ct::LinearOnlyChannel<CfsPerShardPolicy::queue_template<Job>>);

static_assert(!ct::PoolBasedChannel<EevdfPerShardPolicy::queue_template<Job>>);
static_assert(ct::LinearOnlyChannel<EevdfPerShardPolicy::queue_template<Job>>);

static_assert(cs::Fifo::priority_kind == cs::PriorityKind::None);
static_assert(cs::Lifo::priority_kind == cs::PriorityKind::None);
static_assert(cs::RoundRobin::priority_kind == cs::PriorityKind::None);
static_assert(cs::LocalityAware::priority_kind == cs::PriorityKind::None);
static_assert(DeadlinePolicy::priority_kind == cs::PriorityKind::Deadline);
static_assert(CfsPolicy::priority_kind == cs::PriorityKind::VirtualRuntime);
static_assert(EevdfPolicy::priority_kind == cs::PriorityKind::VirtualDeadline);
static_assert(DeadlinePerShardPolicy::priority_kind == cs::PriorityKind::Deadline);
static_assert(CfsPerShardPolicy::priority_kind == cs::PriorityKind::VirtualRuntime);
static_assert(EevdfPerShardPolicy::priority_kind == cs::PriorityKind::VirtualDeadline);

static_assert(!cs::Fifo::needs_topology);
static_assert(!cs::Lifo::needs_topology);
static_assert(!cs::RoundRobin::needs_topology);
static_assert(cs::LocalityAware::needs_topology);
static_assert(!DeadlinePolicy::needs_topology);
static_assert(!CfsPolicy::needs_topology);
static_assert(!EevdfPolicy::needs_topology);
// A per-shard variant needs the topology probe, because each producer
// is pinned to the shard of its own NUMA node or cache group.
static_assert(DeadlinePerShardPolicy::needs_topology);
static_assert(CfsPerShardPolicy::needs_topology);
static_assert(EevdfPerShardPolicy::needs_topology);

static_assert(!cs::needs_priority_key_v<cs::Fifo>);
static_assert(!cs::needs_priority_key_v<cs::Lifo>);
static_assert(!cs::needs_priority_key_v<cs::RoundRobin>);
static_assert(!cs::needs_priority_key_v<cs::LocalityAware>);
static_assert(cs::needs_priority_key_v<DeadlinePolicy>);
static_assert(cs::needs_priority_key_v<CfsPolicy>);
static_assert(cs::needs_priority_key_v<EevdfPolicy>);
static_assert(cs::needs_priority_key_v<DeadlinePerShardPolicy>);
static_assert(cs::needs_priority_key_v<CfsPerShardPolicy>);
static_assert(cs::needs_priority_key_v<EevdfPerShardPolicy>);

static_assert(std::is_same_v<cs::DefaultPolicy, cs::LocalityAware>);

// The three priority policies share one grid topology and one key
// extractor, yet their distinct tags reach the grid's template
// parameters.  That keeps the queue types apart, so mixing two of them
// is a compile error rather than a silent merge.

static_assert(!std::is_same_v<DeadlinePolicy::queue_template<Job>, CfsPolicy::queue_template<Job>>);
static_assert(!std::is_same_v<DeadlinePolicy::queue_template<Job>, EevdfPolicy::queue_template<Job>>);
static_assert(!std::is_same_v<CfsPolicy::queue_template<Job>, EevdfPolicy::queue_template<Job>>);
static_assert(!std::is_same_v<cs::Fifo::queue_template<Job>, cs::RoundRobin::queue_template<Job>>);
static_assert(!std::is_same_v<cs::LocalityAware::queue_template<Job>, DeadlinePolicy::queue_template<Job>>);

// The same separation holds among the per-shard variants and between
// each one and its single-grid counterpart.
static_assert(!std::is_same_v<DeadlinePerShardPolicy::queue_template<Job>, CfsPerShardPolicy::queue_template<Job>>);
static_assert(!std::is_same_v<DeadlinePerShardPolicy::queue_template<Job>, EevdfPerShardPolicy::queue_template<Job>>);
static_assert(!std::is_same_v<CfsPerShardPolicy::queue_template<Job>, EevdfPerShardPolicy::queue_template<Job>>);
static_assert(!std::is_same_v<DeadlinePolicy::queue_template<Job>, DeadlinePerShardPolicy::queue_template<Job>>);
static_assert(!std::is_same_v<CfsPolicy::queue_template<Job>, CfsPerShardPolicy::queue_template<Job>>);
static_assert(!std::is_same_v<EevdfPolicy::queue_template<Job>, EevdfPerShardPolicy::queue_template<Job>>);

namespace {

struct Failure {};

int total_passed = 0;
int total_failed = 0;

void expect_eq_sv(std::string_view actual, std::string_view expected, const char* what) {
    if (actual != expected) {
        std::fprintf(stderr, "  expectation failed: %s — got '%.*s', want '%.*s'\n", what,
                     static_cast<int>(actual.size()), actual.data(), static_cast<int>(expected.size()),
                     expected.data());
        throw Failure{};
    }
}

template <typename Body>
void run_test(const char* name, Body&& b) {
    std::fprintf(stderr, "  %s ...", name);
    try {
        b();
        ++total_passed;
        std::fprintf(stderr, " PASSED\n");
    } catch (Failure&) {
        ++total_failed;
        std::fprintf(stderr, " FAILED\n");
    }
}

void test_policy_names_are_stable() {
    expect_eq_sv(cs::Fifo::name(), "Fifo", "Fifo::name");
    expect_eq_sv(cs::Lifo::name(), "Lifo", "Lifo::name");
    expect_eq_sv(cs::RoundRobin::name(), "RoundRobin", "RoundRobin::name");
    expect_eq_sv(cs::LocalityAware::name(), "LocalityAware", "LocalityAware::name");
    expect_eq_sv(DeadlinePolicy::name(), "Deadline", "Deadline::name");
    expect_eq_sv(CfsPolicy::name(), "Cfs", "Cfs::name");
    expect_eq_sv(EevdfPolicy::name(), "Eevdf", "Eevdf::name");
    expect_eq_sv(DeadlinePerShardPolicy::name(), "DeadlinePerShard", "DeadlinePerShard::name");
    expect_eq_sv(CfsPerShardPolicy::name(), "CfsPerShard", "CfsPerShard::name");
    expect_eq_sv(EevdfPerShardPolicy::name(), "EevdfPerShard", "EevdfPerShard::name");
}

void test_pool_based_queue_constructs() {
    // Constructing one proves the alias instantiates, which a concept
    // check alone does not.
    cs::Fifo::queue_template<Job> q;
    if (q.size_approx() != 0) throw Failure{};
    if (!q.empty_approx()) throw Failure{};
    if (q.capacity() == 0) throw Failure{};
}

void test_linear_only_queue_constructs() {
    cs::LocalityAware::queue_template<Job> g;
    if (!g.empty_approx()) throw Failure{};
    if (g.capacity() == 0) throw Failure{};
}

void test_priority_keyed_queues_construct() {
    DeadlinePolicy::queue_template<Job> dq;
    CfsPolicy::queue_template<Job> cq;
    EevdfPolicy::queue_template<Job> eq;
    if (!dq.empty_approx()) throw Failure{};
    if (!cq.empty_approx()) throw Failure{};
    if (!eq.empty_approx()) throw Failure{};
    if (dq.capacity() == 0) throw Failure{};
    if (cq.capacity() == 0) throw Failure{};
    if (eq.capacity() == 0) throw Failure{};
}

void test_per_shard_queues_construct() {
    // A per-shard variant is far too large for the stack.
    auto dq = std::make_unique<DeadlinePerShardPolicy::queue_template<Job>>();
    auto cq = std::make_unique<CfsPerShardPolicy::queue_template<Job>>();
    auto eq = std::make_unique<EevdfPerShardPolicy::queue_template<Job>>();
    if (!dq->empty_approx()) throw Failure{};
    if (!cq->empty_approx()) throw Failure{};
    if (!eq->empty_approx()) throw Failure{};
    if (dq->capacity() != 4 * 64 * 16) throw Failure{};
    if (cq->capacity() != 4 * 64 * 16) throw Failure{};
    if (eq->capacity() != 4 * 64 * 16) throw Failure{};
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_scheduler_policies\n");

    run_test("policy names are stable", test_policy_names_are_stable);
    run_test("pool-based queue constructs", test_pool_based_queue_constructs);
    run_test("linear-only queue constructs", test_linear_only_queue_constructs);
    run_test("priority-keyed queues construct", test_priority_keyed_queues_construct);
    run_test("per-shard queues construct", test_per_shard_queues_construct);

    std::fprintf(stderr, "\nresult: %d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
