#pragma once

// ── crucible::fixy::sched — the scheduler-control surface ────────────
//
// FIXY-V-191 (Agent 6 §3.5).  The producer side of the V-187 CpuPinned
// proof and the scheduler-policy / priority / thread-name setters, plus
// the grant tags a fixy::fn signature uses to DECLARE a scheduling effect.
// Sibling of fixy/Time.h (V-190).
//
// ── Two complementary surfaces ──────────────────────────────────────
//
//   grant::sched::affinity                → SyscallSurface (sched_setaffinity)
//   grant::sched::scheduler_policy<Policy>→ SyscallSurface (sched_setscheduler/setattr)
//   grant::sched::priority<Nice>          → SyscallSurface (setpriority)
//   grant::sched::thread_name             → SyscallSurface (pthread_setname_np)
//
//   mint_affinity<Mask, Posture>(ctx)         → expected<CpuPinned<Mask,Posture>, int>
//   mint_scheduler_policy<Policy,R,D,P>(ctx)  → expected<SchedClass<Policy,int,R,D,P>, int>
//   mint_priority<Nice>(ctx)                  → expected<SchedPriority<Nice>, int>
//   mint_thread_name<Name>(ctx)               → ThreadNamed<Name>   (re-export of V-189)
//
// ── Why std::expected ───────────────────────────────────────────────
//
// sched_setscheduler(SCHED_FIFO/RR), sched_setattr(SCHED_DEADLINE), and
// lowering a nice value all require CAP_SYS_NICE and fail unprivileged.  A
// proof that CLAIMS "this thread is FIFO / pinned / niced" must NOT be
// mintable when the syscall failed — so the errno is surfaced and the
// caller decides.  mint_affinity is the producer of exactly the move-only
// CpuPinned proof that fixy/Time.h's mint_tsc_reader consumes (V-196).
//
// ── The CBS-admission invariant rides in the return type ────────────
//
// mint_scheduler_policy<Deadline, R, D, P> returns SchedClass<Deadline,
// int, R, D, P>, and SchedClass's own static_assert(R < D <= P) (V-186)
// fires at instantiation — a budget-violating deadline is a compile error
// inside the mint, no extra concept needed.
//
// HS14 negative coverage (two distinct mismatch classes per NEW mint):
//   mint_affinity         : non-ExecCtx · Posture == NotPinned
//   mint_scheduler_policy : non-ExecCtx · Deadline CBS budget violation
//   mint_priority         : Nice out of [-20,19] · non-ExecCtx
// (mint_thread_name is a re-export of V-189's safety::mint_thread_name —
//  its HS14 fixtures live with that mint.)

#include <crucible/Platform.h>
#include <crucible/fixy/Grant.h>                            // grant_base, which_dim, IsGrantTag
#include <crucible/fixy/Dim.h>                              // dim::DimensionAxis

#include <crucible/safety/CpuPinned.h>                      // CpuPinned, PinningPosture, AffinityMask, mint_cpu_pinned
#include <crucible/safety/SchedClass.h>                     // SchedClass, SchedulerPolicy_v, mint_sched_class
#include <crucible/safety/ThreadName.h>                     // mint_thread_name (V-189 re-export)

#include <crucible/effects/ExecCtx.h>                       // effects::IsExecCtx

#include <sched.h>                                          // sched_setaffinity/setscheduler, sched_param, SCHED_*, cpu_set_t
#include <sys/resource.h>                                   // setpriority, PRIO_PROCESS
#include <sys/syscall.h>                                    // SYS_sched_setattr
#include <unistd.h>                                         // syscall

#include <cerrno>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

namespace crucible::fixy::sched {

namespace sf  = ::crucible::safety;
namespace eff = ::crucible::effects;
namespace ml  = ::crucible::algebra::lattices;

using sf::PinningPosture;
using sf::SchedulerPolicy_v;
using ml::AffinityMask;

// The proof carrier for the affinity / scheduler-class tokens — a trivial
// unit value (the proof's authority is its TYPE, not its payload).
using ProofUnit = int;

// ── SchedPriority<Nice> — the applied-nice-value witness ────────────
//
// There is no safety/ Graded wrapper for the POSIX nice value (it is a
// flat [-20, 19] integer, not a lattice), so the sched surface owns this
// small phantom proof.
template <int Nice>
    requires (Nice >= -20 && Nice <= 19)
struct SchedPriority final {
    static constexpr int nice = Nice;
};

namespace detail {

// Map the lattice policy onto its SCHED_* kernel constant.
[[nodiscard]] consteval int sched_policy_constant(SchedulerPolicy_v policy) noexcept {
    switch (policy) {
        case SchedulerPolicy_v::Idle:       return SCHED_IDLE;
        case SchedulerPolicy_v::Batch:      return SCHED_BATCH;
        case SchedulerPolicy_v::Other:      return SCHED_OTHER;
        case SchedulerPolicy_v::RoundRobin: return SCHED_RR;
        case SchedulerPolicy_v::Fifo:       return SCHED_FIFO;
        case SchedulerPolicy_v::Deadline:   return SCHED_DEADLINE;
        default:                            return -1;
    }
}

// Build a cpu_set_t from the 256-bit AffinityMask (kBits = 256 < CPU_SETSIZE).
CRUCIBLE_INLINE void fill_cpu_set(AffinityMask mask, cpu_set_t& set) noexcept {
    CPU_ZERO(&set);
    for (std::uint16_t core = 0; core < AffinityMask::kBits; ++core) {
        if (((mask.words[core / 64] >> (core % 64)) & 1ULL) != 0ULL) {
            CPU_SET(static_cast<std::size_t>(core), &set);
        }
    }
}

// Minimal sched_attr ABI for SCHED_DEADLINE (glibc ships no wrapper).
struct sched_attr_abi {
    std::uint32_t size         = sizeof(sched_attr_abi);
    std::uint32_t sched_policy = 0;
    std::uint64_t sched_flags  = 0;
    std::int32_t  sched_nice   = 0;
    std::uint32_t sched_prio   = 0;
    std::uint64_t sched_runtime  = 0;
    std::uint64_t sched_deadline = 0;
    std::uint64_t sched_period   = 0;
};

[[nodiscard]] CRUCIBLE_INLINE int apply_deadline(std::uint64_t runtime_ns,
                                                 std::uint64_t deadline_ns,
                                                 std::uint64_t period_ns) noexcept {
    sched_attr_abi attr{};
    attr.sched_policy   = static_cast<std::uint32_t>(SCHED_DEADLINE);
    attr.sched_runtime  = runtime_ns;
    attr.sched_deadline = deadline_ns;
    attr.sched_period   = period_ns;
    return static_cast<int>(::syscall(SYS_sched_setattr, 0, &attr, 0u));
}

// Apply a scheduler policy to the calling thread, returning the syscall rc.
// Deadline routes through sched_setattr; all other policies through
// sched_setscheduler.  Holding the dispatch HERE keeps the mint body free
// of `if constexpr` so its §XXI compliance reads cleanly in the inventory.
template <SchedulerPolicy_v Policy>
[[nodiscard]] CRUCIBLE_INLINE int apply_scheduler_policy(int rt_priority,
                                                         std::uint64_t runtime_ns,
                                                         std::uint64_t deadline_ns,
                                                         std::uint64_t period_ns) noexcept {
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

// ═════════════════════════════════════════════════════════════════════
// ── grant tag family (crucible::fixy::grant::sched) ───────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant::sched {

namespace fsc = ::crucible::fixy::sched;

// (1) affinity — a sched_setaffinity CPU-pin (SyscallSurface).
struct affinity final : grant_base {};

// (2) scheduler_policy<Policy> — a sched_setscheduler/setattr class set.
template <fsc::SchedulerPolicy_v Policy>
struct scheduler_policy final : grant_base {};

// (3) priority<Nice> — a setpriority nice-value set.
template <int Nice>
struct priority final : grant_base {};

// (4) thread_name — a pthread_setname_np call (the V-189 surface).
struct thread_name final : grant_base {};

}  // namespace crucible::fixy::grant::sched

// ── which_dim routing — CR-09 locked namespace ───────────────────────

namespace crucible::fixy::grant {

namespace fsc = ::crucible::fixy::sched;

template <>
struct which_dim<sched::affinity>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <fsc::SchedulerPolicy_v Policy>
struct which_dim<sched::scheduler_policy<Policy>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <int Nice>
struct which_dim<sched::priority<Nice>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <>
struct which_dim<sched::thread_name>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

}  // namespace crucible::fixy::grant

// ═════════════════════════════════════════════════════════════════════
// ── The §XXI mint factories ───────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::sched {

// affinity: any ExecCtx + an actually-pinning posture.
template <typename Ctx, PinningPosture Posture>
concept CtxFitsAffinityMint = eff::IsExecCtx<Ctx> && (Posture != PinningPosture::NotPinned);

// scheduler policy: any ExecCtx + a recognized policy.
template <typename Ctx, SchedulerPolicy_v Policy>
concept CtxFitsSchedPolicyMint = eff::IsExecCtx<Ctx> && (detail::sched_policy_constant(Policy) >= 0);

// priority: any ExecCtx + an in-range nice value.
template <typename Ctx, int Nice>
concept CtxFitsPriorityMint = eff::IsExecCtx<Ctx> && (Nice >= -20 && Nice <= 19);

// ── mint_affinity — sched_setaffinity → CpuPinned proof ─────────────
//
// §XXI carve-out: cx=alloc — drops compile-time evaluation (performs a
// kernel side effect).  On success returns the move-only CpuPinned proof
// that fixy/Time.h's mint_tsc_reader requires.
template <AffinityMask Mask, PinningPosture Posture = PinningPosture::PinnedExplicit,
          eff::IsExecCtx Ctx>
    requires CtxFitsAffinityMint<Ctx, Posture>
[[nodiscard]] std::expected<sf::CpuPinned<Mask, Posture, ProofUnit>, int>
mint_affinity(Ctx const&) noexcept {
    cpu_set_t set;
    detail::fill_cpu_set(Mask, set);
    if (::sched_setaffinity(0, sizeof(set), &set) != 0) [[unlikely]] {
        return std::unexpected(errno);
    }
    return sf::mint_cpu_pinned<Mask, Posture, ProofUnit>(0);
}

// ── mint_scheduler_policy — sched_setscheduler/setattr → SchedClass ─
//
// §XXI carve-out: cx=alloc — drops compile-time evaluation (a scheduler
// syscall).  Non-Deadline policies route through sched_setscheduler;
// Deadline through sched_setattr with the (R, D, P) budget.  The
// SchedClass<Policy, int, R, D, P> return type enforces CBS admission.
template <SchedulerPolicy_v Policy, std::uint64_t RuntimeNs = 0,
          std::uint64_t DeadlineNs = 0, std::uint64_t PeriodNs = 0,
          eff::IsExecCtx Ctx>
    requires CtxFitsSchedPolicyMint<Ctx, Policy>
[[nodiscard]] std::expected<sf::SchedClass<Policy, ProofUnit, RuntimeNs, DeadlineNs, PeriodNs>, int>
mint_scheduler_policy(Ctx const&, int rt_priority = 0) noexcept {
    if (detail::apply_scheduler_policy<Policy>(rt_priority, RuntimeNs, DeadlineNs, PeriodNs) != 0)
        [[unlikely]] {
        return std::unexpected(errno);
    }
    return sf::mint_sched_class<Policy, ProofUnit, RuntimeNs, DeadlineNs, PeriodNs>(rt_priority);
}

// ── mint_priority — setpriority → SchedPriority witness ─────────────
//
// §XXI carve-out: cx=alloc — drops compile-time evaluation (a setpriority
// side effect).  PRIO_PROCESS with who == 0 sets the CALLING thread's nice
// value on Linux.
template <int Nice, eff::IsExecCtx Ctx>
    requires CtxFitsPriorityMint<Ctx, Nice>
[[nodiscard]] std::expected<SchedPriority<Nice>, int>
mint_priority(Ctx const&) noexcept {
    errno = 0;
    if (::setpriority(PRIO_PROCESS, 0, Nice) != 0 && errno != 0) [[unlikely]] {
        return std::unexpected(errno);
    }
    return SchedPriority<Nice>{};
}

// ── mint_thread_name — re-export of V-189's Init-row mint ───────────
using ::crucible::safety::mint_thread_name;

}  // namespace crucible::fixy::sched

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time + a guarded runtime smoke) ────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::sched::detail::v191_self_test {

namespace gs = ::crucible::fixy::grant::sched;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;

// ── grant tags: 1-byte EBO markers routed to SyscallSurface ─────────
static_assert(IsGrantTag<gs::affinity>);
static_assert(IsGrantTag<gs::scheduler_policy<SchedulerPolicy_v::Fifo>>);
static_assert(IsGrantTag<gs::priority<-10>>);
static_assert(IsGrantTag<gs::thread_name>);
static_assert(sizeof(gs::affinity)                                   == 1);
static_assert(sizeof(gs::scheduler_policy<SchedulerPolicy_v::Other>) == 1);
static_assert(sizeof(gs::priority<5>)                                == 1);
static_assert(which_dim_v<gs::affinity>                                   == D::SyscallSurface);
static_assert(which_dim_v<gs::scheduler_policy<SchedulerPolicy_v::Fifo>>  == D::SyscallSurface);
static_assert(which_dim_v<gs::priority<-10>>                              == D::SyscallSurface);
static_assert(which_dim_v<gs::thread_name>                                == D::SyscallSurface);

// ── policy → SCHED_* mapping is total over the lattice ──────────────
static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Other)    == SCHED_OTHER);
static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Fifo)     == SCHED_FIFO);
static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Deadline) == SCHED_DEADLINE);

// ── SchedPriority witness ───────────────────────────────────────────
static_assert(SchedPriority<-20>::nice == -20);
static_assert(SchedPriority<19>::nice  == 19);
static_assert(!std::is_same_v<SchedPriority<-10>, SchedPriority<10>>);

// ── mint return types are concrete expected<Proof, int> ─────────────
static_assert(std::is_same_v<
    decltype(mint_priority<5>(std::declval<eff::BgDrainCtx const&>())),
    std::expected<SchedPriority<5>, int>>);

// ── Runtime smoke: unprivileged-safe policy + nice + best-effort pin ─
inline bool runtime_smoke_test() {
    eff::BgDrainCtx bg{};

    // SCHED_OTHER + nice 5 are unprivileged-safe (you may always lower
    // your own priority).
    auto policy = mint_scheduler_policy<SchedulerPolicy_v::Other>(bg);
    if (!policy) return false;
    if (policy->policy != SchedulerPolicy_v::Other) return false;

    auto prio = mint_priority<5>(bg);
    if (!prio) return false;
    if (prio->nice != 5) return false;

    // Self-affinity to CPU 0: unprivileged but cpuset-dependent — exercise
    // and tolerate (a restricted cpuset legitimately returns EINVAL).
    auto pin = mint_affinity<AffinityMask::single(0)>(bg);
    if (pin && !pin->is_singleton_pin) return false;

    return true;
}

}  // namespace crucible::fixy::sched::detail::v191_self_test
