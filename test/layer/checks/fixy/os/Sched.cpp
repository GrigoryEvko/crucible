// The compile-time checks of fixy/os/Sched.h.

#include <fixy/os/Sched.h>

namespace fixy::sched::detail::scheduler_mint_invariants {

using BgWitness = ::fixy::BgDrainCtx;
using InitWitness = ::fixy::ColdInitCtx;
using FgWitness = ::fixy::HotFgCtx;

static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Other) == ::foundation::os::kSchedNormal);
static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Fifo) == ::foundation::os::kSchedFifo);
static_assert(detail::sched_policy_constant(SchedulerPolicy_v::Deadline) == ::foundation::os::kSchedDeadline);

// The kept conjunct, both arms.  Deleting it would turn the second pin
// into a runtime EINVAL.
static_assert(CtxFitsSchedPolicyMint<BgWitness, SchedulerPolicy_v::Fifo>);
static_assert(!CtxFitsSchedPolicyMint<BgWitness, static_cast<SchedulerPolicy_v>(200)>,
              "a policy outside the enum has no SCHED_* constant, and this concept is the only thing that "
              "rejects it before the syscall.");

static_assert(SchedPriority<-20>::nice == -20);
static_assert(SchedPriority<19>::nice == 19);
static_assert(!std::is_same_v<SchedPriority<-10>, SchedPriority<10>>);

// The priority proof comes only from its mint, and it stays with the
// thread whose syscall it names.  fixy/os/SchedClass.h asserts the same
// of the scheduling-class proof.
static_assert(!std::is_default_constructible_v<SchedPriority<5>> && !std::is_copy_constructible_v<SchedPriority<5>>
                  && !std::is_move_constructible_v<SchedPriority<5>>,
              "a priority proof comes only from mint_priority and never leaves the frame that holds it");
static_assert(!std::is_implicit_lifetime_v<SchedPriority<5>> && !std::is_aggregate_v<SchedPriority<5>>,
              "std::start_lifetime_as and aggregate initialization must not build a priority proof");
static_assert(!std::is_trivially_copyable_v<SchedPriority<5>>, "std::bit_cast must not build a priority proof");
static_assert(!std::is_default_constructible_v<SchedProofDoor> && !std::is_copy_constructible_v<SchedProofDoor>
                  && !std::is_move_constructible_v<SchedProofDoor>,
              "No object of the scheduling proof door exists.  Only its members build the proof key.");

// A frame gives the result of its body, or Unit for a body that gives
// nothing, with the error of the kernel.
static_assert(std::is_same_v<decltype(mint_priority<5>(std::declval<BgWitness const&>(), [](auto) noexcept {})),
                             ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>>);
static_assert(std::is_same_v<decltype(mint_scheduler_policy<SchedulerPolicy_v::Other>(
                                 std::declval<BgWitness const&>(), [](auto proof) noexcept { return proof->peek(); })),
                             ::foundation::core::Result<int, ::foundation::os::Errno>>);

// The real-time priority of a policy, both arms.  A priority that the policy
// does not take is a compile error, and not the EINVAL of the kernel.
static_assert(RealTimePriorityFits<SchedulerPolicy_v::Fifo, 1> && RealTimePriorityFits<SchedulerPolicy_v::Fifo, 99>);
static_assert(!RealTimePriorityFits<SchedulerPolicy_v::Fifo, 0> && !RealTimePriorityFits<SchedulerPolicy_v::Fifo, 100>,
              "SCHED_FIFO takes a priority from 1 to 99");
static_assert(RealTimePriorityFits<SchedulerPolicy_v::Other, 0> && !RealTimePriorityFits<SchedulerPolicy_v::Other, 1>,
              "SCHED_OTHER takes the priority zero");
static_assert(RealTimePriorityFits<SchedulerPolicy_v::Deadline, 0>
                  && !RealTimePriorityFits<SchedulerPolicy_v::Deadline, 5>,
              "SCHED_DEADLINE takes the priority zero");

// The kept nice bound, both arms.  Deleting it makes an out-of-range
// nice a hard error at the call site rather than a rejected candidate.
static_assert(CtxFitsPriorityMint<BgWitness, 5>);
static_assert(!CtxFitsPriorityMint<BgWitness, 50>,
              "a nice outside [-20, 19] must be rejected by this concept.  Leaving it to SchedPriority's own "
              "requires-clause in the return type does not work: a constraint failure on a class template is "
              "not in the immediate context of the function template, so GCC 16 raises a hard error instead "
              "of discarding the candidate.");

// The shared scheduling gate, on each of the three mints.  IsExecCtx
// alone admits the foreground context, and the shared gate refuses it.
static_assert(CtxFitsSchedPolicyMint<InitWitness, SchedulerPolicy_v::Other>);
static_assert(!CtxFitsSchedPolicyMint<FgWitness, SchedulerPolicy_v::Other>,
              "the foreground hot path owns neither Bg nor Init, so it must not change a scheduler policy.");
static_assert(CtxFitsPriorityMint<InitWitness, 5>);
static_assert(!CtxFitsPriorityMint<FgWitness, 5>,
              "the foreground hot path owns neither Bg nor Init, so it must not change a nice value.");
static_assert(::fixy::CtxFitsAffinityMint<BgWitness, PinningPosture::PinnedExplicit>);
static_assert(!::fixy::CtxFitsAffinityMint<FgWitness, PinningPosture::PinnedExplicit>);

// A prior mask comes only from apply_affinity_to_mask, so no public
// constructor builds one, and no byte copy builds one either.
static_assert(!std::is_default_constructible_v<PriorAffinity> && !std::is_copy_constructible_v<PriorAffinity>
                  && std::is_nothrow_move_constructible_v<PriorAffinity>,
              "a prior affinity mask comes only from apply_affinity_to_mask, and one mask is restored at most once");
static_assert(!std::is_trivially_copyable_v<PriorAffinity> && !std::is_implicit_lifetime_v<PriorAffinity>,
              "std::bit_cast and std::start_lifetime_as must not build a prior affinity mask");
// The two runtime doors and the restore give the error of the kernel in the
// one error form of the base.
static_assert(std::is_same_v<decltype(apply_affinity_to_mask(std::declval<BgWitness const&>(), AffinityMask{})),
                             ::foundation::core::Result<PriorAffinity, ::foundation::os::Errno>>);
static_assert(std::is_same_v<decltype(apply_affinity_to_cpu(std::declval<BgWitness const&>(), 0)),
                             ::foundation::core::Result<PriorAffinity, ::foundation::os::Errno>>);
static_assert(std::is_same_v<decltype(std::declval<PriorAffinity>().restore()),
                             ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>>);

static_assert(CtxFitsRuntimeAffinity<BgWitness>);
static_assert(CtxFitsRuntimeAffinity<InitWitness>);
static_assert(!CtxFitsRuntimeAffinity<FgWitness>, "the Fg hot path owns no Bg or Init effect — it must not be "
                                                  "able to re-pin a thread.");

}  // namespace fixy::sched::detail::scheduler_mint_invariants
