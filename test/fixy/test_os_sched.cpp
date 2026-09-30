// The scheduling family exercised at run time: the pin proof, the
// scheduling-class band, the thread name, and the four mints that reach
// the kernel.
//
// Each case builds a scenario — construct a value, mutate it, mint over
// it, call a syscall — rather than stating a property of a type as
// shipped, so each lives here and not in fixy/os/CpuPinned.h,
// SchedClass.h, ThreadName.h or Sched.h.  The headers hold the other
// kind: the posture and singleton answers on a pin, the policy
// subsumption lattice, that two clock sources or two scheduling classes
// are distinct types.  Those fire wherever the type is used.
//
// The pin leg below performs a real sched_setaffinity, and it has to.
// CpuPinned has one constructor, it is private, and mint_affinity is its
// sole friend, so there is no way to reach the accessors without pinning
// the thread.  That is the point of the type, and a test that got around
// it would be testing a shape the shipped code does not have.
//
// The arguments below are non-constant on purpose.  A suite made only
// of static_asserts masks the bugs that appear when a body is
// instantiated for runtime evaluation rather than folded.

#include <fixy/Ctx.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/Sched.h>
#include <fixy/os/SchedClass.h>
#include <fixy/os/ThreadName.h>

#include <sched.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace ml = foundation::algebra::lattices;

namespace {

inline constexpr ml::AffinityMask kCore0 = ml::AffinityMask::single(0);
inline constexpr ml::AffinityMask kTwoBit = ml::AffinityMask::range(0, 1);

// TwoBitC is named for its singleton answer alone and is never built.
// Nothing can build it: mint_affinity is the only constructor, and this
// file never asks it for a two-core mask.
using PinnedC0 = fixy::CpuPinned<kCore0, fixy::PinningPosture::PinnedExplicit, int>;
using TwoBitC = fixy::CpuPinned<kTwoBit, fixy::PinningPosture::PinnedExplicit, int>;

using FifoInt = fixy::SchedClass<fixy::SchedulerPolicy_v::Fifo, int>;
using DeadlineInt = fixy::SchedClass<fixy::SchedulerPolicy_v::Deadline, int, 5000, 10000, 20000>;

// Each context is handed the capability it claims: a context is not
// evidence of a capability, it carries one.
using BgWitness = fixy::BgDrainCtx;

// The runtime pin is not a mint, and it has its own requires-clause.
// fixy/os/Sched.h proves the concept of that clause.  This proves that
// the function asks the concept: the foreground context and a value that
// is not a context cannot call it.
template <class Ctx>
concept can_apply_affinity = requires(Ctx const& ctx) { fixy::sched::apply_affinity_to_cpu(ctx, 0); };
static_assert(can_apply_affinity<BgWitness>);
static_assert(can_apply_affinity<fixy::ColdInitCtx>);
static_assert(!can_apply_affinity<fixy::HotFgCtx>);
static_assert(!can_apply_affinity<int>);

template <class Ctx>
concept can_apply_mask = requires(Ctx const& ctx) { fixy::sched::apply_affinity_to_mask(ctx, kCore0); };
static_assert(can_apply_mask<BgWitness>);
static_assert(can_apply_mask<fixy::ColdInitCtx>);
static_assert(!can_apply_mask<fixy::HotFgCtx>);
static_assert(!can_apply_mask<int>);

// The highest CPU in the set, or -1 for an empty set.
[[nodiscard]] int highest_cpu_in(::cpu_set_t const& set) noexcept {
    for (int cpu = CPU_SETSIZE - 1; cpu >= 0; --cpu) {
        if (CPU_ISSET(static_cast<std::size_t>(cpu), &set)) return cpu;
    }
    return -1;
}

// The mask door names each CPU that a cpu_set_t can name.  The pin goes to
// the highest CPU that this thread may use, and the kernel then reports
// the mask and the CPU of the thread.  A 256-bit mask cannot name a CPU
// past 255, so on a host with more CPUs this leg is the witness of the
// width.  It runs first, because the later legs pin the thread to CPU 0.
[[nodiscard]] int mask_door_pins_to_the_highest_allowed_cpu() {
    BgWitness bg{eff::testing::bg()};

    ::cpu_set_t before;
    CPU_ZERO(&before);
    if (::sched_getaffinity(0, sizeof(before), &before) != 0) {
        std::fprintf(stderr, "reading the affinity of the thread failed (errno %d)\n", errno);
        return 1;
    }

    // An empty mask asks for no pin.  The call succeeds, holds no mask and
    // changes nothing.
    auto no_pin = fixy::sched::apply_affinity_to_mask(bg, ml::AffinityMask{});
    if (!no_pin || no_pin->is_held()) {
        std::fprintf(stderr, "an empty mask did not give a result that holds no mask\n");
        return 1;
    }

    // An index past the kernel set is refused before the system call.
    auto past_the_set = fixy::sched::apply_affinity_to_cpu(bg, CPU_SETSIZE);
    if (past_the_set || past_the_set.error() != EINVAL) {
        std::fprintf(stderr, "a pin to a CPU past the kernel set was not refused with EINVAL\n");
        return 1;
    }

    const int highest = highest_cpu_in(before);
    if (highest <= 255) {
        std::fprintf(stderr,
                     "[skipped] the highest CPU this thread may use is %d, and a 256-bit mask names it too, so "
                     "the width leg shows nothing on this host\n",
                     highest);
        return 0;
    }

    auto prior = fixy::sched::apply_affinity_to_mask(bg, ml::AffinityMask::single(static_cast<std::uint16_t>(highest)));
    if (!prior || !prior->is_held()) {
        std::fprintf(stderr, "the mask door did not pin the thread to CPU %d (errno %d)\n", highest,
                     prior ? 0 : prior.error());
        return 1;
    }
    ::cpu_set_t pinned;
    CPU_ZERO(&pinned);
    if (::sched_getaffinity(0, sizeof(pinned), &pinned) != 0 || CPU_COUNT(&pinned) != 1
        || !CPU_ISSET(static_cast<std::size_t>(highest), &pinned) || ::sched_getcpu() != highest) {
        std::fprintf(stderr, "the kernel does not report the pin to CPU %d\n", highest);
        return 1;
    }

    // The restore gives the thread its whole set back.
    if (!std::move(*prior).restore()) {
        std::fprintf(stderr, "the restore of the prior mask failed\n");
        return 1;
    }
    ::cpu_set_t after;
    CPU_ZERO(&after);
    if (::sched_getaffinity(0, sizeof(after), &after) != 0 || !CPU_EQUAL(&after, &before)) {
        std::fprintf(stderr, "the restore did not give the thread its prior mask\n");
        return 1;
    }

    // The one-CPU door gets to the same CPU through the mask door.
    auto again = fixy::sched::apply_affinity_to_cpu(bg, highest);
    if (!again || !again->is_held() || ::sched_getcpu() != highest) {
        std::fprintf(stderr, "the one-CPU door did not pin the thread to CPU %d\n", highest);
        return 1;
    }
    if (!std::move(*again).restore()) {
        std::fprintf(stderr, "the restore after the one-CPU pin failed\n");
        return 1;
    }
    return 0;
}

// Every pin here is earned.  CpuPinned has one constructor, it is
// private, and fixy::sched::mint_affinity is its sole friend, so the
// only way into this leg is a sched_setaffinity that returned 0.  The
// leg therefore really pins the calling thread, and the later legs of
// this process run pinned.  No leg depends on the mask.
[[nodiscard]] int pin_proof_round_trips_through_an_earned_pin() {
    BgWitness bg{eff::testing::bg()};

    auto pin = fixy::sched::mint_affinity<kCore0>(bg);
    if (!pin) {
        // A restricted cpuset returns EINVAL and CPU 0 may be outside
        // it.  That is the environment, not a defect.
        std::fprintf(stderr, "[skipped] no pin to CPU 0 available in this cpuset (errno %d)\n", pin.error());
        return 0;
    }

    // The proof's payload is a unit: mint_affinity seeds it with zero
    // and the authority lives in the type, not the value.
    if (pin->peek() != 0) {
        std::fprintf(stderr, "an earned pin did not carry its unit payload\n");
        return 1;
    }
    pin->peek_mut() = 9;
    if (pin->peek() != 9) {
        std::fprintf(stderr, "peek_mut did not write through\n");
        return 1;
    }

    // The pin is in force on the thread that earned it.
    if (!pin->is_in_force()) {
        std::fprintf(stderr, "an earned pin was not in force on the thread that earned it\n");
        return 1;
    }

    // Moving transfers the claim rather than copying it: the moved-to
    // proof is in force and the source is not.  The copy operations are
    // deleted, so this cannot be anything else.
    PinnedC0 moved{std::move(*pin)};
    if (!moved.is_in_force() || pin->is_in_force()) {
        std::fprintf(stderr, "a move did not take the pin event from its source\n");
        return 1;
    }
    if (std::move(moved).consume() != 9) {
        std::fprintf(stderr, "consume did not move the value out of an earned pin\n");
        return 1;
    }

    // The posture is a parameter of the mint, and asking for the weaker
    // one under-claims rather than over-claims.
    auto auto_pin = fixy::sched::mint_affinity<kCore0, fixy::PinningPosture::PinnedAuto>(bg);
    if (!auto_pin) {
        std::fprintf(stderr, "an auto-posture pin failed where the explicit one succeeded (errno %d)\n",
                     auto_pin.error());
        return 1;
    }
    if (decltype(auto_pin)::value_type::posture != fixy::PinningPosture::PinnedAuto) {
        std::fprintf(stderr, "the minted pin named a posture the caller did not ask for\n");
        return 1;
    }

    // The singleton answer, read at run time rather than folded.  A
    // two-core mask is not a singleton, and a TSC read across two cores
    // is unsound.
    const bool single_is_singleton = PinnedC0::is_singleton_pin;
    const bool two_is_singleton = TwoBitC::is_singleton_pin;
    if (!single_is_singleton || two_is_singleton) {
        std::fprintf(stderr, "the singleton-pin answer was wrong at run time\n");
        return 1;
    }

    return 0;
}

// A scheduling class is a proof that only mint_scheduler_policy builds,
// so the pool rule and the budget are read off the types here, and the
// proof itself is exercised in the case after the next one.
[[nodiscard]] int sched_class_answers_read_at_run_time() {
    // The pool-hosting answers at run time.  A FIFO task runs on a
    // DEADLINE pool and must not run on an OTHER one.
    const bool on_deadline = FifoInt::runnable_on<fixy::SchedulerPolicy_v::Deadline>;
    const bool on_other = FifoInt::runnable_on<fixy::SchedulerPolicy_v::Other>;
    if (!on_deadline || on_other) {
        std::fprintf(stderr, "the policy subsumption answered wrongly at run time\n");
        return 1;
    }

    const std::uint64_t runtime_ns = DeadlineInt::runtime_ns;
    const std::uint64_t period_ns = DeadlineInt::period_ns;
    if (runtime_ns != 5000 || period_ns != 20000) {
        std::fprintf(stderr, "a deadline class lost its budget\n");
        return 1;
    }

    const bool idle_on_idle = fixy::sched_class::Idle<int>::runnable_on<fixy::SchedulerPolicy_v::Idle>;
    const bool round_robin_on_other = fixy::sched_class::RoundRobin<int>::runnable_on<fixy::SchedulerPolicy_v::Other>;
    if (!idle_on_idle || round_robin_on_other) {
        std::fprintf(stderr, "a named policy alias answered the pool rule wrongly\n");
        return 1;
    }
    return 0;
}

// Running this renames the calling thread, which is why it is the last
// thing the process does before the scheduler cases.
[[nodiscard]] int thread_name_reaches_the_kernel() {
    auto init = eff::testing::init();
    auto witness = fixy::mint_thread_name<"crux-smoke">(init);

    // Both facts live in the witness type rather than in the value, so
    // they hold by construction and are asserted where they cost
    // nothing.
    static_assert(std::string_view{decltype(witness)::c_str()} == "crux-smoke");
    static_assert(decltype(witness)::visible_length() == 10);
    static_assert(fixy::IsThreadNamed<decltype(witness)>);
    return 0;
}

[[nodiscard]] int scheduler_mints_reach_the_kernel() {
    BgWitness bg{eff::testing::bg()};

    // SCHED_OTHER and a nice value of 5 need no privilege: a thread may
    // always lower its own priority.
    auto policy = fixy::sched::mint_scheduler_policy<fixy::SchedulerPolicy_v::Other>(bg);
    if (!policy) {
        std::fprintf(stderr, "setting SCHED_OTHER failed (errno %d)\n", policy.error());
        return 1;
    }
    if (policy->policy != fixy::SchedulerPolicy_v::Other || policy->peek() != 0) {
        std::fprintf(stderr, "the minted class named a policy or a priority the call did not set\n");
        return 1;
    }

    auto priority = fixy::sched::mint_priority<5>(bg);
    if (!priority) {
        std::fprintf(stderr, "lowering nice to 5 failed (errno %d)\n", priority.error());
        return 1;
    }
    if (priority->nice != 5) {
        std::fprintf(stderr, "the minted priority named a nice the call did not set\n");
        return 1;
    }

    // A self-pin to CPU 0 needs no privilege but depends on the cpuset.
    // A restricted one returns EINVAL, so success is not asserted —
    // only that a pin handed back really is the singleton it claims.
    auto pin = fixy::sched::mint_affinity<kCore0>(bg);
    if (pin && !pin->is_singleton_pin) {
        std::fprintf(stderr, "a single-core pin did not report itself a singleton\n");
        return 1;
    }

    // A negative index asks for no pin: the call changes nothing, and it
    // succeeds.  A pin through the runtime door records a new pin event,
    // so the proof above stops being in force.
    if (!fixy::sched::apply_affinity_to_cpu(bg, -1)) {
        std::fprintf(stderr, "a runtime affinity call that asks for no pin failed\n");
        return 1;
    }
    if (pin && fixy::sched::apply_affinity_to_cpu(bg, 0) && pin->is_in_force()) {
        std::fprintf(stderr, "a pin proof stayed in force after its thread pinned again\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = mask_door_pins_to_the_highest_allowed_cpu(); rc != 0) return rc;
    if (const int rc = pin_proof_round_trips_through_an_earned_pin(); rc != 0) return rc;
    if (const int rc = sched_class_answers_read_at_run_time(); rc != 0) return rc;
    if (const int rc = thread_name_reaches_the_kernel(); rc != 0) return rc;
    if (const int rc = scheduler_mints_reach_the_kernel(); rc != 0) return rc;
    return 0;
}
