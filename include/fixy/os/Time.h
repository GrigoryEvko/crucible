#pragma once

// Clock and counter readers, each minted against an execution context.
//
// Old spelling: include/crucible/fixy/Time.h.
//
// Three things the old header carried are gone.  The grant tags
// grant::time::{clock_read, tsc_read, sleep} and their which_dim rows
// are decoration: nothing outside their own self-test reads them.  Hw.h
// came in for TscMode alone, which is declared below instead.
//
// Two things the old tree carried elsewhere, in safety/_Mutation.h's
// MonotonicClock, are here now: the gate that keeps a clock read off the
// replay-bound foreground path, and the clamp that keeps two reads
// through one reader from regressing.  Neither came with the first port
// of this header, because its source never had them, and the wrapper
// port that dropped the class did not say so.

#include <fixy/Mutation.h>
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

// The old tree read these names out of its safety namespace, which is
// fixy now.  Keeping the alias keeps every use site below spelled as it
// was.
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
// This concept reads the mask off the type, and the type is now enough.
// CpuPinned has one constructor, it is private, and its sole friend is
// fixy::sched::mint_affinity, which hands a proof back only after
// sched_setaffinity returned 0 for that same mask.  So a PinT that
// satisfies this concept was pinned, and the gate stands on the pin
// rather than on the shape of a token anyone could build.
//
// It did not, until the doors were closed.  Three public constructors
// and a free mint built one out of nothing, and a forged single-core
// pin passed this concept on a thread that could migrate — which makes
// two TSC reads two different counters, with no crash and no diagnostic
// to say so.  test/fixy/neg/neg_os_cpu_pinned_*.cpp is the standing
// witness on each closed route.
template <typename T>
concept IsSingletonCpuPin = detail::is_cpu_pinned_v<std::remove_cvref_t<T>> && std::remove_cvref_t<T>::is_singleton_pin;

// Whether the kernel promises the clock never steps backward.  Realtime
// can be set, and the two CPU-time clocks are per-thread and per-process
// accumulators that one reader may be asked to read from more than one
// thread, so a clamp on either would invent an ordering that does not
// exist.  The three non-decreasing clocks are clamped.
[[nodiscard]] consteval bool is_non_decreasing(ClockSource_v source) noexcept {
    switch (source) {
        case ClockSource_v::Monotonic:
        case ClockSource_v::MonotonicRaw:
        case ClockSource_v::Boot:
            return true;
        case ClockSource_v::Realtime:
        case ClockSource_v::ThreadCpu:
        case ClockSource_v::ProcessCpu:
        case ClockSource_v::TscRaw:
        case ClockSource_v::TscSerialized:
        case ClockSource_v::PmuCounter:
        case ClockSource_v::PtpHwClock:
            return false;
        default:
            return false;
    }
}

// The clamp safety/_Mutation.h's MonotonicClock carried, verbatim in
// effect: if the underlying clock goes backward, the previously observed
// value is returned instead, so two reads through one reader never
// regress.  Detecting the clock fault is the host monitoring layer's
// job, not this one's.  It is a free function over the caller's floor so
// that a test can hand it a regressing value, which no real clock will.
[[nodiscard]] inline std::uint64_t clamp_non_decreasing(sf::AtomicMonotonic<std::uint64_t>& last,
                                                        std::uint64_t raw) noexcept {
    (void)last.try_advance(raw);
    const std::uint64_t observed = last.get();
    return observed > raw ? observed : raw;
}

// Reading a clock on the replay-bound foreground path makes replay
// diverge across machines, so a reader is minted only by a context that
// owns Bg, Init or Test.  The name is the one safety/_Mutation.h gave
// this gate; it holds for every clock-backed source, because the replay
// argument does not depend on which clock diverges.
template <typename Ctx>
concept CtxFitsMonotonicClock =
    eff::CtxOwnsAnyOf<Ctx, eff::Effect::Bg, eff::Effect::Init, eff::Effect::Test>;

template <typename Ctx, ClockSource_v Source>
concept CtxFitsClockReaderMint = CtxFitsMonotonicClock<Ctx> && ClockBacked<Source>;

template <ClockSource_v Source>
    requires ClockBacked<Source>
struct ClockReader final {
    using result_type = sf::ClockSource<Source, std::uint64_t>;
    static constexpr ClockSource_v source = Source;
    static constexpr bool is_clamped = is_non_decreasing(Source);

    [[nodiscard]] result_type read() const noexcept {
        std::timespec now{};
        (void)::clock_gettime(clockid_for(Source), &now);
        const std::uint64_t raw = static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL
                                  + static_cast<std::uint64_t>(now.tv_nsec);
        if constexpr (is_clamped) {
            return result_type{clamp_non_decreasing(last_, raw)};
        } else {
            return result_type{raw};
        }
    }

private:
    // One door.  The mint's requires-clause is the whole gate, and the
    // mint is the only friend, so a reader cannot be built anywhere the
    // evidence of being off the replay path was not checked.
    constexpr ClockReader() noexcept : last_{make_clamp_state()} {}

    template <ClockSource_v S, eff::IsExecCtx Ctx>
        requires CtxFitsClockReaderMint<Ctx, S>
    friend constexpr ClockReader<S> mint_clock_reader(Ctx const&) noexcept;

    // The floor of every value this reader has returned.  Advancing it is
    // the read's own bookkeeping, which is why it is mutable behind a
    // const read; AtomicMonotonic is pinned, so a clamped reader is pinned
    // with it and has one address for the lifetime of its floor.  An
    // unclamped reader carries nothing and stays a value.
    struct NoClamp final {};
    using clamp_state = std::conditional_t<is_clamped, sf::AtomicMonotonic<std::uint64_t>, NoClamp>;

    static constexpr clamp_state make_clamp_state() noexcept {
        if constexpr (is_clamped) {
            return sf::mint_atomic_monotonic<std::uint64_t>(0);
        } else {
            return NoClamp{};
        }
    }

    mutable clamp_state last_;
};

// The name safety/_Mutation.h gave the clamped, gated monotonic reader.
// It names the same thing here.
using MonotonicClock = ClockReader<ClockSource_v::Monotonic>;

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

template <typename Ctx, TscMode Mode, typename PinT>
concept CtxFitsTscReaderMint = eff::IsExecCtx<Ctx> && (Mode != TscMode::NotAllowed) && IsSingletonCpuPin<PinT>;

template <typename Ctx, std::uint64_t MaxNanos>
concept CtxFitsBoundedSleepMint = eff::CtxCanMint<Ctx, eff::Effect::Block> && (MaxNanos > 0);

template <ClockSource_v Source, eff::IsExecCtx Ctx>
    requires CtxFitsClockReaderMint<Ctx, Source>
[[nodiscard]] constexpr ClockReader<Source> mint_clock_reader(Ctx const&) noexcept {
    return ClockReader<Source>{};
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

namespace fixy::time::detail::clock_reader_invariants {

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

// The gate and the clamp, as properties of the types.  A foreground
// context is refused at the mint and a background one admitted; the
// three non-decreasing sources are clamped and Realtime is not; the
// reader's one door is the mint, so it is neither default- nor
// copy-constructible from outside, and a clamped reader has one address
// because its floor does.  The behaviour under a sequence of reads, and
// the clamp fed a regressing value, are in test/fixy/test_os_time.cpp;
// the two refusals are test/fixy/neg/neg_os_clock_reader_*.cpp.
using ForegroundCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;
using BackgroundCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;
static_assert(!CtxFitsMonotonicClock<ForegroundCtx>);
static_assert(CtxFitsMonotonicClock<BackgroundCtx>);
static_assert(!CtxFitsClockReaderMint<ForegroundCtx, ClockSource_v::Monotonic>);
static_assert(CtxFitsClockReaderMint<BackgroundCtx, ClockSource_v::Monotonic>);
static_assert(!CtxFitsClockReaderMint<BackgroundCtx, ClockSource_v::TscRaw>);

static_assert(std::is_same_v<MonotonicClock, ClockReader<ClockSource_v::Monotonic>>);
static_assert(MonotonicClock::is_clamped);
static_assert(ClockReader<ClockSource_v::MonotonicRaw>::is_clamped);
static_assert(ClockReader<ClockSource_v::Boot>::is_clamped);
static_assert(!ClockReader<ClockSource_v::Realtime>::is_clamped);

static_assert(!std::is_default_constructible_v<MonotonicClock>);
static_assert(!std::is_copy_constructible_v<MonotonicClock>);
static_assert(!std::is_move_constructible_v<MonotonicClock>);
static_assert(!std::is_default_constructible_v<ClockReader<ClockSource_v::Realtime>>);
static_assert(std::is_copy_constructible_v<ClockReader<ClockSource_v::Realtime>>);

// The readers and the sleeper are exercised in test/fixy/test_os_time.cpp,
// which is also where the TSC leg lives: a TSC read needs a pin from
// fixy::sched::mint_affinity, and that lives in fixy/os/Sched.h, so the
// case belongs in a translation unit that includes both headers.  The
// old tree read the TSC through a pin its own smoke test minted for
// itself with no sched_setaffinity on the path, which is the forgery
// the proof exists to prevent.

}  // namespace fixy::time::detail::clock_reader_invariants
