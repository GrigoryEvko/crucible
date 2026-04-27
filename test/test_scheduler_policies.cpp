// ═══════════════════════════════════════════════════════════════════
// test_scheduler_policies.cpp
//
// Verifies the seven scheduler::Fifo / Lifo / RoundRobin /
// LocalityAware / Deadline / Cfs / Eevdf policy types defined in
// concurrent/scheduler/Policies.h.
//
// What's tested:
//   1. Each policy satisfies SchedulerPolicy<P, Job> for a sample Job.
//   2. Each policy's queue_template<Job> satisfies
//      traits::PermissionedChannel.
//   3. Each policy's queue_template<Job> classifies into exactly ONE
//      of {PoolBased, LinearOnly} — the topology dichotomy.
//   4. needs_priority_key / needs_topology / is_scaffolding bits
//      match the documented contract for every shipped policy.
//   5. Deadline policy template instantiates with a user-supplied
//      KeyExtractor and continues to satisfy SchedulerPolicy.
//   6. A small runtime smoke — DefaultPolicy is LocalityAware, names
//      are stable, scaffolding flag is detectable.
//
// Reference: SEPLOG-H3 (#329), THREADING.md §5.5.2.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/scheduler/Policies.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace cs = crucible::concurrent::scheduler;
namespace ct = crucible::concurrent::traits;

// ── Sample Job types used across tests ─────────────────────────────
//
// We use a plain uint64_t as the canonical "Job" because ChaseLevDeque's
// DequeValue concept requires std::atomic<T>::is_always_lock_free.  A
// 16-byte struct fails that check without -mcx16 (cmpxchg16b).  Real
// production Jobs are typically a function-pointer or a small handle
// id; this size discipline reflects the actual constraint of the
// underlying lock-free primitives.

using Job = std::uint64_t;

// For Deadline the value IS the priority key — the simplest possible
// extractor.
struct DeadlineKey {
    static std::uint64_t key(const Job& j) noexcept { return j; }
};

// Concrete Deadline instantiation usable like the flat policies.
using DeadlinePolicy = cs::Deadline<DeadlineKey,
                                    /*NumProducers=*/4,
                                    /*NumBuckets=*/64,
                                    /*BucketCap=*/16,
                                    /*QuantumNs=*/1'000'000ULL>;

// ── Tier 1: every policy satisfies SchedulerPolicy ─────────────────

static_assert(cs::SchedulerPolicy<cs::Fifo,           Job>);
static_assert(cs::SchedulerPolicy<cs::Lifo,           Job>);
static_assert(cs::SchedulerPolicy<cs::RoundRobin,     Job>);
static_assert(cs::SchedulerPolicy<cs::LocalityAware,  Job>);
static_assert(cs::SchedulerPolicy<DeadlinePolicy,     Job>);
static_assert(cs::SchedulerPolicy<cs::Cfs,            Job>);
static_assert(cs::SchedulerPolicy<cs::Eevdf,          Job>);

// ── Tier 2: each queue_template is a PermissionedChannel ──────────

static_assert(ct::PermissionedChannel<cs::Fifo::queue_template<Job>>);
static_assert(ct::PermissionedChannel<cs::Lifo::queue_template<Job>>);
static_assert(ct::PermissionedChannel<cs::RoundRobin::queue_template<Job>>);
static_assert(ct::PermissionedChannel<cs::LocalityAware::queue_template<Job>>);
static_assert(ct::PermissionedChannel<DeadlinePolicy::queue_template<Job>>);
static_assert(ct::PermissionedChannel<cs::Cfs::queue_template<Job>>);
static_assert(ct::PermissionedChannel<cs::Eevdf::queue_template<Job>>);

// ── Tier 3: topology dichotomy is mutually exclusive ──────────────

// Pool-based: Fifo (MPMC), Lifo (ChaseLevDeque), RoundRobin (MPSC),
//             Cfs (MPMC), Eevdf (MPMC)
static_assert( ct::PoolBasedChannel<cs::Fifo::queue_template<Job>>);
static_assert(!ct::LinearOnlyChannel<cs::Fifo::queue_template<Job>>);

static_assert( ct::PoolBasedChannel<cs::Lifo::queue_template<Job>>);
static_assert(!ct::LinearOnlyChannel<cs::Lifo::queue_template<Job>>);

static_assert( ct::PoolBasedChannel<cs::RoundRobin::queue_template<Job>>);
static_assert(!ct::LinearOnlyChannel<cs::RoundRobin::queue_template<Job>>);

static_assert( ct::PoolBasedChannel<cs::Cfs::queue_template<Job>>);
static_assert(!ct::LinearOnlyChannel<cs::Cfs::queue_template<Job>>);

static_assert( ct::PoolBasedChannel<cs::Eevdf::queue_template<Job>>);
static_assert(!ct::LinearOnlyChannel<cs::Eevdf::queue_template<Job>>);

// Linear-only: LocalityAware (ShardedGrid), Deadline (CalendarGrid)
static_assert(!ct::PoolBasedChannel<cs::LocalityAware::queue_template<Job>>);
static_assert( ct::LinearOnlyChannel<cs::LocalityAware::queue_template<Job>>);

static_assert(!ct::PoolBasedChannel<DeadlinePolicy::queue_template<Job>>);
static_assert( ct::LinearOnlyChannel<DeadlinePolicy::queue_template<Job>>);

// ── Tier 4: documented contract bits match per-policy ─────────────

static_assert(!cs::Fifo::needs_priority_key);
static_assert(!cs::Fifo::needs_topology);

static_assert(!cs::Lifo::needs_priority_key);
static_assert(!cs::Lifo::needs_topology);

static_assert(!cs::RoundRobin::needs_priority_key);
static_assert(!cs::RoundRobin::needs_topology);

static_assert(!cs::LocalityAware::needs_priority_key);
static_assert( cs::LocalityAware::needs_topology);

static_assert( DeadlinePolicy::needs_priority_key);
static_assert(!DeadlinePolicy::needs_topology);

static_assert( cs::Cfs::needs_priority_key);
static_assert(!cs::Cfs::needs_topology);
static_assert( cs::Cfs::is_scaffolding);

static_assert( cs::Eevdf::needs_priority_key);
static_assert(!cs::Eevdf::needs_topology);
static_assert( cs::Eevdf::is_scaffolding);

// Non-scaffolding policies have no is_scaffolding member; the trait
// is_scaffolding_v defaults to false.
static_assert(!cs::is_scaffolding_v<cs::Fifo>);
static_assert(!cs::is_scaffolding_v<cs::Lifo>);
static_assert(!cs::is_scaffolding_v<cs::RoundRobin>);
static_assert(!cs::is_scaffolding_v<cs::LocalityAware>);
static_assert( cs::is_scaffolding_v<cs::Cfs>);
static_assert( cs::is_scaffolding_v<cs::Eevdf>);

// ── Tier 5: DefaultPolicy is LocalityAware ────────────────────────

static_assert(std::is_same_v<cs::DefaultPolicy, cs::LocalityAware>);

// ── Tier 6: distinct policy_tag types — no cross-contamination ─────
//
// The Permissioned wrappers tag their handles with the policy_tag
// chained into the user_tag.  Two different policies must produce
// distinct queue_template<Job> instantiations even for the same Job.

static_assert(!std::is_same_v<cs::Fifo::queue_template<Job>,
                              cs::Cfs::queue_template<Job>>);
static_assert(!std::is_same_v<cs::Fifo::queue_template<Job>,
                              cs::Eevdf::queue_template<Job>>);
static_assert(!std::is_same_v<cs::Cfs::queue_template<Job>,
                              cs::Eevdf::queue_template<Job>>);
static_assert(!std::is_same_v<cs::LocalityAware::queue_template<Job>,
                              DeadlinePolicy::queue_template<Job>>);

// ── Runtime smoke — names are stable, types instantiate at runtime ─

namespace {

struct Failure {};

int total_passed = 0;
int total_failed = 0;

void expect_eq_sv(std::string_view actual,
                  std::string_view expected,
                  const char* what) {
    if (actual != expected) {
        std::fprintf(stderr,
                     "  expectation failed: %s — got '%.*s', want '%.*s'\n",
                     what,
                     static_cast<int>(actual.size()), actual.data(),
                     static_cast<int>(expected.size()), expected.data());
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
    expect_eq_sv(cs::Fifo::name(),          "Fifo",          "Fifo::name");
    expect_eq_sv(cs::Lifo::name(),          "Lifo",          "Lifo::name");
    expect_eq_sv(cs::RoundRobin::name(),    "RoundRobin",    "RoundRobin::name");
    expect_eq_sv(cs::LocalityAware::name(), "LocalityAware", "LocalityAware::name");
    expect_eq_sv(DeadlinePolicy::name(),    "Deadline",      "Deadline::name");
    expect_eq_sv(cs::Cfs::name(),           "Cfs",           "Cfs::name");
    expect_eq_sv(cs::Eevdf::name(),         "Eevdf",         "Eevdf::name");
}

void test_pool_based_queue_constructs() {
    // Fifo's queue_template should be default-constructible just like
    // any PermissionedMpmcChannel — verifies the alias actually
    // instantiates at runtime, not only at concept-check time.
    cs::Fifo::queue_template<Job> q;
    if (q.size_approx() != 0) throw Failure{};
    if (!q.empty_approx())    throw Failure{};
    if (q.capacity() == 0)    throw Failure{};
}

void test_linear_only_queue_constructs() {
    cs::LocalityAware::queue_template<Job> g;
    if (!g.empty_approx())    throw Failure{};
    if (g.capacity() == 0)    throw Failure{};
}

void test_deadline_queue_constructs() {
    DeadlinePolicy::queue_template<Job> grid;
    if (!grid.empty_approx()) throw Failure{};
    if (grid.capacity() == 0) throw Failure{};
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_scheduler_policies\n");

    run_test("policy names are stable",      test_policy_names_are_stable);
    run_test("pool-based queue constructs",  test_pool_based_queue_constructs);
    run_test("linear-only queue constructs", test_linear_only_queue_constructs);
    run_test("Deadline queue constructs",    test_deadline_queue_constructs);

    std::fprintf(stderr,
                 "\nresult: %d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
