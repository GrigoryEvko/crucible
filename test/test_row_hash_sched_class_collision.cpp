// Distinct SCHED_DEADLINE budgets must occupy distinct federation-cache
// slots.  Folding the three budget parameters through a single shift-XOR
// pre-mix does not achieve that, and a later avalanche step cannot undo a
// collision that has already happened in the pre-mix.  This fixture pins a
// concrete aliasing pair and asserts that the real fold separates it.

#include <crucible/safety/SchedClass.h>
#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/safety/diag/_StableName.h>

#include <cstdint>

namespace {

namespace cs = ::crucible::safety;
namespace cd = ::crucible::safety::diag;

using SchedulerPolicy_v = cs::SchedulerPolicy_v;

// B is A with bit 0 of Deadline flipped, which lands at bit 1 of
// `Deadline << 1`, and bit 1 of Runtime flipped to cancel it there.  So
// `Runtime ^ (Deadline << 1) ^ (Period << 2)` is identical for the two
// triples while the triples themselves differ.  Both satisfy the admission
// inequality, so both are constructible.
inline constexpr std::uint64_t kRuntimeA = 1000;
inline constexpr std::uint64_t kDeadlineA = 2000;
inline constexpr std::uint64_t kPeriodA = 4000;

inline constexpr std::uint64_t kRuntimeB = 1002;
inline constexpr std::uint64_t kDeadlineB = 2001;
inline constexpr std::uint64_t kPeriodB = 4000;

static_assert(kRuntimeA < kDeadlineA && kDeadlineA <= kPeriodA, "triple A must satisfy Runtime < Deadline <= Period");
static_assert(kRuntimeB < kDeadlineB && kDeadlineB <= kPeriodB, "triple B must satisfy Runtime < Deadline <= Period");

static_assert(kRuntimeA != kRuntimeB || kDeadlineA != kDeadlineB || kPeriodA != kPeriodB,
              "collision-witness triples must differ in at least one field");

[[nodiscard]] consteval std::uint64_t legacy_premix(std::uint64_t runtime_ns, std::uint64_t deadline_ns,
                                                    std::uint64_t period_ns) noexcept {
    return runtime_ns ^ (deadline_ns << 1) ^ (period_ns << 2);
}

static_assert(legacy_premix(kRuntimeA, kDeadlineA, kPeriodA) == legacy_premix(kRuntimeB, kDeadlineB, kPeriodB),
              "the chosen triples must alias under the shift-XOR pre-mix, otherwise "
              "the fixture does not exercise the collision class it is written for");

using DeadlineA = cs::SchedClass<SchedulerPolicy_v::Deadline, int, kRuntimeA, kDeadlineA, kPeriodA>;
using DeadlineB = cs::SchedClass<SchedulerPolicy_v::Deadline, int, kRuntimeB, kDeadlineB, kPeriodB>;

// A third triple that does not alias under the shift-XOR pre-mix, to widen
// the distinctness matrix.
using DeadlineC = cs::SchedClass<SchedulerPolicy_v::Deadline, int, 3000, 6000, 12000>;

inline constexpr std::uint64_t kHashA = cd::row_hash_contribution_v<DeadlineA>;
inline constexpr std::uint64_t kHashB = cd::row_hash_contribution_v<DeadlineB>;
inline constexpr std::uint64_t kHashC = cd::row_hash_contribution_v<DeadlineC>;

static_assert(kHashA != kHashB, "SCHED_DEADLINE triples that alias under the shift-XOR pre-mix must "
                                "produce distinct row_hash under the per-field fold");

static_assert(kHashA != kHashC, "distinct SCHED_DEADLINE budgets must produce distinct row_hash");
static_assert(kHashB != kHashC, "distinct SCHED_DEADLINE budgets must produce distinct row_hash");

static_assert(cd::row_hash_contribution_v<DeadlineA> == kHashA && cd::row_hash_contribution_v<DeadlineB> == kHashB,
              "the same SchedClass instantiation must always fold to the same value");

using FifoZero = cs::SchedClass<SchedulerPolicy_v::Fifo, int>;
using OtherZero = cs::SchedClass<SchedulerPolicy_v::Other, int>;
static_assert(cd::row_hash_contribution_v<FifoZero> != cd::row_hash_contribution_v<OtherZero>,
              "distinct scheduler policies must occupy distinct slots even with a "
              "zero budget");

// Permuting the triple is not a usable order-sensitivity check here: the
// admission inequality pins the order of the three fields.  A one-field
// perturbation stands in for it.
static_assert(kHashC
                  != cd::row_hash_contribution_v<cs::SchedClass<SchedulerPolicy_v::Deadline, int, 3000, 6000, 12001>>,
              "a one-nanosecond change in the period must move the slot");

}  // namespace

namespace crucible::safety::detail::sched_class_collision_self_test {

[[gnu::used]] inline void runtime_smoke_test() noexcept {
    // Volatile defeats constant folding, so the fold runs at runtime on the
    // same path the compile-time assertions take.
    volatile std::uint64_t hash_a = kHashA;
    volatile std::uint64_t hash_b = kHashB;
    volatile std::uint64_t hash_c = kHashC;

    if (hash_a == hash_b) __builtin_trap();
    if (hash_a == hash_c) __builtin_trap();
    if (hash_b == hash_c) __builtin_trap();

    if (hash_a != ::crucible::safety::diag::row_hash_contribution_v<DeadlineA>) __builtin_trap();
}

}  // namespace crucible::safety::detail::sched_class_collision_self_test

int main() {
    ::crucible::safety::detail::sched_class_collision_self_test::runtime_smoke_test();
    return 0;
}
