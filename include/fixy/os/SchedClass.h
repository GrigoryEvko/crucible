#pragma once

// Scheduler class that a work item expects, pinned into its type.
//
// The policies form a preemption-rank chain: Idle, Batch, Other,
// RoundRobin, Fifo, Deadline.  Idle and Batch are non-interactive
// background classes.
//
// A task is runnable on a pool when the pool's policy subsumes the
// task's, that is, when the pool sits at or above the task on the chain.
// A FIFO pool hosts an OTHER task.  An OTHER pool does not host a FIFO
// task.
//
// SCHED_DEADLINE is CBS-admitted EDF.  The kernel admits a task through
// sched_setattr only when runtime < deadline <= period, so the Deadline
// case carries the three budgets as template parameters and checks the
// inequality at compile time rather than taking an EINVAL at run time.
// Every other policy leaves the three budgets zero.
//
// A value of this type is a proof: fixy::sched::mint_scheduler_policy
// returns one only after the kernel set the policy for the calling
// thread.  So the one constructor takes the key of
// fixy::sched::SchedProofDoor in fixy/os/Sched.h, and only that door
// builds the key, after the syscall returned.  A public constructor lets
// any code claim a real-time policy that no syscall set.  The proof is
// neither copyable nor movable, because the policy belongs to the thread
// that set it, and a copy or a move could carry the claim to another
// thread.  The pool rule, runnable_on, is a fact about the type, and a
// caller reads it without an object.

#include <fixy/GradedFacade.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/SchedulerPolicyLattice.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy {

using ::foundation::algebra::lattices::SchedulerPolicyLattice;
using SchedulerPolicy_v = ::foundation::algebra::lattices::SchedulerPolicy;

namespace sched {
class SchedProofDoor;
}  // namespace sched

template <SchedulerPolicy_v Policy, typename T, std::uint64_t RuntimeNs = 0, std::uint64_t DeadlineNs = 0,
          std::uint64_t PeriodNs = 0>
class [[nodiscard]] SchedClass
    : public graded_facade<::foundation::algebra::ModalityKind::Absolute, SchedulerPolicyLattice::At<Policy>, T> {
    static_assert(Policy != SchedulerPolicy_v::Deadline || (RuntimeNs < DeadlineNs && DeadlineNs <= PeriodNs),
                  "SchedClass<Deadline, ...>: SCHED_DEADLINE requires "
                  "RuntimeNs < DeadlineNs <= PeriodNs (the CBS admission inequality "
                  "the kernel enforces in sched_setattr).");
    static_assert(Policy == SchedulerPolicy_v::Deadline || (RuntimeNs == 0 && DeadlineNs == 0 && PeriodNs == 0),
                  "SchedClass<Policy, ...>: only SCHED_DEADLINE carries the "
                  "(RuntimeNs, DeadlineNs, PeriodNs) CBS budget; leave them zero for "
                  "every other policy.");

public:
    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the names this class
    // body uses unqualified are re-declared here rather than found by
    // lookup.
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute, SchedulerPolicyLattice::At<Policy>, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

    static constexpr SchedulerPolicy_v policy = Policy;
    static constexpr std::uint64_t runtime_ns = RuntimeNs;
    static constexpr std::uint64_t deadline_ns = DeadlineNs;
    static constexpr std::uint64_t period_ns = PeriodNs;

private:
    graded_type impl_;
    // No byte route builds a proof, so no proof claims a policy that no
    // syscall set.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    using key_ = ::foundation::algebra::grade_key<SchedClass>;

public:
    // The one constructor.  Only the members of SchedProofDoor build the
    // key, so only mint_scheduler_policy, after its syscall, builds a
    // proof.
    constexpr SchedClass(::foundation::algebra::grade_key<::fixy::sched::SchedProofDoor> const&,
                         T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, std::move(value), typename lattice_type::element_type{}} {}

    SchedClass(const SchedClass&) = delete("a scheduling policy belongs to the thread that set it, and a copy could "
                                           "reach another thread");
    SchedClass(SchedClass&&) = delete("a scheduling policy belongs to the thread that set it, and a move could carry "
                                      "the claim to another thread");
    SchedClass& operator=(const SchedClass&) = delete("a scheduling-class proof is not assignable");
    SchedClass& operator=(SchedClass&&) = delete("a scheduling-class proof is not assignable");
    ~SchedClass() = default;

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    template <SchedulerPolicy_v PoolPolicy>
    static constexpr bool runnable_on = SchedulerPolicyLattice::leq(Policy, PoolPolicy);
};

namespace sched_class {
template <typename T>
using Idle = SchedClass<SchedulerPolicy_v::Idle, T>;
template <typename T>
using Batch = SchedClass<SchedulerPolicy_v::Batch, T>;
template <typename T>
using Other = SchedClass<SchedulerPolicy_v::Other, T>;
template <typename T>
using RoundRobin = SchedClass<SchedulerPolicy_v::RoundRobin, T>;
template <typename T>
using Fifo = SchedClass<SchedulerPolicy_v::Fifo, T>;
template <typename T, std::uint64_t RuntimeNs, std::uint64_t DeadlineNs, std::uint64_t PeriodNs>
using Deadline = SchedClass<SchedulerPolicy_v::Deadline, T, RuntimeNs, DeadlineNs, PeriodNs>;
}  // namespace sched_class

}  // namespace fixy
