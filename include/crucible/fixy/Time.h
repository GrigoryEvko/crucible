#pragma once

// ── crucible::fixy::time — the time-reading + bounded-sleep surface ──
//
// FIXY-V-190 (Agent 6 §3.4).  The consumer-facing time vocabulary that
// composes the V-184/185 ClockSource axis and the V-187 CpuPinned proof
// into three §XXI mints, plus the grant tags a fixy::fn signature uses to
// DECLARE a time effect.
//
// ── Two complementary surfaces ──────────────────────────────────────
//
//   grant::time::clock_read<Source>  → SyscallSurface   (clock_gettime via vDSO)
//   grant::time::tsc_read<Mode>      → HwInstruction     (rdtsc / rdtscp)
//   grant::time::sleep<MaxNanos>     → SyscallSurface     (clock_nanosleep, Block)
//
//   mint_clock_reader<Source>(ctx)            → ClockReader<Source>
//   mint_tsc_reader<Mode>(ctx, CpuPinned&&)   → TscReader<Mode, PinT>
//   mint_bounded_sleep<MaxNanos>(ctx)         → BoundedSleeper<MaxNanos>
//
// The grant tags are the fn-signature vocabulary; the mints return the
// runtime READER / SLEEPER objects.  (fixy/Hw.h's mint_tsc_grant mints the
// GRANT tag; this mints the reader that actually executes the read.)
//
// ── The three bug classes eliminated ────────────────────────────────
//
//   (1) CLOCK-SOURCE MISLABELING.  A ClockReader<Boot> returns
//       BootClockBytes; a ClockReader<Monotonic> returns MonotonicClockBytes
//       — distinct types, so a suspend-blind monotonic read cannot be
//       passed where a KeepsTicking BootClockBytes is required (V-194).
//       mint_clock_reader REJECTS the TSC/PMU sources — those are NOT
//       clock_gettime-backed and must go through mint_tsc_reader.
//
//   (2) UNPINNED TSC READ.  rdtsc reads a PER-CORE counter; comparing two
//       reads across a thread migration is meaningless.  mint_tsc_reader
//       CONSUMES a safety::CpuPinned proof whose `is_singleton_pin` is true,
//       and HOLDS it for the reader's lifetime — so a TSC reader is
//       unconstructible without a witnessed single-core pin (V-196).
//
//   (3) UNBOUNDED / WRONG-PHASE SLEEP.  BoundedSleeper<MaxNanos> caps the
//       sleep duration at the type level, and mint_bounded_sleep requires
//       the ctx row to carry Effect::Block — an Init / hot-path context
//       (no Block) cannot mint a sleeper at all.
//
// HS14 negative coverage (two distinct mismatch classes per mint):
//   mint_clock_reader  : non-clock-backed source (TscRaw) · non-ExecCtx
//   mint_tsc_reader    : missing pin proof (non-CpuPinned) · non-singleton pin
//   mint_bounded_sleep : ctx without Block (Init) · MaxNanos == 0

#include <crucible/Platform.h>                              // CRUCIBLE_INLINE
#include <crucible/fixy/Grant.h>                            // grant_base, which_dim, IsGrantTag
#include <crucible/fixy/Dim.h>                              // dim::DimensionAxis
#include <crucible/fixy/Hw.h>                               // fixy::hw::TscMode (single source)

#include <crucible/safety/ClockSource.h>                    // ClockSource_v, ClockSource, TscBytes
#include <crucible/safety/CpuPinned.h>                      // CpuPinned, PinningPosture, AffinityMask
#include <crucible/safety/Pre.h>                            // CRUCIBLE_PRE

#include <crucible/effects/ExecCtx.h>                       // IsExecCtx, CtxCanMint, Effect

#include <ctime>                                            // clock_gettime, clock_nanosleep, timespec, CLOCK_*
#include <cstdint>
#include <type_traits>
#include <utility>                                          // std::move

#if defined(__x86_64__)
#  include <x86intrin.h>                                    // __rdtsc, __rdtscp
#endif

namespace crucible::fixy::time {

namespace sf  = ::crucible::safety;
namespace eff = ::crucible::effects;
namespace ml  = ::crucible::algebra::lattices;

using sf::ClockSource_v;
using TscMode = ::crucible::fixy::hw::TscMode;

// ── clockid mapping — only the static-clockid clock_gettime sources ─
//
// Returns -1 for sources that the generic `ClockReader<Source>` cannot
// serve.  Two disjoint reasons land in the sentinel:
//   - TscRaw / TscSerialized / PmuCounter — not clock_gettime-backed at
//     all; consumers use mint_tsc_reader<Mode, PinT> instead.
//   - PtpHwClock (FIXY-V-201) — IS clock_gettime-backed but the clockid
//     is *per-fd* (derived via `(~fd << 3) | 3` from a /dev/ptpN fd),
//     not a static `CLOCK_*` constant.  A static-NTTP `ClockReader`
//     cannot satisfy it; consumers use topology::ptp_now(PtpClockFd) at
//     src/topology/Ptp.cpp, which mints the PtpHwClockBytes carrier at
//     the syscall boundary itself.
[[nodiscard]] consteval ::clockid_t clockid_for(ClockSource_v source) noexcept {
    switch (source) {
        case ClockSource_v::Realtime:     return CLOCK_REALTIME;
        case ClockSource_v::Monotonic:    return CLOCK_MONOTONIC;
        case ClockSource_v::MonotonicRaw: return CLOCK_MONOTONIC_RAW;
        case ClockSource_v::Boot:         return CLOCK_BOOTTIME;
        case ClockSource_v::ThreadCpu:    return CLOCK_THREAD_CPUTIME_ID;
        case ClockSource_v::ProcessCpu:   return CLOCK_PROCESS_CPUTIME_ID;
        case ClockSource_v::TscRaw:
        case ClockSource_v::TscSerialized:
        case ClockSource_v::PmuCounter:
            return -1;  // not clock_gettime-backed; use mint_tsc_reader
        case ClockSource_v::PtpHwClock:
            return -1;  // FIXY-V-201: per-fd clockid; use topology::ptp_now(fd)
        default:                          return -1;
    }
}

template <ClockSource_v Source>
concept ClockBacked = (clockid_for(Source) >= 0);

namespace detail {

// Per-arch raw counter reads (x86 rdtsc / aarch64 CNTVCT_EL0).
[[nodiscard]] CRUCIBLE_INLINE std::uint64_t read_raw_tsc() noexcept {
#if defined(__x86_64__)
    return __rdtsc();
#elif defined(__aarch64__)
    std::uint64_t value = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#else
#  error "fixy/Time.h: TSC read is supported on x86_64 and aarch64 only."
#endif
}

[[nodiscard]] CRUCIBLE_INLINE std::uint64_t read_raw_tsc_serialized() noexcept {
#if defined(__x86_64__)
    unsigned aux = 0;
    const std::uint64_t value = __rdtscp(&aux);  // rdtscp serializes wrt prior insns
    _mm_lfence();                                // serialize wrt subsequent insns
    return value;
#elif defined(__aarch64__)
    asm volatile("isb" ::: "memory");            // instruction-sync barrier
    std::uint64_t value = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#else
#  error "fixy/Time.h: TSC read is supported on x86_64 and aarch64 only."
#endif
}

// Recognize a safety::CpuPinned<Mask, Posture, Unit> instantiation.
template <typename T>
inline constexpr bool is_cpu_pinned_v = false;
template <ml::AffinityMask Mask, sf::PinningPosture Posture, typename Unit>
inline constexpr bool is_cpu_pinned_v<sf::CpuPinned<Mask, Posture, Unit>> = true;

// Map a TSC mode onto its ClockSource provenance.
template <TscMode Mode>
[[nodiscard]] consteval ClockSource_v tsc_source() noexcept {
    return Mode == TscMode::SerializedPinned ? ClockSource_v::TscSerialized
                                             : ClockSource_v::TscRaw;
}

}  // namespace detail

// A CpuPinned proof pinned to EXACTLY ONE core — the soundness floor for
// a TSC read (a multi-core mask still lets the thread migrate).
template <typename T>
concept IsSingletonCpuPin =
    detail::is_cpu_pinned_v<std::remove_cvref_t<T>>
    && std::remove_cvref_t<T>::is_singleton_pin;

// ── ClockReader<Source> — clock_gettime → ClockSource<Source> ───────
template <ClockSource_v Source>
    requires ClockBacked<Source>
struct ClockReader final {
    using result_type = sf::ClockSource<Source, std::uint64_t>;
    static constexpr ClockSource_v source = Source;

    [[nodiscard]] result_type read() const noexcept {
        std::timespec now{};
        (void)::clock_gettime(clockid_for(Source), &now);
        return result_type{ static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ULL
                            + static_cast<std::uint64_t>(now.tv_nsec) };
    }
};

// ── TscReader<Mode, PinT> — rdtsc, holding the single-core pin proof ─
//
// Move-only: it owns the CpuPinned proof (the consume-once discipline), so
// the pin cannot be released or re-claimed while a reader could still read.
template <TscMode Mode, typename PinT>
    requires (Mode != TscMode::NotAllowed) && IsSingletonCpuPin<PinT>
struct TscReader final {
    using result_type = std::conditional_t<Mode == TscMode::SerializedPinned,
                                            sf::TscSerializedBytes<std::uint64_t>,
                                            sf::TscBytes<std::uint64_t>>;
    static constexpr TscMode mode = Mode;

    explicit constexpr TscReader(PinT&& pin) noexcept : pin_{ std::move(pin) } {}

    TscReader(const TscReader&)            = delete;
    TscReader& operator=(const TscReader&) = delete;
    TscReader(TscReader&&) noexcept            = default;
    TscReader& operator=(TscReader&&) noexcept = default;
    ~TscReader()                               = default;

    [[nodiscard]] result_type read() const noexcept {
        if constexpr (Mode == TscMode::SerializedPinned) {
            return result_type{ detail::read_raw_tsc_serialized() };
        } else {
            return result_type{ detail::read_raw_tsc() };
        }
    }

    [[nodiscard]] PinT const& pin() const& noexcept { return pin_; }

private:
    PinT pin_;
};

// ── BoundedSleeper<MaxNanos> — clock_nanosleep capped at MaxNanos ────
template <std::uint64_t MaxNanos>
    requires (MaxNanos > 0)
struct BoundedSleeper final {
    static constexpr std::uint64_t max_nanos = MaxNanos;

    void sleep_for(std::uint64_t nanos) const noexcept {
        CRUCIBLE_PRE(nanos <= MaxNanos);  // statically-capped — cannot oversleep
        std::timespec request{ static_cast<std::time_t>(nanos / 1'000'000'000ULL),
                               static_cast<long>(nanos % 1'000'000'000ULL) };
        (void)::clock_nanosleep(CLOCK_MONOTONIC, 0, &request, nullptr);
    }
};

}  // namespace crucible::fixy::time

// ═════════════════════════════════════════════════════════════════════
// ── grant tag family (crucible::fixy::grant::time) ────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant::time {

namespace ft = ::crucible::fixy::time;

// (1) clock_read<Source> — a clock_gettime read (SyscallSurface / vDSO).
template <ft::ClockSource_v Source>
struct clock_read final : grant_base {};

// (2) tsc_read<Mode> — a timestamp-counter read (HwInstruction).
template <ft::TscMode Mode>
struct tsc_read final : grant_base {};

// (3) sleep<MaxNanos> — a bounded blocking sleep (SyscallSurface + Block).
template <std::uint64_t MaxNanos>
struct sleep final : grant_base {};

}  // namespace crucible::fixy::grant::time

// ── which_dim routing — CR-09 locked namespace ───────────────────────

namespace crucible::fixy::grant {

namespace ft = ::crucible::fixy::time;

template <ft::ClockSource_v Source>
struct which_dim<time::clock_read<Source>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <ft::TscMode Mode>
struct which_dim<time::tsc_read<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <std::uint64_t MaxNanos>
struct which_dim<time::sleep<MaxNanos>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

}  // namespace crucible::fixy::grant

// ═════════════════════════════════════════════════════════════════════
// ── The three §XXI mint factories ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::time {

// clock reader: any ExecCtx + a clock_gettime-backed source.
template <typename Ctx, ClockSource_v Source>
concept CtxFitsClockReaderMint = eff::IsExecCtx<Ctx> && ClockBacked<Source>;

// tsc reader: any ExecCtx + a real TSC mode + a single-core pin proof.
template <typename Ctx, TscMode Mode, typename PinT>
concept CtxFitsTscReaderMint =
    eff::IsExecCtx<Ctx> && (Mode != TscMode::NotAllowed) && IsSingletonCpuPin<PinT>;

// bounded sleep: a Block-capable ctx + a non-zero static cap.
template <typename Ctx, std::uint64_t MaxNanos>
concept CtxFitsBoundedSleepMint =
    eff::CtxCanMint<Ctx, eff::Effect::Block> && (MaxNanos > 0);

template <ClockSource_v Source, eff::IsExecCtx Ctx>
    requires CtxFitsClockReaderMint<Ctx, Source>
[[nodiscard]] constexpr ClockReader<Source> mint_clock_reader(Ctx const&) noexcept {
    return {};
}

template <TscMode Mode, eff::IsExecCtx Ctx, typename PinT>
    requires CtxFitsTscReaderMint<Ctx, Mode, PinT>
[[nodiscard]] constexpr TscReader<Mode, std::remove_cvref_t<PinT>>
mint_tsc_reader(Ctx const&, PinT&& pin) noexcept {
    return TscReader<Mode, std::remove_cvref_t<PinT>>{ std::move(pin) };
}

template <std::uint64_t MaxNanos, eff::IsExecCtx Ctx>
    requires CtxFitsBoundedSleepMint<Ctx, MaxNanos>
[[nodiscard]] constexpr BoundedSleeper<MaxNanos> mint_bounded_sleep(Ctx const&) noexcept {
    return {};
}

}  // namespace crucible::fixy::time

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time + a guarded runtime smoke) ────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::time::detail::v190_self_test {

namespace gt  = ::crucible::fixy::grant::time;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;

// ── grant tags: 1-byte EBO markers routed to the right axes ─────────
static_assert(IsGrantTag<gt::clock_read<ClockSource_v::Boot>>);
static_assert(IsGrantTag<gt::tsc_read<TscMode::Raw>>);
static_assert(IsGrantTag<gt::sleep<1000>>);
static_assert(sizeof(gt::clock_read<ClockSource_v::Monotonic>) == 1);
static_assert(sizeof(gt::tsc_read<TscMode::SerializedPinned>)  == 1);
static_assert(sizeof(gt::sleep<1>)                             == 1);
static_assert(which_dim_v<gt::clock_read<ClockSource_v::Boot>> == D::SyscallSurface);
static_assert(which_dim_v<gt::tsc_read<TscMode::Raw>>          == D::HwInstruction);
static_assert(which_dim_v<gt::sleep<4096>>                     == D::SyscallSurface);

// ── ClockBacked: clock_gettime sources yes, TSC/PMU no ──────────────
static_assert( ClockBacked<ClockSource_v::Monotonic>);
static_assert( ClockBacked<ClockSource_v::Boot>);
static_assert(!ClockBacked<ClockSource_v::TscRaw>);
static_assert(!ClockBacked<ClockSource_v::PmuCounter>);

// ── ClockReader result-type provenance (the V-194 distinction) ──────
static_assert(std::is_same_v<ClockReader<ClockSource_v::Boot>::result_type,
                             sf::BootClockBytes<std::uint64_t>>);
static_assert(std::is_same_v<ClockReader<ClockSource_v::Monotonic>::result_type,
                             sf::MonotonicClockBytes<std::uint64_t>>);
static_assert(!std::is_same_v<ClockReader<ClockSource_v::Boot>,
                              ClockReader<ClockSource_v::Monotonic>>);

// ── IsSingletonCpuPin gate ──────────────────────────────────────────
using SinglePin = sf::CpuPinned<ml::AffinityMask::single(0), sf::PinningPosture::PinnedExplicit, int>;
using MultiPin  = sf::CpuPinned<ml::AffinityMask::range(0, 1), sf::PinningPosture::PinnedExplicit, int>;
static_assert( IsSingletonCpuPin<SinglePin>);
static_assert(!IsSingletonCpuPin<MultiPin>);
static_assert(!IsSingletonCpuPin<int>);

// ── TscReader result-type per mode ──────────────────────────────────
static_assert(std::is_same_v<TscReader<TscMode::Raw, SinglePin>::result_type,
                             sf::TscBytes<std::uint64_t>>);
static_assert(std::is_same_v<TscReader<TscMode::SerializedPinned, SinglePin>::result_type,
                             sf::TscSerializedBytes<std::uint64_t>>);

// ── BoundedSleeper static cap ───────────────────────────────────────
static_assert(BoundedSleeper<1'000'000>::max_nanos == 1'000'000ULL);

// ── Runtime smoke: read a Boot clock, a pinned TSC, a near-zero sleep ─
inline bool runtime_smoke_test() {
    namespace eff_t = ::crucible::effects;
    eff_t::ColdInitCtx init{};
    eff_t::BgDrainCtx  bg{};

    auto boot_reader = mint_clock_reader<ClockSource_v::Boot>(init);
    const auto t0 = boot_reader.read();
    if (t0.peek() == 0) return false;  // CLOCK_BOOTTIME is monotonic-positive

    auto pin = sf::mint_cpu_pinned<ml::AffinityMask::single(0),
                                   sf::PinningPosture::PinnedExplicit, int>(0);
    auto tsc_reader = mint_tsc_reader<TscMode::Raw>(init, std::move(pin));
    (void)tsc_reader.read();

    auto sleeper = mint_bounded_sleep<1'000>(bg);  // 1 µs cap; sleep ~0
    sleeper.sleep_for(0);
    return true;
}

}  // namespace crucible::fixy::time::detail::v190_self_test
