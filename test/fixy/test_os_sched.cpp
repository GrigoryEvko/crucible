// The scheduling family exercised at run time: the pin proof, the
// scheduling-class band, the thread name, and the four mints that reach
// the kernel.
//
// These cases used to be `runtime_smoke_test` functions inside
// fixy/os/CpuPinned.h, SchedClass.h, ThreadName.h and Sched.h,
// compiled into every translation unit that included any of them.  Each
// builds a scenario — construct a value, mutate it, mint over it, call
// a syscall — rather than stating a property of a type as shipped, so
// each belongs here.  What stayed in the headers is the other kind: the
// posture and singleton answers on a pin, the policy subsumption
// lattice, that two clock sources or two scheduling classes are
// distinct types.  Those fire wherever the type is used.
//
// The arguments below are non-constant on purpose.  A suite made only
// of static_asserts masks the bugs that appear when a body is
// instantiated for runtime evaluation rather than folded.

#include <fixy/os/CpuPinned.h>
#include <fixy/os/Sched.h>
#include <fixy/os/SchedClass.h>
#include <fixy/os/ThreadName.h>

#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace ml = foundation::algebra::lattices;

namespace {

inline constexpr ml::AffinityMask kCore0 = ml::AffinityMask::single(0);
inline constexpr ml::AffinityMask kCore7 = ml::AffinityMask::single(7);
inline constexpr ml::AffinityMask kTwoBit = ml::AffinityMask::range(0, 1);

using PinnedC0 = fixy::CpuPinned<kCore0, fixy::PinningPosture::PinnedExplicit, int>;
using AutoC0 = fixy::CpuPinned<kCore0, fixy::PinningPosture::PinnedAuto, int>;
using TwoBitC = fixy::CpuPinned<kTwoBit, fixy::PinningPosture::PinnedExplicit, int>;

using FifoInt = fixy::SchedClass<fixy::SchedulerPolicy_v::Fifo, int>;
using DeadlineInt = fixy::SchedClass<fixy::SchedulerPolicy_v::Deadline, int, 5000, 10000, 20000>;

// The named contexts these mints are meant to take belong to a header
// the tree does not have yet.  This one stands in, in the shape
// foundation's own context self-test uses.  It is handed the capability
// it claims: a context is not evidence of a capability, it carries one.
using BgWitness = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

[[nodiscard]] int pin_proof_values_round_trip() {
    int seed = 21;

    PinnedC0 pin{seed * 2};
    if (pin.peek() != 42) {
        std::fprintf(stderr, "a pin proof did not carry its value\n");
        return 1;
    }
    pin.peek_mut() = 9;
    if (pin.peek() != 9) {
        std::fprintf(stderr, "peek_mut did not write through\n");
        return 1;
    }

    auto minted = fixy::mint_cpu_pinned<kCore7, fixy::PinningPosture::PinnedExplicit, unsigned long long>(
        static_cast<unsigned long long>(seed));
    if (std::move(minted).consume() != 21) {
        std::fprintf(stderr, "consume did not move the value out\n");
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

    // An auto pin's value moves into an explicit one; the postures are
    // distinct types, so this is a move of the carried value, not a
    // reinterpretation of the proof.
    AutoC0 auto_pin{1};
    PinnedC0 moved{std::move(auto_pin).consume()};
    if (moved.peek() != 1) {
        std::fprintf(stderr, "the value did not survive the move between postures\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int sched_class_values_round_trip() {
    int seed = 21;

    FifoInt fifo{seed * 2};
    if (fifo.peek() != 42) {
        std::fprintf(stderr, "a scheduling class did not carry its value\n");
        return 1;
    }
    fifo.peek_mut() = 9;
    if (fifo.peek() != 9) {
        std::fprintf(stderr, "peek_mut did not write through\n");
        return 1;
    }

    auto minted = fixy::mint_sched_class<fixy::SchedulerPolicy_v::Other, int>(seed);
    if (std::move(minted).consume() != 21) {
        std::fprintf(stderr, "consume did not move the value out\n");
        return 1;
    }

    FifoInt first{1};
    FifoInt second{2};
    swap(first, second);
    if (first.peek() != 2 || second.peek() != 1) {
        std::fprintf(stderr, "swap did not exchange two same-policy tasks\n");
        return 1;
    }

    // The pool-hosting answers at run time.  A FIFO task runs on a
    // DEADLINE pool and must not run on an OTHER one.
    const bool on_deadline = FifoInt::runnable_on<fixy::SchedulerPolicy_v::Deadline>;
    const bool on_other = FifoInt::runnable_on<fixy::SchedulerPolicy_v::Other>;
    if (!on_deadline || on_other) {
        std::fprintf(stderr, "the policy subsumption answered wrongly at run time\n");
        return 1;
    }

    DeadlineInt deadline{seed};
    if (deadline.peek() != 21 || deadline.runtime_ns != 5000) {
        std::fprintf(stderr, "a deadline task lost its value or its budget\n");
        return 1;
    }

    fixy::sched_class::Idle<int> idle{0};
    fixy::sched_class::RoundRobin<int> round_robin{456};
    if (idle.peek() != 0 || round_robin.peek() != 456) {
        std::fprintf(stderr, "a named policy alias did not carry its value\n");
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
    if (policy->policy != fixy::SchedulerPolicy_v::Other) {
        std::fprintf(stderr, "the minted class named a policy the call did not set\n");
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

    // -1 means "every CPU in the cpuset", which always succeeds.
    if (!fixy::sched::apply_affinity_to_cpu(bg, -1)) {
        std::fprintf(stderr, "restoring the full affinity mask failed\n");
        return 1;
    }
    (void)fixy::sched::apply_affinity_to_cpu(bg, 0);
    return 0;
}

}  // namespace

int main() {
    if (const int rc = pin_proof_values_round_trip(); rc != 0) return rc;
    if (const int rc = sched_class_values_round_trip(); rc != 0) return rc;
    if (const int rc = thread_name_reaches_the_kernel(); rc != 0) return rc;
    if (const int rc = scheduler_mints_reach_the_kernel(); rc != 0) return rc;
    return 0;
}
