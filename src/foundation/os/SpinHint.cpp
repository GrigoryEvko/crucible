// The door of foundation/os/SpinHint.h: the read of the features of the core
// at start-up, the choice of the process, and the steps of each arm.
//
// On x86_64 the file reads CPUID through foundation/os/Cpuid.h.  On aarch64 it
// reads AT_HWCAP2 through the auxiliary vector door.  Each step of an x86_64
// word arm calls a builtin of GCC in a function with the target attribute of
// its feature.  So the file includes no intrinsics header, and no other unit
// compiles with that feature.  The start-up hook selects an arm only from the
// features, so a step runs only on a core that has its instruction.

#include <foundation/os/SpinHint.h>

#include <foundation/core/Atomic.h>
#include <foundation/os/Auxiliary.h>
#include <foundation/os/Cpuid.h>

#include <cstdint>
#include <type_traits>

namespace foundation::os {

namespace {

// A monitor watches the line of the 32-bit word at the address of the cell.
// The cell of an Atomic has one member, the word, and a class of the standard
// layout puts its first member at its own address.
static_assert(std::is_standard_layout_v<core::Atomic<std::uint32_t>>,
              "the word of a wait must have the standard layout, so the cell is at the address of its word");
static_assert(sizeof(core::Atomic<std::uint32_t>) == sizeof(std::uint32_t),
              "the word of a wait must have the size of the 32-bit word that the monitor watches");

// The arms that each core of the architecture has.
[[nodiscard]] constexpr SpinArmSet base_arms() noexcept {
#if defined(__x86_64__)
    return spin_arms_of_x86_64(0, 0);
#elif defined(__aarch64__)
    return spin_arms_of_aarch64(0);
#endif
}

// The choice of each core of the architecture (the choice of the header).
[[nodiscard]] constexpr SpinChoice base_choice() noexcept {
#if defined(__x86_64__)
    return SpinChoice{.plain = SpinArm::pause, .word = SpinArm::pause};
#elif defined(__aarch64__)
    return SpinChoice{.plain = SpinArm::yield, .word = SpinArm::yield};
#endif
}

// The record of the start-up hook.  One copy in each library is correct: the
// hook of a copy writes it one time, at the load of its library, and only the
// door of that library reads it.  Each read comes after the write, because the
// thread of the read starts after main starts, or it reaches the library
// through the dlopen that ran the write.  Before the hook, the record holds
// the arms of each core of the architecture.
struct SpinRecord {
    SpinArmSet arms = base_arms();
    SpinChoice choice = base_choice();
};

constinit SpinRecord spin_record_at_load{};

// The arms of the core of the calling thread.
[[nodiscard]] SpinArmSet read_arms_of_core() noexcept {
#if defined(__x86_64__)
    constexpr std::uint32_t kLeafOfFeatures = 7;
    constexpr std::uint32_t kExtendedBase = 0x80000000U;
    constexpr std::uint32_t kExtendedFeatures = 0x80000001U;
    std::uint32_t const largest = read_cpuid(0, 0).eax;
    std::uint32_t const leaf7_ecx = largest >= kLeafOfFeatures ? read_cpuid(kLeafOfFeatures, 0).ecx : 0;
    std::uint32_t const largest_extended = read_cpuid(kExtendedBase, 0).eax;
    std::uint32_t const extended_ecx = largest_extended >= kExtendedFeatures ? read_cpuid(kExtendedFeatures, 0).ecx : 0;
    return spin_arms_of_x86_64(leaf7_ecx, extended_ecx);
#elif defined(__aarch64__)
    return spin_arms_of_aarch64(read_auxiliary(AuxiliaryEntry::hardware_caps2).value_or(0));
#endif
}

// The read of the features.  The priority 104 comes after the start-up check
// of the kernel floor (101), and before each constructor with no priority.
[[gnu::constructor(104)]] void read_spin_arms_at_start() noexcept { spin_record_at_load.arms = read_arms_of_core(); }

[[nodiscard]] bool holds(core::Atomic<std::uint32_t> const& word, std::uint32_t expected) noexcept {
    return word.load_acquire() == expected;
}

[[nodiscard]] void const* address_of(core::Atomic<std::uint32_t> const& word CRUCIBLE_LIFETIMEBOUND) noexcept {
    return static_cast<void const*>(&word);
}
void address_of(core::Atomic<std::uint32_t> const&&) =
    delete("the address names the word, and a temporary word ends with the full expression");

#if defined(__x86_64__)

// The bit 0 of the control word of TPAUSE and UMWAIT selects the state C0.1,
// which wakes faster than C0.2.
constexpr std::uint32_t kStateC01 = 1;

// The hint of MWAITX that keeps the core in C0, and the bit of ECX that
// enables its timer (MWAITX_DISABLE_CSTATES and MWAITX_ECX_TIMER_ENABLE of
// the kernel).  The timer counts the clocks of P0, the rate of the counter.
constexpr std::uint32_t kMwaitxHintC0 = 0xF0;
constexpr std::uint32_t kMwaitxTimerEnable = 2;
constexpr std::uint64_t kMwaitxLargestTimer = 0xFFFFFFFFU;

// TPAUSE does not watch the line, so one step sleeps for this slice at most,
// and the wait then reads the word again.
constexpr std::uint64_t kTpauseSliceTicks = 1024;

// The monitor comes before the last read of the word.  A store after the
// read then ends the wait of MWAITX, and a store before it ends the step at
// the read.
[[gnu::target("mwaitx")]] void step_mwaitx(core::Atomic<std::uint32_t> const& word, std::uint32_t expected,
                                           std::uint64_t now, std::uint64_t deadline) noexcept {
    __builtin_ia32_monitorx(address_of(word), 0, 0);
    if (!holds(word, expected)) return;
    std::uint64_t const remaining = deadline - now;
    auto const clocks = static_cast<std::uint32_t>(remaining < kMwaitxLargestTimer ? remaining : kMwaitxLargestTimer);
    __builtin_ia32_mwaitx(kMwaitxTimerEnable, kMwaitxHintC0, clocks);
}

// UMWAIT ends at a store to the line, at the deadline, or at the limit that
// the kernel sets in IA32_UMWAIT_CONTROL.  The wait then reads the word
// again, so the limit of the kernel only adds a step.
[[gnu::target("waitpkg")]] void step_umwait(core::Atomic<std::uint32_t> const& word, std::uint32_t expected,
                                            std::uint64_t deadline) noexcept {
    asm volatile("umonitor %0" : : "r"(address_of(word)) : "memory");
    if (!holds(word, expected)) return;
    static_cast<void>(__builtin_ia32_umwait(kStateC01, deadline));
}

[[gnu::target("waitpkg")]] void step_tpause(std::uint64_t now, std::uint64_t deadline) noexcept {
    std::uint64_t const end = deadline - now > kTpauseSliceTicks ? now + kTpauseSliceTicks : deadline;
    static_cast<void>(__builtin_ia32_tpause(kStateC01, end));
}

#elif defined(__aarch64__)

// The loop of __cmpwait of the arm64 kernel.  SEVL and the first WFE clear
// the event register.  LDXR arms the exclusive monitor on the line.  A word
// that changed ends the step at once.  Otherwise the second WFE sleeps until
// an event: a store to the line clears the monitor and gives one, and the
// event stream of the timer gives one too.
void step_wfe(core::Atomic<std::uint32_t> const& word, std::uint32_t expected) noexcept {
    std::uint32_t observed = 0;
    asm volatile("sevl\n"
                 "wfe\n"
                 "ldxr %w[observed], [%[address]]\n"
                 "eor %w[observed], %w[observed], %w[expected]\n"
                 "cbnz %w[observed], 1f\n"
                 "wfe\n"
                 "1:\n"
                 : [observed] "=&r"(observed)
                 : [address] "r"(address_of(word)), [expected] "r"(expected)
                 : "memory");
}

// The same loop with WFET in place of the second WFE.  WFET also ends when
// the counter gets to the deadline.  The register s0_3_c1_c0_0 is the
// encoding of WFET that the arm64 kernel writes, so the assembler needs no
// flag of FEAT_WFxT.
void step_wfet(core::Atomic<std::uint32_t> const& word, std::uint32_t expected, std::uint64_t deadline) noexcept {
    std::uint32_t observed = 0;
    asm volatile("sevl\n"
                 "wfe\n"
                 "ldxr %w[observed], [%[address]]\n"
                 "eor %w[observed], %w[observed], %w[expected]\n"
                 "cbnz %w[observed], 1f\n"
                 "msr s0_3_c1_c0_0, %[deadline]\n"
                 "1:\n"
                 : [observed] "=&r"(observed)
                 : [address] "r"(address_of(word)), [expected] "r"(expected), [deadline] "r"(deadline)
                 : "memory");
}

#endif

// One step of the arm ARM.  A plain arm spends one turn.
void step(SpinArm arm, core::Atomic<std::uint32_t> const& word, std::uint32_t expected, std::uint64_t now,
          std::uint64_t deadline) noexcept {
    switch (arm) {
#if defined(__x86_64__)
        case SpinArm::tpause:
            step_tpause(now, deadline);
            return;
        case SpinArm::umwait:
            step_umwait(word, expected, deadline);
            return;
        case SpinArm::mwaitx:
            step_mwaitx(word, expected, now, deadline);
            return;
#elif defined(__aarch64__)
        case SpinArm::wfe:
            step_wfe(word, expected);
            return;
        case SpinArm::wfet:
            step_wfet(word, expected, deadline);
            return;
#endif
        default:
            static_cast<void>(now);
            static_cast<void>(deadline);
            spin_turn(arm);
            return;
    }
}

}  // namespace

SpinArmSet spin_arms_of_core() noexcept { return spin_record_at_load.arms; }

SpinChoice spin_choice_of_process() noexcept { return spin_record_at_load.choice; }

SpinEnd wait_for_change(core::Atomic<std::uint32_t> const& word, std::uint32_t expected, std::uint64_t deadline,
                        SpinArm arm) noexcept {
    // The check reads the record of the hook, a load that an assumption
    // cannot hold, so it is a fatal invariant.  It also keeps the contract
    // handler, and the unwind tables that it needs, out of the door.
    CRUCIBLE_FATAL_INVARIANT(spin_record_at_load.arms.has(arm));
    for (;;) {
        if (!holds(word, expected)) return SpinEnd::changed;
        std::uint64_t const now = read_spin_counter();
        if (now >= deadline) return SpinEnd::deadline_passed;
        step(arm, word, expected, now, deadline);
    }
}

}  // namespace foundation::os
