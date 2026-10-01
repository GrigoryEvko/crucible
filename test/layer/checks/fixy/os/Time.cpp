// The compile-time checks of fixy/os/Time.h.

#include <fixy/os/Time.h>

namespace fixy::time::detail::clock_reader_invariants {

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
static_assert(IsSingletonCpuPin<SinglePin const&>);

// A class that copies the shape of a pin is not a pin.
struct ShapeOfAPin {
    static constexpr bool is_singleton_pin = true;
    static constexpr sf::PinningPosture posture = sf::PinningPosture::PinnedExplicit;
};
static_assert(!IsSingletonCpuPin<ShapeOfAPin>);

static_assert(std::is_same_v<TscReader<TscMode::Raw, SinglePin>::result_type, sf::TscBytes<std::uint64_t>>);
static_assert(std::is_same_v<TscReader<TscMode::SerializedPinned, SinglePin>::result_type,
                             sf::TscSerializedBytes<std::uint64_t>>);

// The TSC reader's one door is the mint.  The reader owns its pin, so it
// moves and does not copy.
static_assert(!std::is_constructible_v<TscReader<TscMode::Raw, SinglePin>, SinglePin&&>);
static_assert(!std::is_copy_constructible_v<TscReader<TscMode::Raw, SinglePin>>);
static_assert(std::is_nothrow_move_constructible_v<TscReader<TscMode::Raw, SinglePin>>);

// The gate of the TSC mint: a context off the foreground path, a pin the
// caller gives up, and the explicit posture.
using AutoPin = sf::CpuPinned<ml::AffinityMask::single(0), sf::PinningPosture::PinnedAuto, int>;
using InitCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init>>;
static_assert(CtxFitsTscReaderMint<InitCtx, TscMode::Raw, SinglePin>);
static_assert(!CtxFitsTscReaderMint<eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>, TscMode::Raw, SinglePin>,
              "the foreground hot path must not read the TSC: the value differs across machines and breaks replay");
static_assert(!CtxFitsTscReaderMint<InitCtx, TscMode::Raw, SinglePin&>,
              "an lvalue pin must be refused: the mint would move the caller's pin out with no std::move in sight");
static_assert(!CtxFitsTscReaderMint<InitCtx, TscMode::Raw, SinglePin const>);
static_assert(!CtxFitsTscReaderMint<InitCtx, TscMode::Raw, AutoPin>,
              "an auto pin can still migrate, so it must not feed a per-core counter");
static_assert(!CtxFitsTscReaderMint<InitCtx, TscMode::NotAllowed, SinglePin>);

static_assert(BoundedSleeper<1000000>::max_nanos == 1000000ULL);
static_assert(!std::is_default_constructible_v<BoundedSleeper<1000000>>
                  && !std::is_copy_constructible_v<BoundedSleeper<1000000>>
                  && !std::is_move_constructible_v<BoundedSleeper<1000000>>,
              "a sleeper comes only from mint_bounded_sleep and never leaves the frame that holds it");
static_assert(!std::is_trivially_copyable_v<BoundedSleeper<1000000>> && !std::is_trivially_copyable_v<MonotonicClock>
                  && !std::is_trivially_copyable_v<ClockReader<ClockSource_v::Realtime>>,
              "std::bit_cast must not build a sleeper or a clock reader from bytes");
static_assert(IsSleepBound<1> && IsSleepBound<max_bounded_sleep_nanos>);
static_assert(!IsSleepBound<0> && !IsSleepBound<max_bounded_sleep_nanos + 1>);

// The gate and the clamp, as properties of the types.  A foreground
// context is refused at the mint and a background one admitted; the
// three non-decreasing sources are clamped and Realtime is not; the
// reader's one door is the mint, so it is not default-constructible from
// outside, and no reader copies or moves out of the frame that minted
// it.  The behaviour under a sequence of reads, and
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
static_assert(!std::is_copy_constructible_v<ClockReader<ClockSource_v::Realtime>>
                  && !std::is_move_constructible_v<ClockReader<ClockSource_v::Realtime>>,
              "an unclamped reader stays in the frame whose context the mint read, as a clamped one does");

// The PTP reader.  Its readings are PtpHwClock readings and are never
// clamped.  It is not built from outside: no default constructor, no
// public constructor from a descriptor.  It moves, because it owns the
// descriptor, and it does not copy.  Its mint needs the clock gate and
// the filesystem gate, so a context that lacks Block is refused as well
// as the foreground one.
using BackgroundFsCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO, eff::Effect::Block>>;
static_assert(std::is_same_v<PtpClockReader::result_type, sf::PtpHwClockBytes<std::uint64_t>>);
static_assert(!PtpClockReader::is_clamped);
static_assert(!std::is_default_constructible_v<PtpClockReader>);
static_assert(!std::is_constructible_v<PtpClockReader, ::fixy::fs::OwnedFd>);
static_assert(!std::is_copy_constructible_v<PtpClockReader>);
static_assert(std::is_nothrow_move_constructible_v<PtpClockReader>);
static_assert(CtxFitsPtpClockReaderMint<BackgroundFsCtx>);
static_assert(!CtxFitsPtpClockReaderMint<BackgroundCtx>);
static_assert(!CtxFitsPtpClockReaderMint<ForegroundCtx>);
static_assert(!std::is_default_constructible_v<PtpDeviceDoor> && !std::is_copy_constructible_v<PtpDeviceDoor>
                  && !std::is_move_constructible_v<PtpDeviceDoor>,
              "No object of the PTP device door exists.  Its private member is the only open of a /dev/ptpN node.");

// The clock id of descriptor 3 is (~3 << 3) | 3, which is -29 as a
// clockid_t, and the kernel's decode (~id >> 3) gives the descriptor back.
static_assert(detail::ptp_clockid_from_fd(3) == -29);
static_assert((~static_cast<unsigned int>(detail::ptp_clockid_from_fd(3)) >> 3u) == 3u);

// The readers and the sleeper are exercised in test/fixy/test_os_time.cpp,
// which is also where the TSC leg lives: a TSC read needs a pin from
// fixy::sched::mint_affinity, and that lives in fixy/os/Sched.h, so the
// case belongs in a translation unit that includes both headers.

}  // namespace fixy::time::detail::clock_reader_invariants
