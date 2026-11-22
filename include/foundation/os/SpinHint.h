#pragma once

// The spin-hint door of the base: the instructions that a spin runs while it
// waits, with one arm for each architecture and feature, and the read of the
// counter that times a spin.
//
// Arms
//   An arm is one way to spend the time of a spin.  A plain arm spends one
//   turn and reads no word.  Each arm can also wait on a word: the wait ends
//   when the word holds a value other than the expected value, or when the
//   counter passes a deadline.  A plain arm does that wait as a loop of its
//   turn and a read of the word.  A word arm sleeps in the core until a store
//   to the line of the word, or until a timer.
//
//     pause    plain, x86_64.  PAUSE.  Each x86_64 core has it.
//     tpause   word, x86_64 with WAITPKG.  TPAUSE in the state C0.1 for a
//              slice of the counter, then a read of the word.
//     umwait   word, x86_64 with WAITPKG.  UMONITOR on the line of the word,
//              then UMWAIT in the state C0.1 to the deadline.
//     mwaitx   word, x86_64 with MONITORX (AMD).  MONITORX on the line of the
//              word, then MWAITX in C0 with its timer set to the deadline.
//     yield    plain, aarch64.  YIELD.  Each aarch64 core has it.
//     isb      plain, aarch64.  ISB SY, which waits for the pipeline to drain.
//     wfe      word, aarch64.  LDXR of the word, then WFE.  A store to the
//              line clears the exclusive monitor, and that gives an event.  The
//              event stream of the generic timer ends a wait with no store, so
//              a deadline has the grain of that stream.
//     wfet     word, aarch64 with FEAT_WFxT.  LDXR of the word, then WFET to
//              the deadline.
//
//   A word arm reads less of the line than a plain arm.  So a writer of the
//   line loses less time while a reader waits, and the core uses less power.
//
// Counter
//   read_spin_counter() reads the counter of the core with no serialization:
//   RDTSC on x86_64 and CNTVCT_EL0 on aarch64.  A deadline is a value of
//   this counter.  The spin budget of fixy/os/SpinBudget.h converts a time to
//   a deadline, and it gives the deadline to its caller only as an opaque
//   value.  This door is the raw layer below it, as raw_syscall is for the
//   system calls.
//
// Choice
//   The start-up hook of src/foundation/os/SpinHint.cpp reads the features of
//   the core one time: CPUID on x86_64, and AT_HWCAP2 of the auxiliary vector
//   on aarch64.  It keeps the arms of the core and the choice of the process:
//   a plain arm for a spin on a predicate, and an arm for a wait on a word.  A
//   measurement of the wake latency and of the cost for the writer chooses
//   the arm of each core.  Until such a measurement exists, the choice is the
//   arm that each core of the architecture has: pause on x86_64, and yield on
//   aarch64.
//
// Hot path
//   The record path keeps CRUCIBLE_SPIN_PAUSE of foundation/Platform.h, a
//   plain arm that the compiler inlines and that reads no record.
//
// This header is in the layer F0_doors of utils/rule-table.txt.  The door
// file is src/foundation/os/SpinHint.cpp.

#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>

#include <cstdint>

namespace foundation::os {

// The arms of the door (the arms).
enum class SpinArm : std::uint8_t {
    pause,
    tpause,
    umwait,
    mwaitx,
    yield,
    isb,
    wfe,
    wfet,
};

// True for an arm that spends one turn and reads no word.
[[nodiscard]] constexpr bool is_plain_arm(SpinArm arm) noexcept {
    return arm == SpinArm::pause || arm == SpinArm::yield || arm == SpinArm::isb;
}

// A set of arms, one bit for each enumerator of SpinArm.
class SpinArmSet {
public:
    constexpr SpinArmSet() noexcept = default;

    [[nodiscard]] constexpr bool has(SpinArm arm) const noexcept { return (bits_ & bit_of_(arm)) != 0; }

    [[nodiscard]] constexpr SpinArmSet with(SpinArm arm) const noexcept {
        SpinArmSet result{};
        result.bits_ = static_cast<std::uint8_t>(bits_ | bit_of_(arm));
        return result;
    }

    [[nodiscard]] constexpr bool operator==(SpinArmSet const& other) const noexcept = default;

private:
    [[nodiscard]] static constexpr std::uint8_t bit_of_(SpinArm arm) noexcept {
        return static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(arm));
    }

    std::uint8_t bits_ = 0;
};

// The bit of WAITPKG in the word ECX of CPUID leaf 7, subleaf 0.
inline constexpr std::uint32_t kCpuidLeaf7EcxWaitpkg = std::uint32_t{1} << 5;

// The bit of MONITORX in the word ECX of CPUID leaf 0x80000001.
inline constexpr std::uint32_t kCpuidExtendedEcxMonitorx = std::uint32_t{1} << 29;

// The bit of FEAT_WFxT in AT_HWCAP2 of aarch64: HWCAP2_WFXT of the aarch64
// <asm/hwcap.h>, bit 31.  An x86_64 build has no such header, so the door
// includes none.
inline constexpr std::uint64_t kHwcap2Wfxt = std::uint64_t{1} << 31;

// The arms of an x86_64 core with the word ECX of CPUID leaf 7 and the word
// ECX of CPUID leaf 0x80000001.  A caller gives zero for a leaf above the
// largest leaf of the core.
[[nodiscard]] constexpr SpinArmSet spin_arms_of_x86_64(std::uint32_t leaf7_ecx, std::uint32_t extended_ecx) noexcept {
    SpinArmSet arms = SpinArmSet{}.with(SpinArm::pause);
    if ((leaf7_ecx & kCpuidLeaf7EcxWaitpkg) != 0) {
        arms = arms.with(SpinArm::tpause).with(SpinArm::umwait);
    }
    if ((extended_ecx & kCpuidExtendedEcxMonitorx) != 0) {
        arms = arms.with(SpinArm::mwaitx);
    }
    return arms;
}

// The arms of an aarch64 core with the entry AT_HWCAP2 of the auxiliary
// vector.  A caller gives zero when the vector has no such entry.
[[nodiscard]] constexpr SpinArmSet spin_arms_of_aarch64(std::uint64_t hwcap2) noexcept {
    SpinArmSet arms = SpinArmSet{}.with(SpinArm::yield).with(SpinArm::isb).with(SpinArm::wfe);
    if ((hwcap2 & kHwcap2Wfxt) != 0) {
        arms = arms.with(SpinArm::wfet);
    }
    return arms;
}

// The choice of the process (the choice).  plain is a plain arm.
struct SpinChoice {
    SpinArm plain = SpinArm::pause;
    SpinArm word = SpinArm::pause;
};

// The arms of the core, as the start-up hook read them.
[[nodiscard]] SpinArmSet spin_arms_of_core() noexcept;

// The choice of the process, as the start-up hook made it.
[[nodiscard]] SpinChoice spin_choice_of_process() noexcept;

// The counter of the core (the counter).  The read is not serialized, so the
// core can run it before or after the work around it.  A spin reads it only
// to end a wait.
//
// The value is a source of nondeterminism, and the read takes no tag of
// Nondet.  It goes to three readers.  wait_for_change compares it with the
// deadline of its caller, and its SpinEnd then tells whether the word
// changed first.  The TimingSink of fixy/os/TimingSink.h writes it as a
// count in a body with no context, and only a context whose row holds Nondet
// reads a count of the sink.  The spin budget of fixy/os/SpinBudget.h reads
// it for a deadline and for each check of a deadline, and each of the two
// takes the tag of Nondet.
[[nodiscard]] CRUCIBLE_INLINE std::uint64_t read_spin_counter() noexcept {
#if defined(__x86_64__)
    return __builtin_ia32_rdtsc();
#elif defined(__aarch64__)
    std::uint64_t value = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#endif
}

// One turn of the plain arm ARM.  On x86_64 pause is the only plain arm.
CRUCIBLE_INLINE void spin_turn(SpinArm arm) noexcept {
#if defined(__x86_64__)
    static_cast<void>(arm);
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    if (arm == SpinArm::isb) {
        asm volatile("isb" ::: "memory");
    } else {
        asm volatile("yield" ::: "memory");
    }
#endif
}

// How a wait on a word ended.
enum class SpinEnd : std::uint8_t {
    // A read of the word, with acquire order, gave a value other than the
    // expected value.
    changed,
    // The counter passed the deadline, and the last read gave the expected
    // value.
    deadline_passed,
};

// Waits with the arm ARM until WORD holds a value other than EXPECTED, or
// until the counter is at DEADLINE or past it.  The word is read with
// acquire order before each step, so the first read of a changed value ends
// the wait.  A deadline that passed ends the wait after one read.  ARM is an
// arm of the core.  Complexity: one read of the word for each step of the
// arm, and the steps take the time to the deadline at most.
[[nodiscard]] SpinEnd wait_for_change(core::Atomic<std::uint32_t> const& word, std::uint32_t expected,
                                      std::uint64_t deadline, SpinArm arm) noexcept;

}  // namespace foundation::os
