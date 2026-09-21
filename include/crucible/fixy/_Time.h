#pragma once

#include <crucible/Platform.h>
#include <crucible/fixy/_Grant.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Hw.h>

#include <crucible/safety/_ClockSource.h>
#include <crucible/safety/_CpuPinned.h>
#include <crucible/safety/_Pre.h>

#include <crucible/effects/_ExecCtx.h>

#include <ctime>
#include <cstdint>
#include <type_traits>
#include <utility>

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

namespace crucible::fixy::time {

namespace sf = ::crucible::safety;
namespace eff = ::crucible::effects;
namespace ml = ::crucible::algebra::lattices;

using sf::ClockSource_v;
using sf::MonotonicClockBytes;
using sf::mint_clock_source;
using TscMode = ::crucible::fixy::hw::TscMode;

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
#error "fixy/Time.h: TSC read is supported on x86_64 and aarch64 only."
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
#error "fixy/Time.h: TSC read is supported on x86_64 and aarch64 only."
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

}  // namespace crucible::fixy::time

namespace crucible::fixy::grant::time {

namespace ft = ::crucible::fixy::time;

template <ft::ClockSource_v Source>
struct clock_read final : grant_base {};

template <ft::TscMode Mode>
struct tsc_read final : grant_base {};

template <std::uint64_t MaxNanos>
struct sleep final : grant_base {};

}  // namespace crucible::fixy::grant::time

namespace crucible::fixy::grant {

namespace ft = ::crucible::fixy::time;

template <ft::ClockSource_v Source>
struct which_dim<time::clock_read<Source>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <ft::TscMode Mode>
struct which_dim<time::tsc_read<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {
};

template <std::uint64_t MaxNanos>
struct which_dim<time::sleep<MaxNanos>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

}  // namespace crucible::fixy::grant

namespace crucible::fixy::time {

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

}  // namespace crucible::fixy::time

namespace crucible::fixy::time::detail::v190_self_test {

namespace gt = ::crucible::fixy::grant::time;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(IsGrantTag<gt::clock_read<ClockSource_v::Boot>>);
static_assert(IsGrantTag<gt::tsc_read<TscMode::Raw>>);
static_assert(IsGrantTag<gt::sleep<1000>>);
static_assert(sizeof(gt::clock_read<ClockSource_v::Monotonic>) == 1);
static_assert(sizeof(gt::tsc_read<TscMode::SerializedPinned>) == 1);
static_assert(sizeof(gt::sleep<1>) == 1);
static_assert(which_dim_v<gt::clock_read<ClockSource_v::Boot>> == D::SyscallSurface);
static_assert(which_dim_v<gt::tsc_read<TscMode::Raw>> == D::HwInstruction);
static_assert(which_dim_v<gt::sleep<4096>> == D::SyscallSurface);

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

inline bool runtime_smoke_test() {
    namespace eff_t = ::crucible::effects;
    eff_t::ColdInitCtx init{};
    eff_t::BgDrainCtx bg{};

    auto boot_reader = mint_clock_reader<ClockSource_v::Boot>(init);
    const auto t0 = boot_reader.read();
    if (t0.peek() == 0) return false;  // a running system never reads zero here

    auto pin = sf::mint_cpu_pinned<ml::AffinityMask::single(0), sf::PinningPosture::PinnedExplicit, int>(0);
    auto tsc_reader = mint_tsc_reader<TscMode::Raw>(init, std::move(pin));
    (void)tsc_reader.read();

    auto sleeper = mint_bounded_sleep<1000>(bg);
    sleeper.sleep_for(0);
    return true;
}

}  // namespace crucible::fixy::time::detail::v190_self_test
