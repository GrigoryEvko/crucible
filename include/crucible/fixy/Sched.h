#pragma once

// Setting a real-time policy, a deadline budget, or a lower nice value needs
// CAP_SYS_NICE and fails for an unprivileged thread. A proof that claims the
// thread is pinned, real-time or niced must not exist when the syscall failed,
// so every mint here returns the errno and lets the caller decide.

#include <crucible/Platform.h>
#include <crucible/fixy/_Grant.h>
#include <crucible/fixy/Dim.h>

#include <crucible/safety/_CpuPinned.h>
#include <crucible/safety/_SchedClass.h>
#include <crucible/safety/ThreadName.h>

#include <crucible/effects/_ExecCtx.h>

#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

namespace crucible::fixy::sched {

namespace sf = ::crucible::safety;
namespace eff = ::crucible::effects;
namespace ml = ::crucible::algebra::lattices;

using sf::PinningPosture;
using sf::SchedulerPolicy_v;
using ml::AffinityMask;

// The payload of a scheduling proof carries nothing. The authority is the
// type, so a unit value is enough.
using ProofUnit = int;

// The nice value is a flat integer over [-20, 19] rather than a lattice, so
// no graded wrapper fits and this surface owns the witness.
template <int Nice>
    requires(Nice >= -20 && Nice <= 19)
struct SchedPriority final {
    static constexpr int nice = Nice;
};

namespace detail {

[[nodiscard]] consteval int sched_policy_constant(SchedulerPolicy_v policy) noexcept {
    switch (policy) {
        case SchedulerPolicy_v::Idle:
            return SCHED_IDLE;
        case SchedulerPolicy_v::Batch:
            return SCHED_BATCH;
        case SchedulerPolicy_v::Other:
            return SCHED_OTHER;
        case SchedulerPolicy_v::RoundRobin:
            return SCHED_RR;
        case SchedulerPolicy_v::Fifo:
            return SCHED_FIFO;
        case SchedulerPolicy_v::Deadline:
            return SCHED_DEADLINE;
        default:
            return -1;
    }
}

// AffinityMask::kBits is smaller than CPU_SETSIZE, so every set bit is a
// valid CPU_SET index and the loop needs no bound check of its own.
CRUCIBLE_INLINE void fill_cpu_set(AffinityMask mask, cpu_set_t& set) noexcept {
    CPU_ZERO(&set);
    for (std::uint16_t core = 0; core < AffinityMask::kBits; ++core) {
        if (((mask.words[core / 64] >> (core % 64)) & 1ULL) != 0ULL) {
            CPU_SET(static_cast<std::size_t>(core), &set);
        }
    }
}

// The kernel sched_attr layout for SCHED_DEADLINE. glibc declares no
// wrapper, so the struct is restated here and the field order is fixed by
// the kernel.
struct sched_attr_abi {
    std::uint32_t size = sizeof(sched_attr_abi);
    std::uint32_t sched_policy = 0;
    std::uint64_t sched_flags = 0;
    std::int32_t sched_nice = 0;
    std::uint32_t sched_prio = 0;
    std::uint64_t sched_runtime = 0;
    std::uint64_t sched_deadline = 0;
    std::uint64_t sched_period = 0;
};

[[nodiscard]] CRUCIBLE_INLINE int apply_deadline(std::uint64_t runtime_ns, std::uint64_t deadline_ns,
                                                 std::uint64_t period_ns) noexcept {
    sched_attr_abi attr{};
    attr.sched_policy = static_cast<std::uint32_t>(SCHED_DEADLINE);
    attr.sched_runtime = runtime_ns;
    attr.sched_deadline = deadline_ns;
    attr.sched_period = period_ns;
    return static_cast<int>(::syscall(
        SYS_sched_setattr, 0, &attr,
        0u));  // SYSCALL-CAP-OK: detail helper for mint_scheduler_policy ctx-gate (CtxFitsSchedPolicyMint, effects::Init)
}

template <SchedulerPolicy_v Policy>
[[nodiscard]] CRUCIBLE_INLINE int apply_scheduler_policy(int rt_priority, std::uint64_t runtime_ns,
                                                         std::uint64_t deadline_ns, std::uint64_t period_ns) noexcept {
    if constexpr (Policy == SchedulerPolicy_v::Deadline) {
        return apply_deadline(runtime_ns, deadline_ns, period_ns);
    } else {
        sched_param param{};
        param.sched_priority = rt_priority;
        return ::sched_setscheduler(0, sched_policy_constant(Policy), &param);
    }
}

}  // namespace detail

}  // namespace crucible::fixy::sched

namespace crucible::fixy::grant::sched {

namespace fsc = ::crucible::fixy::sched;

// A sched_setaffinity CPU pin.
struct affinity final : grant_base {};

// A sched_setscheduler or sched_setattr class change.
template <fsc::SchedulerPolicy_v Policy>
struct scheduler_policy final : grant_base {};

// A setpriority nice-value change.
template <int Nice>
struct priority final : grant_base {};

// A pthread_setname_np call.
struct thread_name final : grant_base {};

}  // namespace crucible::fixy::grant::sched

namespace crucible::fixy::grant {

namespace fsc = ::crucible::fixy::sched;

template <>
struct which_dim<sched::affinity> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <fsc::SchedulerPolicy_v Policy>
struct which_dim<sched::scheduler_policy<Policy>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <int Nice>
struct which_dim<sched::priority<Nice>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <>
struct which_dim<sched::thread_name> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {
};

}  // namespace crucible::fixy::grant

namespace crucible::fixy::sched {

template <typename Ctx, PinningPosture Posture>
concept CtxFitsAffinityMint = eff::IsExecCtx<Ctx> && (Posture != PinningPosture::NotPinned);

template <typename Ctx, SchedulerPolicy_v Policy>
concept CtxFitsSchedPolicyMint = eff::IsExecCtx<Ctx> && (detail::sched_policy_constant(Policy) >= 0);

template <typename Ctx, int Nice>
concept CtxFitsPriorityMint = eff::IsExecCtx<Ctx> && (Nice >= -20 && Nice <= 19);

// §XXI carve-out: cx=alloc — setting affinity is a kernel side effect.
template <AffinityMask Mask, PinningPosture Posture = PinningPosture::PinnedExplicit, eff::IsExecCtx Ctx>
    requires CtxFitsAffinityMint<Ctx, Posture>
[[nodiscard]] std::expected<sf::CpuPinned<Mask, Posture, ProofUnit>, int> mint_affinity(Ctx const&) noexcept {
    cpu_set_t set;
    detail::fill_cpu_set(Mask, set);
    if (::sched_setaffinity(0, sizeof(set), &set) != 0)
        [[unlikely]] {  // SYSCALL-CAP-OK: mint_affinity body, CtxFitsAffinityMint ctx-gate (effects::Init)
        return std::unexpected(errno);
    }
    return sf::mint_cpu_pinned<Mask, Posture, ProofUnit>(0);
}

// Deadline admission requires runtime < deadline <= period. The SchedClass
// return type asserts that ordering, so a budget that violates it is a
// compile error inside the mint and needs no separate concept.
template <SchedulerPolicy_v Policy, std::uint64_t RuntimeNs = 0, std::uint64_t DeadlineNs = 0,
          std::uint64_t PeriodNs = 0, eff::IsExecCtx Ctx>
// §XXI carve-out: cx=alloc — setting a scheduler policy is a kernel side effect.
    requires CtxFitsSchedPolicyMint<Ctx, Policy>
[[nodiscard]] std::expected<sf::SchedClass<Policy, ProofUnit, RuntimeNs, DeadlineNs, PeriodNs>, int>
mint_scheduler_policy(Ctx const&, int rt_priority = 0) noexcept {
    if (detail::apply_scheduler_policy<Policy>(rt_priority, RuntimeNs, DeadlineNs, PeriodNs) != 0) [[unlikely]] {
        return std::unexpected(errno);
    }
    return sf::mint_sched_class<Policy, ProofUnit, RuntimeNs, DeadlineNs, PeriodNs>(rt_priority);
}

// On Linux, PRIO_PROCESS with a who of 0 sets the nice value of the calling
// thread, not of the whole process.
// §XXI carve-out: cx=alloc — setpriority is a kernel side effect.
template <int Nice, eff::IsExecCtx Ctx>
    requires CtxFitsPriorityMint<Ctx, Nice>
[[nodiscard]] std::expected<SchedPriority<Nice>, int> mint_priority(Ctx const&) noexcept {
    errno = 0;
    if (::setpriority(PRIO_PROCESS, 0, Nice) != 0 && errno != 0) [[unlikely]] {
        return std::unexpected(errno);
    }
    return SchedPriority<Nice>{};
}

using ::crucible::safety::mint_thread_name;

// The CPU index arrives at runtime, so no compile-time pinning proof can be
// produced. This is not a mint for that reason. The gate admits only a
// background or init context. A stage worker pins itself at startup, and
// hot-path code cannot migrate a thread mid-flight.
template <typename Ctx>
concept CtxFitsRuntimeAffinity =
    ::crucible::effects::CtxOwnsAnyOf<Ctx, ::crucible::effects::Effect::Bg, ::crucible::effects::Effect::Init>;

template <eff::IsExecCtx Ctx>
    requires CtxFitsRuntimeAffinity<Ctx>
[[nodiscard]] std::expected<void, int> apply_affinity_to_cpu(Ctx const&, int cpu) noexcept {
    if (cpu < 0) return {};
    if (static_cast<unsigned>(cpu) >= static_cast<unsigned>(CPU_SETSIZE)) [[unlikely]] {
        return std::unexpected(EINVAL);
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<std::size_t>(cpu), &set);
    if (::sched_setaffinity(0, sizeof(set), &set) != 0)
        [[unlikely]] {  // SYSCALL-CAP-OK: apply_affinity_to_cpu CtxFitsRuntimeAffinity ctx-gate (Bg|Init)
        return std::unexpected(errno);
    }
    return {};
}

}  // namespace crucible::fixy::sched

namespace crucible::fixy::sched::detail::v191_self_test {

namespace gs = ::crucible::fixy::grant::sched;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(IsGrantTag<gs::affinity>);
static_assert(IsGrantTag<gs::scheduler_policy<SchedulerPolicy_v::Fifo>>);
static_assert(IsGrantTag<gs::priority<-10>>);
static_assert(IsGrantTag<gs::thread_name>);
static_assert(sizeof(gs::affinity) == 1);
static_assert(sizeof(gs::scheduler_policy<SchedulerPolicy_v::Other>) == 1);
static_assert(sizeof(gs::priority<5>) == 1);
static_assert(which_dim_v<gs::affinity> == D::SyscallSurface);
static_assert(which_dim_v<gs::scheduler_policy<SchedulerPolicy_v::Fifo>> == D::SyscallSurface);
static_assert(which_dim_v<gs::priority<-10>> == D::SyscallSurface);
static_assert(which_dim_v<gs::thread_name> == D::SyscallSurface);

static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Other) == SCHED_OTHER);
static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Fifo) == SCHED_FIFO);
static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Deadline) == SCHED_DEADLINE);

static_assert(SchedPriority<-20>::nice == -20);
static_assert(SchedPriority<19>::nice == 19);
static_assert(!std::is_same_v<SchedPriority<-10>, SchedPriority<10>>);

static_assert(std::is_same_v<decltype(mint_priority<5>(std::declval<eff::BgDrainCtx const&>())),
                             std::expected<SchedPriority<5>, int>>);

static_assert(CtxFitsRuntimeAffinity<eff::BgDrainCtx>);
static_assert(CtxFitsRuntimeAffinity<eff::ColdInitCtx>);
static_assert(!CtxFitsRuntimeAffinity<eff::HotFgCtx>, "the Fg hot path owns no Bg or Init effect — it must not be "
                                                      "able to re-pin a thread.");

inline bool runtime_smoke_test() {
    eff::BgDrainCtx bg{};

    // SCHED_OTHER and a nice value of 5 need no privilege. A thread may
    // always lower its own priority.
    auto policy = mint_scheduler_policy<SchedulerPolicy_v::Other>(bg);
    if (!policy) return false;
    if (policy->policy != SchedulerPolicy_v::Other) return false;

    auto prio = mint_priority<5>(bg);
    if (!prio) return false;
    if (prio->nice != 5) return false;

    // A self-pin to CPU 0 needs no privilege but depends on the cpuset. A
    // restricted cpuset returns EINVAL, so the result is not asserted.
    auto pin = mint_affinity<AffinityMask::single(0)>(bg);
    if (pin && !pin->is_singleton_pin) return false;

    if (!apply_affinity_to_cpu(bg, -1)) return false;
    (void)apply_affinity_to_cpu(bg, 0);

    return true;
}

}  // namespace crucible::fixy::sched::detail::v191_self_test
