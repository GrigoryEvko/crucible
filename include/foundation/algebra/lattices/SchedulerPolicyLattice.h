#pragma once

// Chain over Linux scheduling policies, ordered by how aggressively a
// thread in the class preempts the default time-shared pool.  bottom is
// Idle and top is Deadline.  leq(weak, strong) reads "a weaker
// requirement is satisfied by a stronger provider", so a FIFO thread
// serves a round-robin requirement.
//
// These ordinals are the preemption rank.  They are not the SCHED_*
// syscall constants, which run in a different order.  Code that reaches
// the sched_setattr boundary translates.
//
// SCHED_FIFO and its siblings are preprocessor macros, so the
// enumerators are spelled in PascalCase.  The kernel spellings appear
// only inside string literals, which the preprocessor leaves alone.
//
// Old spelling: include/crucible/algebra/lattices/SchedulerPolicyLattice.h.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class SchedulerPolicy : std::uint8_t {
    Idle = 0,  // SCHED_IDLE — runs only when the CPU is otherwise idle
    Batch = 1,  // SCHED_BATCH — throughput-oriented, no interactive boost
    Other = 2,  // SCHED_OTHER — the default time-shared class
    RoundRobin = 3,  // SCHED_RR — real time, time-sliced among equal priorities
    Fifo = 4,  // SCHED_FIFO — real time, runs until it yields or blocks
    Deadline = 5,  // SCHED_DEADLINE — admitted earliest-deadline-first
};

inline constexpr std::size_t scheduler_policy_count = ::foundation::reflect::enum_count<SchedulerPolicy>;

// The identifier of p, or "<unknown SchedulerPolicy>" for a value
// outside the enum.
[[nodiscard]] consteval std::string_view scheduler_policy_name(SchedulerPolicy p) noexcept {
    return ::foundation::reflect::enum_name(p);
}

struct SchedulerPolicyLattice : ChainLatticeOps<SchedulerPolicy> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return SchedulerPolicy::Idle; }
    [[nodiscard]] static constexpr element_type top() noexcept { return SchedulerPolicy::Deadline; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "SchedulerPolicyLattice"; }

    template <SchedulerPolicy P>
    struct AtElement : PinnedElement<P> {
        using scheduler_policy_value_type = SchedulerPolicy;
    };

    template <SchedulerPolicy P>
    struct At : PinnedAt<SchedulerPolicyLattice, P, AtElement<P>> {
        static constexpr SchedulerPolicy policy = P;
    };
};

namespace scheduler_policy {
using IdleClass = SchedulerPolicyLattice::At<SchedulerPolicy::Idle>;
using BatchClass = SchedulerPolicyLattice::At<SchedulerPolicy::Batch>;
using OtherClass = SchedulerPolicyLattice::At<SchedulerPolicy::Other>;
using RoundRobinClass = SchedulerPolicyLattice::At<SchedulerPolicy::RoundRobin>;
using FifoClass = SchedulerPolicyLattice::At<SchedulerPolicy::Fifo>;
using DeadlineClass = SchedulerPolicyLattice::At<SchedulerPolicy::Deadline>;
}  // namespace scheduler_policy

namespace detail::scheduler_policy_lattice_self_test {

static_assert(scheduler_policy_count == 6, "SchedulerPolicy catalog diverged from {Idle, Batch, Other, "
                                           "RoundRobin, Fifo, Deadline}.  A new class needs the admission "
                                           "thresholds that name a class rechecked.");

// Each enumerator renders as the identifier it declares, and a value
// outside the enum reaches the sentinel.  These pins replace the two
// coverage walks the hand-written name switches needed: with the name
// read from the enumerator, those walks answered true by construction.
static_assert(scheduler_policy_name(SchedulerPolicy::Idle) == "Idle");
static_assert(scheduler_policy_name(SchedulerPolicy::Batch) == "Batch");
static_assert(scheduler_policy_name(SchedulerPolicy::Other) == "Other");
static_assert(scheduler_policy_name(SchedulerPolicy::RoundRobin) == "RoundRobin");
static_assert(scheduler_policy_name(SchedulerPolicy::Fifo) == "Fifo");
static_assert(scheduler_policy_name(SchedulerPolicy::Deadline) == "Deadline");
static_assert(scheduler_policy_name(static_cast<SchedulerPolicy>(200)) == "<unknown SchedulerPolicy>",
              "A value outside the enum must reach the unknown-class sentinel, so a corrupt byte prints as "
              "one rather than as an empty name.");

static_assert(Lattice<SchedulerPolicyLattice>);
static_assert(BoundedLattice<SchedulerPolicyLattice>);
static_assert(Lattice<scheduler_policy::IdleClass>);
static_assert(Lattice<scheduler_policy::DeadlineClass>);
static_assert(BoundedLattice<scheduler_policy::DeadlineClass>);

static_assert(!UnboundedLattice<SchedulerPolicyLattice>);
static_assert(!Semiring<SchedulerPolicyLattice>);

static_assert(std::is_empty_v<scheduler_policy::IdleClass::element_type>);
static_assert(std::is_empty_v<scheduler_policy::OtherClass::element_type>);
static_assert(std::is_empty_v<scheduler_policy::FifoClass::element_type>);
static_assert(std::is_empty_v<scheduler_policy::DeadlineClass::element_type>);

static_assert(verify_chain_lattice_exhaustive<SchedulerPolicyLattice>(),
              "SchedulerPolicyLattice chain-order lattice axioms fail at some "
              "triple.  The defect is in leq, join, meet or the enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<SchedulerPolicyLattice>(),
              "SchedulerPolicyLattice chain fails distributivity at some triple.  "
              "A chain order always satisfies it, so the defect is in join or "
              "meet.");

static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Idle, SchedulerPolicy::Batch));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Batch, SchedulerPolicy::Other));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::RoundRobin));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Fifo, SchedulerPolicy::Deadline));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Idle, SchedulerPolicy::Deadline));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo),
              "A FIFO thread serves a round-robin requirement.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Fifo, SchedulerPolicy::RoundRobin),
              "A round-robin thread does not satisfy a FIFO requirement.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Deadline, SchedulerPolicy::Idle));

static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Other));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::RoundRobin));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Fifo));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Deadline));
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Batch),
              "SCHED_BATCH sits below the Other threshold that a timestamp-counter "
              "read requires.  The kernel may migrate the thread mid-quantum, so a "
              "sched_getcpu pin proves nothing there.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Idle));

static_assert(SchedulerPolicyLattice::bottom() == SchedulerPolicy::Idle);
static_assert(SchedulerPolicyLattice::top() == SchedulerPolicy::Deadline);

static_assert(SchedulerPolicyLattice::join(SchedulerPolicy::Idle, SchedulerPolicy::Deadline)
              == SchedulerPolicy::Deadline);
static_assert(SchedulerPolicyLattice::join(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo)
              == SchedulerPolicy::Fifo);
static_assert(SchedulerPolicyLattice::meet(SchedulerPolicy::Idle, SchedulerPolicy::Deadline) == SchedulerPolicy::Idle);
static_assert(SchedulerPolicyLattice::meet(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo)
              == SchedulerPolicy::RoundRobin);

static_assert(SchedulerPolicyLattice::name() == "SchedulerPolicyLattice");
static_assert(scheduler_policy::IdleClass::name() == "SchedulerPolicyLattice::At<Idle>");
static_assert(scheduler_policy::BatchClass::name() == "SchedulerPolicyLattice::At<Batch>");
static_assert(scheduler_policy::OtherClass::name() == "SchedulerPolicyLattice::At<Other>");
static_assert(scheduler_policy::RoundRobinClass::name() == "SchedulerPolicyLattice::At<RoundRobin>");
static_assert(scheduler_policy::FifoClass::name() == "SchedulerPolicyLattice::At<Fifo>");
static_assert(scheduler_policy::DeadlineClass::name() == "SchedulerPolicyLattice::At<Deadline>");

// A value outside the enum reaches the At sentinel rather than an empty
// name, which is the other half of what the retired coverage walk over
// At<P>::name() said.
static_assert(SchedulerPolicyLattice::At<static_cast<SchedulerPolicy>(200)>::name() == "SchedulerPolicyLattice::At<?>");

static_assert(scheduler_policy::IdleClass::policy == SchedulerPolicy::Idle);
static_assert(scheduler_policy::BatchClass::policy == SchedulerPolicy::Batch);
static_assert(scheduler_policy::OtherClass::policy == SchedulerPolicy::Other);
static_assert(scheduler_policy::RoundRobinClass::policy == SchedulerPolicy::RoundRobin);
static_assert(scheduler_policy::FifoClass::policy == SchedulerPolicy::Fifo);
static_assert(scheduler_policy::DeadlineClass::policy == SchedulerPolicy::Deadline);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using FifoGraded = Graded<ModalityKind::Absolute, scheduler_policy::FifoClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoGraded, int);

template <typename T_>
using DeadlineGraded = Graded<ModalityKind::Absolute, scheduler_policy::DeadlineClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(DeadlineGraded, EightByteValue);

}  // namespace detail::scheduler_policy_lattice_self_test

}  // namespace foundation::algebra::lattices
