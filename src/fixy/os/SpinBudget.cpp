// The door of fixy/os/SpinBudget.h: the start-up hook that computes the rate
// of the counter of the process (the rate of the header).
//
// On aarch64 the hook reads CNTFRQ_EL0.  On x86_64 it reads CPUID through
// foundation/os/Cpuid.h, and it takes the rate of leaf 0x15 only on an Intel
// core with no hypervisor.  Each other core takes the calibration against
// CLOCK_MONOTONIC through the clock door of fixy/os/MonotonicClock.h.
//
// The calibration reads the clock with no tag of Nondet, because a start-up
// hook has no context.  The reading leaves the hook only as the rate, and the
// rate leaves the header only as the end of a spin, which takes the tag (THE
// GATE of the header).  The file includes no system header.

#include <fixy/os/SpinBudget.h>

#include <fixy/os/ClockSource.h>
#include <fixy/os/Time.h>
#include <foundation/Platform.h>
#include <foundation/os/Cpuid.h>
#include <foundation/os/SpinHint.h>

#include <cstdint>

namespace fixy::time {

namespace {

constexpr std::uint64_t kNanosecondsPerSecond = 1000000000U;

// The time between the two samples of a calibration.
constexpr std::uint64_t kCalibrationWindowNanos = 100000;

// The reads of one sample.  The sample keeps the narrowest bracket.
constexpr std::uint32_t kBracketsPerSample = 8;

// The record of the start-up hook.  One copy in each library is correct: the
// hook of a copy writes it one time, at the load of its library, and only the
// deadlines of that library read it.  Each read comes after the write,
// because the thread of the read starts after main starts, or it reaches the
// library through the dlopen that ran the write.  Before the hook, the rate
// is zero (the rate of the header).
constinit TickRate tick_rate_at_load{};

// One read of CLOCK_MONOTONIC in nanoseconds, or zero for a read that
// failed.  A boot of the host is more than zero nanoseconds before the read,
// so zero is no reading of the clock.
[[nodiscard]] std::uint64_t read_monotonic_nanos() noexcept {
    return detail::read_clock_nanos(clockid_for(ClockSource_v::Monotonic)).value_or(0);
}

// One read of CLOCK_MONOTONIC with the counter at the middle of the read.
struct ClockSample {
    std::uint64_t nanoseconds = 0;
    std::uint64_t ticks = 0;
};

// The sample of the narrowest of kBracketsPerSample brackets.  A bracket is
// the counter before and after one read of the clock.  A preemption or an
// interrupt in a read widens its bracket, and the narrowest bracket holds
// the least of such a delay.  A sample with no good read has zero
// nanoseconds.
[[nodiscard]] ClockSample read_clock_sample() noexcept {
    ClockSample best{};
    std::uint64_t best_width = ~std::uint64_t{0};
    for (std::uint32_t bracket = 0; bracket < kBracketsPerSample; ++bracket) {
        std::uint64_t const before = ::foundation::os::read_spin_counter();
        std::uint64_t const nanoseconds = read_monotonic_nanos();
        std::uint64_t const after = ::foundation::os::read_spin_counter();
        if (nanoseconds == 0 || after < before) continue;
        std::uint64_t const width = after - before;
        if (width < best_width) {
            best_width = width;
            best = ClockSample{.nanoseconds = nanoseconds, .ticks = before + width / 2};
        }
    }
    return best;
}

// The rate from two samples kCalibrationWindowNanos apart, or the rate of
// zero when the clock gives no good sample.
[[nodiscard]] TickRate calibrate_against_the_clock() noexcept {
    ClockSample const first = read_clock_sample();
    if (first.nanoseconds == 0) return TickRate{};
    for (;;) {
        std::uint64_t const now = read_monotonic_nanos();
        if (now == 0 || now - first.nanoseconds >= kCalibrationWindowNanos) break;
        CRUCIBLE_SPIN_PAUSE;
    }
    ClockSample const last = read_clock_sample();
    if (last.nanoseconds <= first.nanoseconds || last.ticks <= first.ticks) return TickRate{};
    return tick_rate_of(CounterTicks{last.ticks - first.ticks}, SpanNanos{last.nanoseconds - first.nanoseconds});
}

#if defined(__x86_64__)

// The rate of leaf 0x15 of an Intel core with no hypervisor: the crystal
// clock in ECX times the ratio EBX / EAX.  The rate of zero when the core
// gives no such rate.
[[nodiscard]] TickRate read_rate_of_leaf_15() noexcept {
    constexpr std::uint32_t kLeafOfTheCounter = 0x15;
    constexpr std::uint32_t kLeafOfModel = 1;
    constexpr std::uint32_t kHypervisorBit = std::uint32_t{1} << 31;
    // "GenuineIntel" in the words EBX, EDX and ECX of leaf 0.
    constexpr std::uint32_t kIntelEbx = 0x756E6547U;
    constexpr std::uint32_t kIntelEdx = 0x49656E69U;
    constexpr std::uint32_t kIntelEcx = 0x6C65746EU;
    ::foundation::os::CpuidWords const vendor = ::foundation::os::read_cpuid(0, 0);
    bool const is_intel = vendor.ebx == kIntelEbx && vendor.edx == kIntelEdx && vendor.ecx == kIntelEcx;
    bool const is_guest = (::foundation::os::read_cpuid(kLeafOfModel, 0).ecx & kHypervisorBit) != 0;
    if (!is_intel || is_guest || vendor.eax < kLeafOfTheCounter) return TickRate{};
    ::foundation::os::CpuidWords const counter = ::foundation::os::read_cpuid(kLeafOfTheCounter, 0);
    if (counter.eax == 0 || counter.ebx == 0 || counter.ecx == 0) return TickRate{};
    return tick_rate_of(CounterTicks{std::uint64_t{counter.ecx} * counter.ebx},
                        SpanNanos{std::uint64_t{counter.eax} * kNanosecondsPerSecond});
}

#endif

// The rate of the counter of the core (the rate of the header).
[[nodiscard]] TickRate read_rate_of_counter() noexcept {
#if defined(__aarch64__)
    std::uint64_t hertz = 0;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(hertz));
    if (hertz != 0) return tick_rate_of(CounterTicks{hertz}, SpanNanos{kNanosecondsPerSecond});
#elif defined(__x86_64__)
    TickRate const of_leaf = read_rate_of_leaf_15();
    if (of_leaf.multiplier != 0) return of_leaf;
#endif
    return calibrate_against_the_clock();
}

// The hook of the rate.  The priority 104 comes after the start-up check of
// the kernel floor (101), and before each constructor with no priority.
[[gnu::constructor(104)]] void compute_tick_rate_at_start() noexcept { tick_rate_at_load = read_rate_of_counter(); }

}  // namespace

namespace detail {

TickRate read_tick_rate_() noexcept { return tick_rate_at_load; }

}  // namespace detail

}  // namespace fixy::time
