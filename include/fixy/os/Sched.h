#pragma once

// Setting a real-time policy, a deadline budget, or a lower nice value needs
// CAP_SYS_NICE and fails for an unprivileged thread. A proof that claims the
// thread is pinned, real-time or niced must not exist when the syscall failed,
// so every mint here returns the errno and lets the caller decide.
//
// Old spelling: include/crucible/fixy/Sched.h.
//
// The grant tags grant::sched::{affinity, scheduler_policy, priority,
// thread_name} and their four which_dim rows are decoration: nothing
// outside the header's own self-test reads them, so the port drops both
// and the eleven assertions that read them.

#include <fixy/os/CpuPinned.h>
#include <fixy/os/SchedClass.h>
#include <fixy/os/ThreadName.h>
#include <foundation/Platform.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>

#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

namespace fixy::sched {

// The old tree read these names out of its safety namespace, which is
// fixy now.  Keeping the alias keeps every use site below spelled as it
// was.
namespace sf = ::fixy;
namespace eff = ::foundation::effects;
namespace ml = ::foundation::algebra::lattices;

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
    using row_discipline = SchedPriority;
    using row_payload = ::foundation::diag::row_payloads<>;
};

namespace detail {

// fill_cpu_set moved to fixy/os/CpuPinned.h, beside the pin proof it
// serves.  mint_affinity was its only caller, and the proof's sole
// constructor is now private to that mint, so the syscall and the type
// it authorizes have to share a header.

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
        return ::sched_setscheduler(
            0, sched_policy_constant(Policy),
            &param);  // SYSCALL-CAP-OK: detail helper for mint_scheduler_policy ctx-gate (CtxFitsSchedPolicyMint)
    }
}

}  // namespace detail

// CtxFitsAffinityMint moved to fixy/os/CpuPinned.h with mint_affinity's
// declaration, which had to move because the proof names that mint as
// its sole friend and a friend must already have been declared.  The
// name is unqualified below through `using sf::CtxFitsAffinityMint`, so
// every call site reads as it did.

// The second conjunct is not a restatement of the enum's own range,
// though it reads like one.  It is the
// only compile-time rejection of a policy outside the enum: SchedClass
// accepts any SchedulerPolicy_v value, including a cast one, and renders
// it as the At<?> sentinel.  Without this conjunct,
// mint_scheduler_policy<static_cast<SchedulerPolicy_v>(200)> compiles,
// calls sched_setscheduler with a policy of -1, and turns a compile
// error into a runtime EINVAL.  The two pins below witness both arms.
template <typename Ctx, SchedulerPolicy_v Policy>
concept CtxFitsSchedPolicyMint = eff::IsExecCtx<Ctx> && (detail::sched_policy_constant(Policy) >= 0);

// The [-20, 19] bound here reads like a restatement of the one on
// SchedPriority<Nice>.  It is not.  The mint names SchedPriority<Nice>
// in its return type, so without this conjunct an out-of-range nice
// reaches that return type during substitution, and a constraint
// failure on a CLASS template is not in the immediate context of the
// function template.  GCC 16 reports it as a hard error rather than
// discarding the candidate, so the call site cannot recover and no
// overload set can absorb it.  Measured, not reasoned: the deletion was
// written, compiled, and reverted on the diagnostic.
template <typename Ctx, int Nice>
concept CtxFitsPriorityMint = eff::IsExecCtx<Ctx> && (Nice >= -20 && Nice <= 19);

// The definition of the mint declared in fixy/os/CpuPinned.h.  It lives
// here, beside the other scheduling mints, and it is the sole friend of
// CpuPinned's only constructor.  The default template argument belongs
// to the declaration and must not be repeated, and the requires-clause
// is spelled exactly as it is there so the two match.
//
// This is the one door that earns a CpuPinned.  The proof comes back
// only after sched_setaffinity returned 0 for this same mask, and no
// other path to one exists: the three public constructors the port
// inherited are gone.
template <AffinityMask Mask, PinningPosture Posture, eff::IsExecCtx Ctx>
    requires ::fixy::CtxFitsAffinityMint<Ctx, Posture>
[[nodiscard]] std::expected<sf::CpuPinned<Mask, Posture, sf::PinProofUnit>, int> mint_affinity(Ctx const&) noexcept {
    if (const int failure = sf::detail::pin_calling_thread(Mask); failure != 0) [[unlikely]] {
        return std::unexpected(failure);
    }
    return sf::CpuPinned<Mask, Posture, sf::PinProofUnit>{0};
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
    if (::setpriority(PRIO_PROCESS, 0, Nice) != 0 && errno != 0)
        [[unlikely]] {  // SYSCALL-CAP-OK: mint_priority body, CtxFitsPriorityMint ctx-gate
        return std::unexpected(errno);
    }
    return SchedPriority<Nice>{};
}

using ::fixy::mint_thread_name;

// The CPU index arrives at runtime, so no compile-time pinning proof can be
// produced. This is not a mint for that reason. The gate admits only a
// background or init context. A stage worker pins itself at startup, and
// hot-path code cannot migrate a thread mid-flight.
template <typename Ctx>
concept CtxFitsRuntimeAffinity = eff::CtxOwnsAnyOf<Ctx, eff::Effect::Bg, eff::Effect::Init>;

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

}  // namespace fixy::sched

namespace fixy::sched::detail::scheduler_mint_invariants {

// The eleven grant-tag assertions the old self-test carried are not
// ported, because the tags they read are not ported.

// BgDrainCtx, ColdInitCtx and HotFgCtx belong to a fixy/Ctx.h the tree
// does not have yet.  These three stand in until it lands, in the shape
// foundation's own Ctx.h self-test uses.  They are scaffolding, not a
// second spelling of the named contexts.
using BgWitness = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using InitWitness = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;
using FgWitness = eff::ExecCtx<>;

static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Other) == SCHED_OTHER);
static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Fifo) == SCHED_FIFO);
static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Deadline) == SCHED_DEADLINE);

// The kept conjunct, both arms.  Deleting it would turn the second pin
// into a runtime EINVAL.
static_assert(CtxFitsSchedPolicyMint<BgWitness, SchedulerPolicy_v::Fifo>);
static_assert(!CtxFitsSchedPolicyMint<BgWitness, static_cast<SchedulerPolicy_v>(200)>,
              "a policy outside the enum has no SCHED_* constant, and this concept is the only thing that "
              "rejects it before the syscall.");

static_assert(SchedPriority<-20>::nice == -20);
static_assert(SchedPriority<19>::nice == 19);
static_assert(!std::is_same_v<SchedPriority<-10>, SchedPriority<10>>);

static_assert(
    std::is_same_v<decltype(mint_priority<5>(std::declval<BgWitness const&>())), std::expected<SchedPriority<5>, int>>);

// The kept nice bound, both arms.  Deleting it makes an out-of-range
// nice a hard error at the call site rather than a rejected candidate.
static_assert(CtxFitsPriorityMint<BgWitness, 5>);
static_assert(!CtxFitsPriorityMint<BgWitness, 50>,
              "a nice outside [-20, 19] must be rejected by this concept.  Leaving it to SchedPriority's own "
              "requires-clause in the return type does not work: a constraint failure on a class template is "
              "not in the immediate context of the function template, so GCC 16 raises a hard error instead "
              "of discarding the candidate.");

static_assert(CtxFitsRuntimeAffinity<BgWitness>);
static_assert(CtxFitsRuntimeAffinity<InitWitness>);
static_assert(!CtxFitsRuntimeAffinity<FgWitness>, "the Fg hot path owns no Bg or Init effect — it must not be "
                                                  "able to re-pin a thread.");

}  // namespace fixy::sched::detail::scheduler_mint_invariants
