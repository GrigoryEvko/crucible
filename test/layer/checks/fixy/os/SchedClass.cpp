// The compile-time checks of fixy/os/SchedClass.h.

#include <fixy/os/SchedClass.h>

namespace fixy {

namespace detail::sched_class_layout {

template <typename T>
using OtherSc = SchedClass<SchedulerPolicy_v::Other, T>;
template <typename T>
using FifoSc = SchedClass<SchedulerPolicy_v::Fifo, T>;

// A proof keeps the size, the alignment and the trivial destructor of its
// value.  It is not trivially copyable on purpose, so the trivial-copy
// parity of CRUCIBLE_GRADED_LAYOUT_INVARIANT does not hold, and the other
// three properties are stated here one by one.
template <typename Proof, typename T>
concept KeepsTheValueLayout =
    sizeof(Proof) == sizeof(T) && alignof(Proof) == alignof(T) && std::is_trivially_destructible_v<Proof>
    && !std::is_trivially_copyable_v<Proof> && !::foundation::lifetime::ImplicitLifetimeThroughout<Proof>;

static_assert(KeepsTheValueLayout<OtherSc<char>, char> && KeepsTheValueLayout<OtherSc<int>, int>
              && KeepsTheValueLayout<FifoSc<int>, int> && KeepsTheValueLayout<FifoSc<double>, double>);

}  // namespace detail::sched_class_layout

static_assert(sizeof(SchedClass<SchedulerPolicy_v::Other, int>) == sizeof(int));
static_assert(sizeof(SchedClass<SchedulerPolicy_v::Fifo, double>) == sizeof(double));
static_assert(sizeof(SchedClass<SchedulerPolicy_v::Idle, char>) == sizeof(char));
static_assert(sizeof(SchedClass<SchedulerPolicy_v::Deadline, int, 5000, 10000, 20000>) == sizeof(int));

namespace detail::sched_class_invariants {

using OtherInt = SchedClass<SchedulerPolicy_v::Other, int>;
using FifoInt = SchedClass<SchedulerPolicy_v::Fifo, int>;
using BatchInt = SchedClass<SchedulerPolicy_v::Batch, int>;
using RrInt = SchedClass<SchedulerPolicy_v::RoundRobin, int>;
using IdleInt = SchedClass<SchedulerPolicy_v::Idle, int>;
using DeadlineInt = SchedClass<SchedulerPolicy_v::Deadline, int, 5000, 10000, 20000>;

static_assert(FifoInt::policy == SchedulerPolicy_v::Fifo);
static_assert(FifoInt::modality == ::foundation::algebra::ModalityKind::Absolute);

// The doors that let any code build a proof are closed.  Each cell names
// a route that builds one with no syscall.
static_assert(!std::is_default_constructible_v<FifoInt>);
static_assert(!std::is_constructible_v<FifoInt, int>);
static_assert(!std::is_constructible_v<FifoInt, std::in_place_t, int>);
static_assert(!std::is_copy_constructible_v<FifoInt> && !std::is_move_constructible_v<FifoInt>);
static_assert(!std::is_implicit_lifetime_v<FifoInt> && !std::is_aggregate_v<FifoInt>,
              "std::start_lifetime_as and aggregate initialization must not build a scheduling-class proof");

static_assert(DeadlineInt::runtime_ns == 5000);
static_assert(DeadlineInt::deadline_ns == 10000);
static_assert(DeadlineInt::period_ns == 20000);
static_assert(FifoInt::runtime_ns == 0 && FifoInt::deadline_ns == 0 && FifoInt::period_ns == 0,
              "Non-DEADLINE policies carry a zero CBS budget.");

static_assert(FifoInt::runnable_on<SchedulerPolicy_v::Fifo>);
static_assert(FifoInt::runnable_on<SchedulerPolicy_v::Deadline>,
              "A FIFO task runs on a DEADLINE pool — Fifo ⊑ Deadline.");
static_assert(!FifoInt::runnable_on<SchedulerPolicy_v::Other>, "A SCHED_FIFO task MUST NOT run on a SCHED_OTHER pool — "
                                                               "the pool is too weak (Fifo ⋤ Other).");
static_assert(!FifoInt::runnable_on<SchedulerPolicy_v::RoundRobin>,
              "Fifo ⋤ RoundRobin — a round-robin pool cannot host a FIFO task.");
static_assert(OtherInt::runnable_on<SchedulerPolicy_v::Other>);
static_assert(OtherInt::runnable_on<SchedulerPolicy_v::Fifo>);
static_assert(!OtherInt::runnable_on<SchedulerPolicy_v::Batch>);
static_assert(IdleInt::runnable_on<SchedulerPolicy_v::Idle>);
static_assert(IdleInt::runnable_on<SchedulerPolicy_v::Deadline>);

template <typename Task>
concept hot_path_eligible = SchedulerPolicyLattice::leq(SchedulerPolicy_v::Other, Task::policy);

static_assert(hot_path_eligible<OtherInt>);
static_assert(hot_path_eligible<FifoInt>);
static_assert(hot_path_eligible<DeadlineInt>);
static_assert(!hot_path_eligible<BatchInt>, "SCHED_BATCH is non-interactive background work and MUST be "
                                            "rejected at a HotPath stance (Batch ⊏ Other).");
static_assert(!hot_path_eligible<IdleInt>);

static_assert(!std::is_same_v<FifoInt, OtherInt>);
static_assert(!std::is_same_v<DeadlineInt, SchedClass<SchedulerPolicy_v::Deadline, int, 5000, 10000, 30000>>,
              "Two SCHED_DEADLINE tasks with different periods are DISTINCT types.");

static_assert(FifoInt::lattice_name() == "SchedulerPolicyLattice::At<Fifo>");
static_assert(OtherInt::lattice_name() == "SchedulerPolicyLattice::At<Other>");
static_assert(FifoInt::value_type_name().ends_with("int"));

template <typename Task, SchedulerPolicy_v PoolPolicy>
concept hostable_on = Task::template runnable_on<PoolPolicy>;

static_assert(hostable_on<OtherInt, SchedulerPolicy_v::Fifo>, "A FIFO pool MUST host an OTHER task.");
static_assert(!hostable_on<FifoInt, SchedulerPolicy_v::Other>, "An OTHER pool MUST reject a FIFO task.");

}  // namespace detail::sched_class_invariants

}  // namespace fixy
