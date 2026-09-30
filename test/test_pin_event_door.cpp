// Each affinity change outside fixy/os goes through the recording door.
//
// A pin proof of fixy/os/CpuPinned.h holds one pin event, and is_in_force()
// compares it with the event in force on the thread.  Only the door
// fixy::detail::set_calling_thread_affinity records an event.  A change of
// affinity that goes around the door leaves an older proof in force while
// the thread runs under another mask.
//
// The three changes below were raw calls of sched_setaffinity: the pin of
// warden::Hardening::apply, the restore of warden::AppliedPolicy::revert and
// the helper pin of ledger::pin_this_thread_to.  Each case earns a proof,
// makes the change, and asks that the proof is no longer in force.
//
// Each proof pins the calling thread to CPU 0.  A cpuset without CPU 0
// refuses the pin, and the test then reports a skip.

#include <crucible/ledger/ProbeSupport.h>
#include <crucible/warden/Hardening.h>
#include <fixy/Ctx.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/Sched.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <utility>

namespace eff = foundation::effects;
namespace ml = foundation::algebra::lattices;

namespace {

inline constexpr ml::AffinityMask kCore0 = ml::AffinityMask::single(0);

// The policy pins the hot thread to CPU 0 and changes nothing else.
[[nodiscard]] constexpr crucible::warden::Policy pin_only_policy() noexcept {
    crucible::warden::Policy policy = crucible::warden::Policy::none();
    policy.hot_enabled = true;
    policy.hot_sched = crucible::warden::SchedClass::Other;
    policy.hot_core.explicit_cpu = 0;
    return policy;
}

int failures = 0;

void require(bool condition, char const* what) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    const fixy::BgLoadCtx background{eff::testing::bg()};
    const fixy::InitLoadCtx startup{eff::testing::init()};

    auto first = fixy::sched::mint_affinity<kCore0>(background);
    if (!first) {
        std::fprintf(stderr, "[skipped] no pin to CPU 0 available in this cpuset (errno %d)\n", first.error());
        return 0;
    }
    require(first->is_in_force(), "an earned pin is in force on its thread");

    // The pin of the hardening policy.
    auto applied = crucible::warden::mint_hardening(startup, pin_only_policy());
    require(applied.affinity_applied() && applied.pinned_cpu() == 0, "the policy pins the thread to CPU 0");
    require(!first->is_in_force(), "a pin proof stayed in force after Hardening::apply pinned the thread");

    // The restore of the hardening policy.
    auto second = fixy::sched::mint_affinity<kCore0>(background);
    require(second.has_value() && second->is_in_force(), "a second earned pin is in force");
    applied.revert();
    require(!applied.affinity_applied(), "revert gives the prior mask back");
    require(second.has_value() && !second->is_in_force(),
            "a pin proof stayed in force after AppliedPolicy::revert restored the mask");

    // The helper pin of the ledger probes.
    auto third = fixy::sched::mint_affinity<kCore0>(background);
    require(third.has_value() && third->is_in_force(), "a third earned pin is in force");
    require(crucible::ledger::pin_this_thread_to(background, 0), "the probe helper pins the thread to CPU 0");
    require(third.has_value() && !third->is_in_force(),
            "a pin proof stayed in force after ledger::pin_this_thread_to pinned the thread");

    // A restore on another thread changes nothing and says so.
    auto prior = fixy::sched::apply_affinity_to_cpu(background, 0);
    require(prior.has_value() && prior->is_held(), "the runtime pin holds the prior mask");
    if (prior.has_value()) {
        fixy::sched::PriorAffinity moved{std::move(*prior)};
        require(!prior->is_held() && moved.is_held(), "a move takes the prior mask from its source");
        int restore_errno = 0;
        {
            std::jthread other{[&moved, &restore_errno]() noexcept {
                const auto restored = std::move(moved).restore();
                restore_errno = restored.has_value() ? 0 : restored.error();
            }};
        }
        require(restore_errno == EPERM, "a restore on another thread is refused with EPERM");
    }

    if (failures != 0) return EXIT_FAILURE;
    std::printf("test_pin_event_door: each affinity change ends the older pin proof\n");
    return EXIT_SUCCESS;
}
