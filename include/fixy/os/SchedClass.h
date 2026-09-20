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
// Old spelling: include/crucible/safety/SchedClass.h.

#include <fixy/GradedFacade.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/SchedulerPolicyLattice.h>

#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy {

using ::foundation::algebra::lattices::SchedulerPolicyLattice;
using SchedulerPolicy_v = ::foundation::algebra::lattices::SchedulerPolicy;

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

public:
    constexpr SchedClass() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit SchedClass(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit SchedClass(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                            && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr SchedClass(const SchedClass&) = default;
    constexpr SchedClass(SchedClass&&) = default;
    constexpr SchedClass& operator=(const SchedClass&) = default;
    constexpr SchedClass& operator=(SchedClass&&) = default;
    ~SchedClass() = default;

    [[nodiscard]] friend constexpr bool operator==(SchedClass const& a,
                                                   SchedClass const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek();
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(SchedClass& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(SchedClass& a, SchedClass& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <SchedulerPolicy_v PoolPolicy>
    static constexpr bool runnable_on = SchedulerPolicyLattice::leq(Policy, PoolPolicy);
};

template <SchedulerPolicy_v Policy, typename T, std::uint64_t RuntimeNs = 0, std::uint64_t DeadlineNs = 0,
          std::uint64_t PeriodNs = 0, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr SchedClass<Policy, T, RuntimeNs, DeadlineNs, PeriodNs>
mint_sched_class(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return SchedClass<Policy, T, RuntimeNs, DeadlineNs, PeriodNs>{std::in_place, std::forward<Args>(args)...};
}

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

namespace detail::sched_class_layout {

template <typename T>
using OtherSc = SchedClass<SchedulerPolicy_v::Other, T>;
template <typename T>
using FifoSc = SchedClass<SchedulerPolicy_v::Fifo, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(OtherSc, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(OtherSc, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoSc, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoSc, double);

}  // namespace detail::sched_class_layout

static_assert(sizeof(SchedClass<SchedulerPolicy_v::Other, int>) == sizeof(int));
static_assert(sizeof(SchedClass<SchedulerPolicy_v::Fifo, double>) == sizeof(double));
static_assert(sizeof(SchedClass<SchedulerPolicy_v::Idle, char>) == sizeof(char));
static_assert(sizeof(SchedClass<SchedulerPolicy_v::Deadline, int, 5000, 10000, 20000>) == sizeof(int));

namespace detail::sched_class_self_test {

using OtherInt = SchedClass<SchedulerPolicy_v::Other, int>;
using FifoInt = SchedClass<SchedulerPolicy_v::Fifo, int>;
using BatchInt = SchedClass<SchedulerPolicy_v::Batch, int>;
using RrInt = SchedClass<SchedulerPolicy_v::RoundRobin, int>;
using IdleInt = SchedClass<SchedulerPolicy_v::Idle, int>;
using DeadlineInt = SchedClass<SchedulerPolicy_v::Deadline, int, 5000, 10000, 20000>;

inline constexpr FifoInt f_default{};
static_assert(f_default.peek() == 0);
static_assert(FifoInt::policy == SchedulerPolicy_v::Fifo);

inline constexpr FifoInt f_explicit{42};
static_assert(f_explicit.peek() == 42);

inline constexpr OtherInt o_in_place{std::in_place, 7};
static_assert(o_in_place.peek() == 7);

static_assert(FifoInt::modality == ::foundation::algebra::ModalityKind::Absolute);

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

[[nodiscard]] consteval bool swap_exchanges_within_same_policy() noexcept {
    FifoInt a{10};
    FifoInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_policy());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    FifoInt a{42};
    FifoInt b{42};
    FifoInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

inline constexpr auto minted_fifo = mint_sched_class<SchedulerPolicy_v::Fifo, int>(99);
static_assert(minted_fifo.peek() == 99 && minted_fifo.policy == SchedulerPolicy_v::Fifo);
inline constexpr auto minted_dl = mint_sched_class<SchedulerPolicy_v::Deadline, int, 5000, 10000, 20000>(7);
static_assert(minted_dl.peek() == 7 && minted_dl.deadline_ns == 10000);

template <typename Task, SchedulerPolicy_v PoolPolicy>
concept hostable_on = Task::template runnable_on<PoolPolicy>;

static_assert(hostable_on<OtherInt, SchedulerPolicy_v::Fifo>, "A FIFO pool MUST host an OTHER task.");
static_assert(!hostable_on<FifoInt, SchedulerPolicy_v::Other>, "An OTHER pool MUST reject a FIFO task.");

inline void runtime_smoke_test() {
    int seed = 21;
    FifoInt f{seed * 2};
    if (f.peek() != 42) std::abort();
    f.peek_mut() = 9;
    if (f.peek() != 9) std::abort();

    auto m = mint_sched_class<SchedulerPolicy_v::Other, int>(seed);
    if (std::move(m).consume() != 21) std::abort();

    FifoInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool g1 = FifoInt::runnable_on<SchedulerPolicy_v::Deadline>;
    [[maybe_unused]] bool g2 = FifoInt::runnable_on<SchedulerPolicy_v::Other>;
    if (!g1 || g2) std::abort();

    DeadlineInt dl{seed};
    if (dl.peek() != 21 || dl.runtime_ns != 5000) std::abort();

    sched_class::Idle<int> idle_task{0};
    sched_class::RoundRobin<int> rr_task{456};
    if (idle_task.peek() != 0 || rr_task.peek() != 456) std::abort();
}

}  // namespace detail::sched_class_self_test

}  // namespace fixy
