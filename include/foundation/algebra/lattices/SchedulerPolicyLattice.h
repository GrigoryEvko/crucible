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

}  // namespace foundation::algebra::lattices
