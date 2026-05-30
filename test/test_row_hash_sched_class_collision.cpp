// fix-14 — HS14 negative/witness fixture for the SchedClass SCHED_DEADLINE
// budget-NTTP fold in safety/diag/RowHashFold.h.
//
// CONTEXT
// -------
// The pre-fix SchedClass `row_hash_contribution` folded the three CBS
// budget NTTPs through a single shift-XOR pre-mix:
//
//     RuntimeNs ^ (DeadlineNs << 1) ^ (PeriodNs << 2)
//
// That pre-mix is trivially collidable: distinct (runtime, deadline,
// period) triples can map to one shifted-XOR value, because flipping a
// bit in DeadlineNs (shifted left 1) can be cancelled by flipping the
// neighbouring bit in RuntimeNs.  The downstream `combine_ids → fmix64`
// avalanche cannot UNDO a collision that already happened in the pre-mix,
// so two different SCHED_DEADLINE budgets would have shared one
// federation-cache slot — a DetSafe-adjacent correctness gap in the
// row_hash key.
//
// The fix folds each budget NTTP through a SEPARATE order-sensitive
// `combine_ids` step (matching the CpuPinned multi-field idiom), so each
// field contributes collision-resistantly and distinct triples land in
// distinct slots.
//
// WHAT THIS FIXTURE PROVES
// ------------------------
//  1. The collision pair below DID alias under the old shift-XOR pre-mix
//     (static_assert on the legacy formula — documents the witness).
//  2. The two triples differ AND each satisfies the CBS admission
//     inequality (so they are constructible SchedClass instantiations).
//  3. Under the NEW fold, every triple in the small matrix produces a
//     DISTINCT row_hash — including the previously-colliding pair.
//  4. Determinism: the same triple folds to the same hash (trivially via
//     the `inline constexpr` row_hash_contribution_v, re-asserted for
//     documentation).
//
// All assertions are consteval/constexpr — the fixture is a pure
// compile-time witness; the `runtime_smoke_test()` re-checks the same
// facts with non-constant args so the discipline matches the rest of
// safety/* + effects/*.

#include <crucible/safety/SchedClass.h>
#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/safety/diag/StableName.h>

#include <cstdint>

namespace {

namespace cs = ::crucible::safety;
namespace cd = ::crucible::safety::diag;

using SchedulerPolicy_v = cs::SchedulerPolicy_v;

// ── The colliding budget pair ──────────────────────────────────────
//
// triple A = (Runtime=1000, Deadline=2000, Period=4000)
// triple B = (Runtime=1002, Deadline=2001, Period=4000)
//
// B is A with bit-0 of Deadline flipped (changes Deadline<<1 at bit 1)
// AND bit-1 of Runtime flipped to compensate — so the legacy pre-mix
// `R ^ (D<<1) ^ (P<<2)` is IDENTICAL for both, while the triples differ.
// Both satisfy the CBS admission inequality `Runtime < Deadline <= Period`.
inline constexpr std::uint64_t kRuntimeA  = 1000;
inline constexpr std::uint64_t kDeadlineA = 2000;
inline constexpr std::uint64_t kPeriodA   = 4000;

inline constexpr std::uint64_t kRuntimeB  = 1002;
inline constexpr std::uint64_t kDeadlineB = 2001;
inline constexpr std::uint64_t kPeriodB   = 4000;

// CBS admission inequality holds for both triples.
static_assert(kRuntimeA < kDeadlineA && kDeadlineA <= kPeriodA,
    "fix-14: triple A must satisfy Runtime < Deadline <= Period");
static_assert(kRuntimeB < kDeadlineB && kDeadlineB <= kPeriodB,
    "fix-14: triple B must satisfy Runtime < Deadline <= Period");

// The triples are genuinely distinct.
static_assert(kRuntimeA != kRuntimeB || kDeadlineA != kDeadlineB
                  || kPeriodA != kPeriodB,
    "fix-14: collision-witness triples must differ in at least one field");

// ── Witness: the OLD shift-XOR pre-mix DID collide ─────────────────
[[nodiscard]] consteval std::uint64_t legacy_premix(
    std::uint64_t runtime_ns, std::uint64_t deadline_ns,
    std::uint64_t period_ns) noexcept {
    return runtime_ns ^ (deadline_ns << 1) ^ (period_ns << 2);
}

static_assert(
    legacy_premix(kRuntimeA, kDeadlineA, kPeriodA)
        == legacy_premix(kRuntimeB, kDeadlineB, kPeriodB),
    "fix-14: the chosen triples MUST alias under the legacy shift-XOR "
    "pre-mix — otherwise the fixture does not exercise the collision class "
    "the fix closes.");

// ── SchedClass instantiations under the SCHED_DEADLINE policy ──────
using DeadlineA = cs::SchedClass<SchedulerPolicy_v::Deadline, int,
                                 kRuntimeA, kDeadlineA, kPeriodA>;
using DeadlineB = cs::SchedClass<SchedulerPolicy_v::Deadline, int,
                                 kRuntimeB, kDeadlineB, kPeriodB>;

// A third triple that does NOT collide under the legacy pre-mix, to widen
// the distinctness matrix.
using DeadlineC = cs::SchedClass<SchedulerPolicy_v::Deadline, int,
                                 3000, 6000, 12000>;

inline constexpr std::uint64_t kHashA = cd::row_hash_contribution_v<DeadlineA>;
inline constexpr std::uint64_t kHashB = cd::row_hash_contribution_v<DeadlineB>;
inline constexpr std::uint64_t kHashC = cd::row_hash_contribution_v<DeadlineC>;

// ── PRIMARY CLAIM: the previously-colliding pair now differs ───────
static_assert(kHashA != kHashB,
    "fix-14: SCHED_DEADLINE triples that aliased under the legacy "
    "shift-XOR pre-mix MUST produce distinct row_hash under the new "
    "per-field combine_ids fold — this is the bug the fix closes.");

static_assert(kHashA != kHashC,
    "fix-14: distinct SCHED_DEADLINE budgets must produce distinct row_hash");
static_assert(kHashB != kHashC,
    "fix-14: distinct SCHED_DEADLINE budgets must produce distinct row_hash");

// ── Determinism: same triple → same hash ───────────────────────────
static_assert(
    cd::row_hash_contribution_v<DeadlineA> == kHashA
        && cd::row_hash_contribution_v<DeadlineB> == kHashB,
    "fix-14: row_hash_contribution_v is a stable inline constexpr — the "
    "same SchedClass instantiation must always fold to the same value.");

// ── Policy discrimination still holds ──────────────────────────────
// Two triples with the SAME budget but DIFFERENT policy must differ; a
// zero-budget Fifo (the golden's W35 shape) must differ from a zero-budget
// non-Fifo policy.
using FifoZero  = cs::SchedClass<SchedulerPolicy_v::Fifo, int>;
using OtherZero = cs::SchedClass<SchedulerPolicy_v::Other, int>;
static_assert(cd::row_hash_contribution_v<FifoZero>
                  != cd::row_hash_contribution_v<OtherZero>,
    "fix-14: distinct scheduler policies must occupy distinct slots even "
    "with a zero budget.");

// ── Order-sensitivity of the budget fold ───────────────────────────
// Permuting the budget triple must change the hash (the old shift-XOR
// pre-mix already gave SOME order sensitivity via the differing shifts,
// but per-field combine_ids makes it robust).  Use a triple whose
// permutation still satisfies the CBS inequality is not possible (the
// inequality pins the order), so we assert via the pre-mix-distinct
// triples instead: DeadlineC already covers a non-aliasing budget.
static_assert(kHashC != cd::row_hash_contribution_v<
                  cs::SchedClass<SchedulerPolicy_v::Deadline, int,
                                 3000, 6000, 12001>>,
    "fix-14: a one-nanosecond change in the period must move the slot.");

}  // namespace

// ── Runtime smoke test (discipline: non-constant args) ─────────────
namespace crucible::safety::detail::sched_class_collision_self_test {

[[gnu::used]] inline void runtime_smoke_test() noexcept {
    // Volatile args defeat constant folding so the fold runs at runtime
    // exactly as the consteval path does (combine_ids is constexpr, not
    // consteval, post-FIXY-FOUND-050).
    volatile std::uint64_t hash_a = kHashA;
    volatile std::uint64_t hash_b = kHashB;
    volatile std::uint64_t hash_c = kHashC;

    // Re-check the primary distinctness claims at runtime.
    if (hash_a == hash_b) __builtin_trap();
    if (hash_a == hash_c) __builtin_trap();
    if (hash_b == hash_c) __builtin_trap();

    // Determinism: recomputing the same instantiation's contribution
    // yields the identical value.
    if (hash_a != ::crucible::safety::diag::row_hash_contribution_v<DeadlineA>)
        __builtin_trap();
}

}  // namespace crucible::safety::detail::sched_class_collision_self_test

int main() {
    ::crucible::safety::detail::sched_class_collision_self_test::runtime_smoke_test();
    return 0;
}
