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

#include <foundation/algebra/ClaimOrientation.h>
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

// A class that preempts more is the stronger claim.
struct SchedulerPolicyLattice
    : EnumChainLattice<SchedulerPolicyLattice, SchedulerPolicy, ClaimOrientation::stronger_is_higher> {
    template <SchedulerPolicy P>
    struct At : PinnedAt<SchedulerPolicyLattice, P> {
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

// The generic walk covers the declaration order, the exhaustive axioms,
// the reflected names and the shape of every At<policy>.
static_assert(verify_chain_lattice<SchedulerPolicyLattice>(),
              "SchedulerPolicyLattice: the chain order, the pinned grades or the reflected names diverged from "
              "the SchedulerPolicy enumerator list.");

static_assert(!UnboundedLattice<SchedulerPolicyLattice>);
static_assert(!Semiring<SchedulerPolicyLattice>);

static_assert(SchedulerPolicyLattice::bottom() == SchedulerPolicy::Idle);
static_assert(SchedulerPolicyLattice::top() == SchedulerPolicy::Deadline);

// The pairs that the admission thresholds read.
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo),
              "A FIFO thread serves a round-robin requirement.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Fifo, SchedulerPolicy::RoundRobin),
              "A round-robin thread does not satisfy a FIFO requirement.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Batch),
              "SCHED_BATCH sits below the Other threshold that a timestamp-counter "
              "read requires.  The kernel may migrate the thread mid-quantum, so a "
              "sched_getcpu pin proves nothing there.");

static_assert(SchedulerPolicyLattice::name() == "SchedulerPolicyLattice");
static_assert(scheduler_policy::FifoClass::name() == "SchedulerPolicyLattice::At<Fifo>");
static_assert(SchedulerPolicyLattice::At<static_cast<SchedulerPolicy>(200)>::name() == "SchedulerPolicyLattice::At<?>");

static_assert(scheduler_policy_name(SchedulerPolicy::RoundRobin) == "RoundRobin");
static_assert(scheduler_policy_name(static_cast<SchedulerPolicy>(200)) == "<unknown SchedulerPolicy>",
              "A value outside the enum must reach the unknown-class sentinel, so a corrupt byte prints as "
              "one rather than as an empty name.");

static_assert(scheduler_policy::IdleClass::policy == SchedulerPolicy::Idle);
static_assert(scheduler_policy::DeadlineClass::policy == SchedulerPolicy::Deadline);

}  // namespace detail::scheduler_policy_lattice_self_test

}  // namespace foundation::algebra::lattices
