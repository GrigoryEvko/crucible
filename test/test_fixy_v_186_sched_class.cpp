// FIXY-V-186 sentinel TU: safety/SchedClass.h — the Graded<Absolute,
// SchedulerPolicyLattice::At<Policy>, T> regime-1 carrier for the V-183
// SchedulerPolicy axis, plus safety/IsSchedClass.h (concept extractor) and
// the row_hash_contribution<safety::SchedClass<Policy, Inner, R, D, P>>
// federation-cache discriminator wired in safety/diag/RowHashFold.h
// (salt 0x31 — the row_hash V-183 deferred).
//
// This TU forces every header-embedded static_assert to compile under the
// project warning flags (header-only static_asserts are otherwise
// unverified — feedback_header_only_static_assert_blind_spot) AND adds the
// cross-cutting checks the wrapper header cannot self-contain: row_hash
// distinctness (per-policy + per-DEADLINE-budget) + nesting-order
// sensitivity, salt-disjointness from the adjacent-salt wrapper
// (ClockSource, 0x30), and the runtime smoke tests.
//
// SchedClass is a CHAIN wrapper (preemption rank): runnable_on<PoolPolicy>
// = leq(Policy, PoolPolicy) (task ⊑ pool).  The SCHED_DEADLINE case carries
// (RuntimeNs, DeadlineNs, PeriodNs) NTTPs with a compile-time CBS-admission
// assert.  No DimensionAxis enumerator (off-tree Synchronization-
// neighborhood wrapper), so no DimensionTraits quadruple to assert.
//
// HS14 negative coverage lives in three distinct-mismatch-class fixtures:
//   - neg_sched_class_fifo_task_on_other_pool         (pool-fit: Fifo ⋤ Other)
//   - neg_sched_class_deadline_runtime_gt_deadline    (CBS assert: runtime > deadline)
//   - neg_sched_class_batch_on_hotpath                (HotPath stance rejects BATCH)

#include <crucible/safety/SchedClass.h>
#include <crucible/safety/IsSchedClass.h>
#include <crucible/safety/ClockSource.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf  = ::crucible::safety;
namespace ex  = ::crucible::safety::extract;
namespace dg  = ::crucible::safety::diag;
using Sp_t = sf::SchedulerPolicy_v;
using Cs_t = sf::ClockSource_v;

// ── Regime-1 sizeof preservation — distinct policies, same payload ─
static_assert(sizeof(sf::SchedClass<Sp_t::Other, int>)    == sizeof(int));
static_assert(sizeof(sf::SchedClass<Sp_t::Fifo,  double>) == sizeof(double));
static_assert(sizeof(sf::SchedClass<Sp_t::Idle,  char>)   == sizeof(char));
// The DEADLINE budget NTTPs add zero storage.
static_assert(sizeof(sf::SchedClass<Sp_t::Deadline, int, 5'000, 10'000, 20'000>) == sizeof(int));

// ── Pool-vs-task fit (runnable_on, task ⊑ pool) ─────────────────────
static_assert( sf::SchedClass<Sp_t::Fifo, int>::runnable_on<Sp_t::Fifo>);
static_assert( sf::SchedClass<Sp_t::Fifo, int>::runnable_on<Sp_t::Deadline>);
static_assert(!sf::SchedClass<Sp_t::Fifo, int>::runnable_on<Sp_t::Other>,
    "A SCHED_FIFO task MUST NOT run on a SCHED_OTHER pool (Fifo ⋤ Other).");
static_assert( sf::SchedClass<Sp_t::Other, int>::runnable_on<Sp_t::Fifo>);
static_assert(!sf::SchedClass<Sp_t::Other, int>::runnable_on<Sp_t::Batch>);

// ── DEADLINE budget exposure + zero-budget for non-Deadline ─────────
static_assert(sf::SchedClass<Sp_t::Deadline, int, 5'000, 10'000, 20'000>::deadline_ns == 10'000);
static_assert(sf::SchedClass<Sp_t::Fifo, int>::runtime_ns == 0);

// ── IsSchedClass concept extractor ──────────────────────────────────
static_assert(ex::IsSchedClass<sf::SchedClass<Sp_t::Fifo, int>>);
static_assert(!ex::IsSchedClass<int>);
static_assert(std::is_same_v<ex::sched_class_value_t<sf::SchedClass<Sp_t::Other, double>>, double>);
static_assert(ex::sched_class_policy_v<sf::SchedClass<Sp_t::Deadline, int, 5'000, 10'000, 20'000>>
              == Sp_t::Deadline);

// ── row_hash distinctness — different policies / payloads ───────────
static_assert(dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Fifo, int>>
              != dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Other, int>>,
    "SchedClass<Fifo,int> and <Other,int> MUST hash differently — the "
    "policy salt discriminates federation-cache slots.");
static_assert(dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Fifo, int>>
              != dg::row_hash_contribution_v<int>,
    "SchedClass<Fifo,int> MUST hash differently from bare int — the wrapper "
    "tag (0x31) discriminates the wrapped value.");

// ── DEADLINE budget distinctness in the hash ────────────────────────
static_assert(
    dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Deadline, int, 5'000, 10'000, 20'000>>
    != dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Deadline, int, 5'000, 10'000, 30'000>>,
    "Two SCHED_DEADLINE tasks with different periods MUST hash to distinct "
    "slots — the budget NTTPs are folded into the row_hash.");

// ── Salt-disjointness from the adjacent-salt wrapper (ClockSource 0x30) ─
static_assert(dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Fifo, int>>
              != dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, int>>,
    "SchedClass (0x31) and ClockSource (0x30) are distinct wrappers — their "
    "per-wrapper salts MUST discriminate even at adjacent high bytes.");

// ── Nesting-order sensitivity (§XVI / GAPS-029) ─────────────────────
static_assert(
    dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, sf::SchedClass<Sp_t::Fifo, int>>>
    != dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Fifo, sf::ClockSource<Cs_t::Boot, int>>>,
    "ClockSource<Boot, SchedClass<Fifo,int>> and SchedClass<Fifo, "
    "ClockSource<Boot,int>> MUST hash differently — row_hash is "
    "order-sensitive per the canonical wrapper-nesting discipline.");

}  // namespace

int main() {
    ::crucible::safety::detail::sched_class_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_sched_class_smoke_test()) return 1;
    return 0;
}
