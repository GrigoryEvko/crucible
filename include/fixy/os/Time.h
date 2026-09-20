#pragma once

// Clock and counter readers, each minted against an execution context.
//
// Old spelling: include/crucible/fixy/Time.h.
//
// Three things the old header carried are gone.  The grant tags
// grant::time::{clock_read, tsc_read, sleep} and their which_dim rows
// are decoration: nothing outside their own self-test reads them.  Hw.h
// came in for TscMode alone, which is declared below instead.

#include <fixy/os/ClockSource.h>
#include <fixy/os/CpuPinned.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <ctime>
#include <type_traits>
#include <utility>

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

namespace fixy::time {

// The old tree read these names out of crucible::safety, which is fixy
// now.  Keeping the alias keeps every use site below spelled as it was.
namespace sf = ::fixy;
namespace eff = ::foundation::effects;
namespace ml = ::foundation::algebra::lattices;

using sf::ClockSource_v;
using sf::MonotonicClockBytes;
using sf::mint_clock_source;

// Moved in from include/crucible/fixy/Hw.h, which the port no longer
// includes.  Hw.h supplied this one enum and nothing else.
enum class TscMode : std::uint8_t {
    NotAllowed,
    SerializedPinned,  // rdtscp + lfence
    Raw,  // rdtsc, not serialized
    SteadyClockFallback,  // chrono::steady_clock — wall-clock nanoseconds, not cycles
};

// Returns -1 for the sources this reader cannot serve, for two disjoint
// reasons. The TSC and PMU sources are not clock_gettime-backed at all. The
// PTP hardware clock is clock_gettime-backed, but its clockid is derived
// per-fd as (~fd << 3) | 3 from an open /dev/ptpN descriptor, so no static
// constant exists for it.
[[nodiscard]] consteval ::clockid_t clockid_for(ClockSource_v source) noexcept {
    switch (source) {
        case ClockSource_v::Realtime:
            return CLOCK_REALTIME;
        case ClockSource_v::Monotonic:
            return CLOCK_MONOTONIC;
        case ClockSource_v::MonotonicRaw:
            return CLOCK_MONOTONIC_RAW;
        case ClockSource_v::Boot:
            return CLOCK_BOOTTIME;
        case ClockSource_v::ThreadCpu:
            return CLOCK_THREAD_CPUTIME_ID;
        case ClockSource_v::ProcessCpu:
            return CLOCK_PROCESS_CPUTIME_ID;
        case ClockSource_v::TscRaw:
        case ClockSource_v::TscSerialized:
        case ClockSource_v::PmuCounter:
            return -1;
        case ClockSource_v::PtpHwClock:
            return -1;
        default:
            return -1;
    }
}

template <ClockSource_v Source>
concept ClockBacked = (clockid_for(Source) >= 0);

namespace detail {

[[nodiscard]] CRUCIBLE_INLINE std::uint64_t read_raw_tsc() noexcept {
#if defined(__x86_64__)
    return __rdtsc();
#elif defined(__aarch64__)
    std::uint64_t value = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#else
#error "fixy/os/Time.h: TSC read is supported on x86_64 and aarch64 only."
#endif
}

[[nodiscard]] CRUCIBLE_INLINE std::uint64_t read_raw_tsc_serialized() noexcept {
#if defined(__x86_64__)
    unsigned aux = 0;
    const std::uint64_t value = __rdtscp(&aux);  // ordered against earlier instructions
    _mm_lfence();  // ordered against later instructions
    return value;
#elif defined(__aarch64__)
    asm volatile("isb" ::: "memory");  // the counter read must not float above earlier work
    std::uint64_t value = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#else
#error "fixy/os/Time.h: TSC read is supported on x86_64 and aarch64 only."
#endif
}

template <typename T>
inline constexpr bool is_cpu_pinned_v = false;
template <ml::AffinityMask Mask, sf::PinningPosture Posture, typename Unit>
inline constexpr bool is_cpu_pinned_v<sf::CpuPinned<Mask, Posture, Unit>> = true;

template <TscMode Mode>
[[nodiscard]] consteval ClockSource_v tsc_source() noexcept {
    return Mode == TscMode::SerializedPinned ? ClockSource_v::TscSerialized : ClockSource_v::TscRaw;
}

}  // namespace detail

// A multi-core mask still lets the thread migrate, and the counter is
// per-core, so only a single-core pin makes two reads comparable.
//
// What this concept proves is the SHAPE of the proof, not the pin.
// CpuPinned has three public doors that hand one out with no evidence
// of a sched_setaffinity call, so a reader that wants the pin itself
// must take one from fixy::sched::mint_affinity, which returns a proof
// only after the syscall succeeded.
template <typename T>
concept IsSingletonCpuPin = detail::is_cpu_pinned_v<std::remove_cvref_t<T>> && std::remove_cvref_t<T>::is_singleton_pin;

template <ClockSource_v Source>
    requires ClockBacked<Source>
struct ClockReader final {
    using result_type = sf::ClockSource<Source, std::uint64_t>;
    static constexpr ClockSource_v source = Source;

    [[nodiscard]] result_type read() const noexcept {
        std::timespec now{};
        (void)::clock_gettime(clockid_for(Source), &now);
        return result_type{static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL
                           + static_cast<std::uint64_t>(now.tv_nsec)};
    }
};

// The reader owns the pin proof for its whole lifetime, so the pin cannot be
// released while a read is still possible.
template <TscMode Mode, typename PinT>
    requires(Mode != TscMode::NotAllowed) && IsSingletonCpuPin<PinT>
struct TscReader final {
    using result_type = std::conditional_t<Mode == TscMode::SerializedPinned, sf::TscSerializedBytes<std::uint64_t>,
                                           sf::TscBytes<std::uint64_t>>;
    static constexpr TscMode mode = Mode;

    explicit constexpr TscReader(PinT&& pin) noexcept : pin_{std::move(pin)} {}

    TscReader(const TscReader&) = delete;
    TscReader& operator=(const TscReader&) = delete;
    TscReader(TscReader&&) noexcept = default;
    TscReader& operator=(TscReader&&) noexcept = default;
    ~TscReader() = default;

    [[nodiscard]] result_type read() const noexcept {
        if constexpr (Mode == TscMode::SerializedPinned) {
            return result_type{detail::read_raw_tsc_serialized()};
        } else {
            return result_type{detail::read_raw_tsc()};
        }
    }

    [[nodiscard]] PinT const& pin() const& noexcept { return pin_; }

private:
    PinT pin_;
};

template <std::uint64_t MaxNanos>
    requires(MaxNanos > 0)
struct BoundedSleeper final {
    static constexpr std::uint64_t max_nanos = MaxNanos;

    void sleep_for(std::uint64_t nanos) const noexcept {
        CRUCIBLE_PRE(nanos <= MaxNanos);
        std::timespec request{static_cast<std::time_t>(nanos / 1000000000ULL),
                              static_cast<long>(nanos % 1000000000ULL)};
        (void)::clock_nanosleep(CLOCK_MONOTONIC, 0, &request, nullptr);
    }
};

template <typename Ctx, ClockSource_v Source>
concept CtxFitsClockReaderMint = eff::IsExecCtx<Ctx> && ClockBacked<Source>;

template <typename Ctx, TscMode Mode, typename PinT>
concept CtxFitsTscReaderMint = eff::IsExecCtx<Ctx> && (Mode != TscMode::NotAllowed) && IsSingletonCpuPin<PinT>;

template <typename Ctx, std::uint64_t MaxNanos>
concept CtxFitsBoundedSleepMint = eff::CtxCanMint<Ctx, eff::Effect::Block> && (MaxNanos > 0);

template <ClockSource_v Source, eff::IsExecCtx Ctx>
    requires CtxFitsClockReaderMint<Ctx, Source>
[[nodiscard]] constexpr ClockReader<Source> mint_clock_reader(Ctx const&) noexcept {
    return {};
}

template <TscMode Mode, eff::IsExecCtx Ctx, typename PinT>
    requires CtxFitsTscReaderMint<Ctx, Mode, PinT>
[[nodiscard]] constexpr TscReader<Mode, std::remove_cvref_t<PinT>> mint_tsc_reader(Ctx const&, PinT&& pin) noexcept {
    return TscReader<Mode, std::remove_cvref_t<PinT>>{std::move(pin)};
}

template <std::uint64_t MaxNanos, eff::IsExecCtx Ctx>
    requires CtxFitsBoundedSleepMint<Ctx, MaxNanos>
[[nodiscard]] constexpr BoundedSleeper<MaxNanos> mint_bounded_sleep(Ctx const&) noexcept {
    return {};
}

}  // namespace fixy::time

namespace fixy::time::detail::v190_self_test {

// The nine grant-tag assertions the old self-test carried are not
// ported, because the tags they read are not ported.

static_assert(ClockBacked<ClockSource_v::Monotonic>);
static_assert(ClockBacked<ClockSource_v::Boot>);
static_assert(!ClockBacked<ClockSource_v::TscRaw>);
static_assert(!ClockBacked<ClockSource_v::PmuCounter>);

static_assert(std::is_same_v<ClockReader<ClockSource_v::Boot>::result_type, sf::BootClockBytes<std::uint64_t>>);
static_assert(
    std::is_same_v<ClockReader<ClockSource_v::Monotonic>::result_type, sf::MonotonicClockBytes<std::uint64_t>>);
static_assert(!std::is_same_v<ClockReader<ClockSource_v::Boot>, ClockReader<ClockSource_v::Monotonic>>);

using SinglePin = sf::CpuPinned<ml::AffinityMask::single(0), sf::PinningPosture::PinnedExplicit, int>;
using MultiPin = sf::CpuPinned<ml::AffinityMask::range(0, 1), sf::PinningPosture::PinnedExplicit, int>;
static_assert(IsSingletonCpuPin<SinglePin>);
static_assert(!IsSingletonCpuPin<MultiPin>);
static_assert(!IsSingletonCpuPin<int>);

static_assert(std::is_same_v<TscReader<TscMode::Raw, SinglePin>::result_type, sf::TscBytes<std::uint64_t>>);
static_assert(std::is_same_v<TscReader<TscMode::SerializedPinned, SinglePin>::result_type,
                             sf::TscSerializedBytes<std::uint64_t>>);

static_assert(BoundedSleeper<1000000>::max_nanos == 1000000ULL);

// The readers and the sleeper are exercised in test/fixy/test_os_time.cpp,
// which is also where the TSC leg lives: a TSC read needs a pin from
// fixy::sched::mint_affinity, and that lives in fixy/os/Sched.h, so the
// case belongs in a translation unit that includes both headers.  The
// old tree read the TSC through a pin its own smoke test minted for
// itself with no sched_setaffinity on the path, which is the forgery
// the proof exists to prevent.

}  // namespace fixy::time::detail::v190_self_test
