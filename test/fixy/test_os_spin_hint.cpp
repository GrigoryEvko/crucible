// The spin-hint door of foundation/os/SpinHint.h: the arms of a core from
// its feature words, the arms and the choice of the process, and a wait on a
// word with each arm of the core.  The wait ends at a change of the word, and
// at a deadline of the counter.
//
// A wait that loses a change ends at its deadline, about 2^34 ticks of the
// counter, and the test then fails.  So a lost change fails the test before
// the timeout of the test preset.

#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/SpinHint.h>

#include <cstdint>

namespace {

namespace eff = ::foundation::effects;
namespace core = ::foundation::core;
namespace os = ::foundation::os;
using os::SpinArm;
using os::SpinArmSet;
using os::SpinEnd;

using BlockCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

// The deadline of a wait that a change must end.  At a counter of 1 GHz to
// 5 GHz it is 3 s to 17 s.
constexpr std::uint64_t kLongWaitTicks = std::uint64_t{1} << 34;

// The words of CPUID and of AT_HWCAP2 give the arms of the core.
void test_the_features_give_the_arms() {
    constexpr SpinArmSet plain_x86 = os::spin_arms_of_x86_64(0, 0);
    static_assert(plain_x86 == SpinArmSet{}.with(SpinArm::pause));
    constexpr SpinArmSet waitpkg = os::spin_arms_of_x86_64(os::kCpuidLeaf7EcxWaitpkg, 0);
    static_assert(waitpkg == plain_x86.with(SpinArm::tpause).with(SpinArm::umwait));
    constexpr SpinArmSet monitorx = os::spin_arms_of_x86_64(0, os::kCpuidExtendedEcxMonitorx);
    static_assert(monitorx == plain_x86.with(SpinArm::mwaitx));
    static_assert(
        !os::spin_arms_of_x86_64(~os::kCpuidLeaf7EcxWaitpkg, ~os::kCpuidExtendedEcxMonitorx).has(SpinArm::mwaitx));

    constexpr SpinArmSet plain_aarch64 = os::spin_arms_of_aarch64(0);
    static_assert(plain_aarch64 == SpinArmSet{}.with(SpinArm::yield).with(SpinArm::isb).with(SpinArm::wfe));
    static_assert(os::spin_arms_of_aarch64(os::kHwcap2Wfxt) == plain_aarch64.with(SpinArm::wfet));
    static_assert(!os::spin_arms_of_aarch64(~os::kHwcap2Wfxt).has(SpinArm::wfet));

    static_assert(os::is_plain_arm(SpinArm::pause) && os::is_plain_arm(SpinArm::yield)
                  && os::is_plain_arm(SpinArm::isb));
    static_assert(!os::is_plain_arm(SpinArm::tpause) && !os::is_plain_arm(SpinArm::umwait)
                  && !os::is_plain_arm(SpinArm::mwaitx) && !os::is_plain_arm(SpinArm::wfe)
                  && !os::is_plain_arm(SpinArm::wfet));
}

// The core has the arms that each core of its architecture has, and the
// choice of the process names arms of the core.
void test_the_process_chooses_arms_of_the_core() {
    SpinArmSet const arms = os::spin_arms_of_core();
#if defined(__x86_64__)
    CRUCIBLE_FATAL_INVARIANT(arms.has(SpinArm::pause));
    CRUCIBLE_FATAL_INVARIANT(!arms.has(SpinArm::yield) && !arms.has(SpinArm::wfe));
#elif defined(__aarch64__)
    CRUCIBLE_FATAL_INVARIANT(arms.has(SpinArm::yield) && arms.has(SpinArm::isb) && arms.has(SpinArm::wfe));
    CRUCIBLE_FATAL_INVARIANT(!arms.has(SpinArm::pause) && !arms.has(SpinArm::mwaitx));
#endif
    os::SpinChoice const choice = os::spin_choice_of_process();
    CRUCIBLE_FATAL_INVARIANT(arms.has(choice.plain) && os::is_plain_arm(choice.plain));
    CRUCIBLE_FATAL_INVARIANT(arms.has(choice.word));
}

// A word that does not hold the expected value ends the wait at once, also
// with a deadline that passed.  A word that holds it and a deadline that
// passed end the wait after one read.
void test_a_wait_ends_at_once(SpinArm arm) {
    core::Atomic<std::uint32_t> const word{7};
    CRUCIBLE_FATAL_INVARIANT(os::wait_for_change(word, 3, 0, arm) == SpinEnd::changed);
    CRUCIBLE_FATAL_INVARIANT(os::wait_for_change(word, 3, os::read_spin_counter() + kLongWaitTicks, arm)
                             == SpinEnd::changed);
    CRUCIBLE_FATAL_INVARIANT(os::wait_for_change(word, 7, 0, arm) == SpinEnd::deadline_passed);
}

// A word that holds the expected value ends the wait at a deadline that is
// some ticks after now.
void test_a_deadline_ends_the_wait(SpinArm arm) {
    core::Atomic<std::uint32_t> const word{7};
    constexpr std::uint64_t kShortWaitTicks = 100000;
    std::uint64_t const deadline = os::read_spin_counter() + kShortWaitTicks;
    CRUCIBLE_FATAL_INVARIANT(os::wait_for_change(word, 7, deadline, arm) == SpinEnd::deadline_passed);
    CRUCIBLE_FATAL_INVARIANT(os::read_spin_counter() >= deadline);
}

// A store of a different thread ends the wait, and the waiter reads the value
// of the store.  The writer stores after it sees that the waiter is about to
// wait, so most stores come during the wait.
void test_a_store_ends_the_wait(SpinArm arm) {
    BlockCtx const ctx{eff::testing::bg()};
    constexpr std::uint32_t kRounds = 200;
    core::Atomic<std::uint32_t> word{0};
    core::Atomic<std::uint32_t> waiting_round{0};
    core::Atomic<std::uint32_t> lost_rounds{0};
    auto waiter = [&word, &waiting_round, &lost_rounds, arm]() noexcept {
        for (std::uint32_t round = 0; round < kRounds; ++round) {
            waiting_round.store_release(round + 1);
            SpinEnd const end = os::wait_for_change(word, round, os::read_spin_counter() + kLongWaitTicks, arm);
            if (end != SpinEnd::changed || word.load_acquire() != round + 1) {
                static_cast<void>(lost_rounds.fetch_add_acq_rel(1));
            }
        }
    };
    auto writer = [&]() noexcept {
        for (std::uint32_t round = 0; round < kRounds; ++round) {
            while (waiting_round.load_acquire() != round + 1) {
                CRUCIBLE_SPIN_PAUSE;
            }
            word.store_release(round + 1);
        }
    };
    CRUCIBLE_FATAL_INVARIANT(::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, waiter, writer).is_ok());
    CRUCIBLE_FATAL_INVARIANT(lost_rounds.load_acquire() == 0);
}

// The tests of one arm, when the core has it.  The core of a run has only
// some of the arms, so each run tests the arms of its own core.
void test_the_arm_when_the_core_has_it(SpinArm arm) {
    if (!os::spin_arms_of_core().has(arm)) return;
    test_a_wait_ends_at_once(arm);
    test_a_deadline_ends_the_wait(arm);
    test_a_store_ends_the_wait(arm);
}

}  // namespace

int main() {
    test_the_features_give_the_arms();
    test_the_process_chooses_arms_of_the_core();
    test_the_arm_when_the_core_has_it(SpinArm::pause);
    test_the_arm_when_the_core_has_it(SpinArm::tpause);
    test_the_arm_when_the_core_has_it(SpinArm::umwait);
    test_the_arm_when_the_core_has_it(SpinArm::mwaitx);
    test_the_arm_when_the_core_has_it(SpinArm::yield);
    test_the_arm_when_the_core_has_it(SpinArm::isb);
    test_the_arm_when_the_core_has_it(SpinArm::wfe);
    test_the_arm_when_the_core_has_it(SpinArm::wfet);
    return 0;
}
