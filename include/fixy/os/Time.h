#pragma once

// Clock and counter readers, each minted against an execution context.
//
// The header also holds the gate that keeps a clock read off the
// replay-bound foreground path, and the clamp that keeps two reads
// through one reader from regressing.
//
// The PTP hardware clock has a reader of its own, PtpClockReader.  The
// reader owns the /dev/ptpN descriptor, stamps only what clock_gettime
// returned on it, and refuses a negative field.  It clamps no field to
// zero, because a stamp of a clamped value claims a reading that the
// clock never returned.

#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/Fs.h>
#include <foundation/Lifetime.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/reflect/Instance.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <expected>
#include <limits>
#include <system_error>
#include <type_traits>
#include <utility>

#include <fcntl.h>
#include <linux/ptp_clock.h>
#include <sys/ioctl.h>

namespace fixy::time {

// sf names ::fixy, where the clock-source wrapper and the pin proof live.
namespace sf = ::fixy;
namespace eff = ::foundation::effects;
namespace ml = ::foundation::algebra::lattices;

using sf::ClockSource_v;
using sf::MonotonicClockBytes;

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

// The two x86_64 reads call the builtins that __rdtsc, __rdtscp and
// _mm_lfence wrap, so no includer pays for the parse of an intrinsics header.
[[nodiscard]] CRUCIBLE_INLINE std::uint64_t read_raw_tsc() noexcept {
#if defined(__x86_64__)
    return __builtin_ia32_rdtsc();
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
    const std::uint64_t value = __builtin_ia32_rdtscp(&aux);  // ordered against earlier instructions
    __builtin_ia32_lfence();  // ordered against later instructions
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

template <TscMode Mode>
[[nodiscard]] consteval ClockSource_v tsc_source() noexcept {
    return Mode == TscMode::SerializedPinned ? ClockSource_v::TscSerialized : ClockSource_v::TscRaw;
}

// The kernel names the clock behind an open /dev/ptpN descriptor as
// (~fd << 3) | CLOCKFD, where CLOCKFD is 3.  No userspace header defines
// the macro, and this function does the arithmetic itself.  It uses
// unsigned arithmetic, and it shifts no signed value.  The conversion
// back to clockid_t keeps all 32 bits, and the kernel decodes them.
inline constexpr unsigned int ptp_clockfd_tag = 3u;

[[nodiscard]] constexpr ::clockid_t ptp_clockid_from_fd(int fd) noexcept {
    const unsigned int complemented = ~static_cast<unsigned int>(fd);
    return static_cast<::clockid_t>((complemented << 3u) | ptp_clockfd_tag);
}

}  // namespace detail

// The nanosecond count of one clock reading, or the reason there is none.
// The function refuses a negative second or nanosecond field, and a
// nanosecond field of a full second or more.  A Realtime reading before
// 1970 has a negative second field, and it is refused.  The function
// clamps no value into range, because a stamp must hold what the clock
// returned.  It refuses a count that does not fit in 64 bits as an
// overflow.  It is a pure function of the timespec, and a test can give
// it the values that no working clock returns.  Each reader converts
// each read with it, and so does a caller that reads a hardware timestamp
// from a socket control message.
[[nodiscard]] constexpr std::expected<std::uint64_t, std::error_code>
nanos_from_timespec(std::timespec const& reading) noexcept {
    constexpr std::uint64_t nanos_per_second = 1000000000ULL;
    if (reading.tv_sec < 0 || reading.tv_nsec < 0 || reading.tv_nsec >= static_cast<long>(nanos_per_second)) {
        return std::unexpected{std::make_error_code(std::errc::result_out_of_range)};
    }
    const auto seconds = static_cast<std::uint64_t>(reading.tv_sec);
    const auto nanos = static_cast<std::uint64_t>(reading.tv_nsec);
    constexpr auto limit = std::numeric_limits<std::uint64_t>::max();
    if (seconds > (limit - nanos) / nanos_per_second) {
        return std::unexpected{std::make_error_code(std::errc::value_too_large)};
    }
    return seconds * nanos_per_second + nanos;
}

namespace detail {

// Reads one clock, and converts the reading with nanos_from_timespec.  A
// failed call gives its errno, and a value that the count cannot hold
// gives the error of the conversion.  ClockReader::read and
// PtpClockReader::read are its callers, and a mint gate keeps each reader
// off the replay-bound foreground path.
[[nodiscard]] inline std::expected<std::uint64_t, std::error_code> read_clock_nanos(::clockid_t clock) noexcept {
    std::timespec now{};
    if (::clock_gettime(clock, &now) != 0)
        [[unlikely]] {  // SYSCALL-CAP-OK: detail helper of ClockReader::read and PtpClockReader::read, whose builders the CtxFitsClockReaderMint and CtxFitsPtpClockReaderMint gates admit
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return nanos_from_timespec(now);
}

}  // namespace detail

// A multi-core mask still lets the thread migrate, and the counter is
// per-core, so only a single-core pin makes two reads comparable.
//
// This concept reads the mask off the type, and the type is enough.
// CpuPinned has one constructor, it is private, and its sole friend is
// fixy::sched::mint_affinity, which hands a proof back only after
// sched_setaffinity returned 0 for that same mask.  So a PinT that
// satisfies this concept was pinned, and the gate stands on the pin
// rather than on the shape of a token anyone could build.
//
// A forged single-core pin passes this concept on a thread that can
// migrate, and two TSC reads then come from two different counters,
// with no crash and no diagnostic to say so.
// test/fixy/neg/neg_os_cpu_pinned_*.cpp is the standing witness that each
// route to a forged pin is closed.
//
// The recognition is the reflection query of foundation/reflect/
// Instance.h, which no translation unit can specialize.  A variable
// template in its place could be specialized for a fake class that
// declares is_singleton_pin, and that class would pass this concept
// with no pin behind it.
template <typename T>
concept IsSingletonCpuPin =
    ::foundation::reflect::IsInstanceOf<T, ^^sf::CpuPinned> && std::remove_cvref_t<T>::is_singleton_pin;

// The pin that a TSC reader takes.  It is a single-core pin with the
// explicit posture, because an auto pin can still migrate.  The reader
// owns it, so the caller gives up its pin: an lvalue or a const pin is
// refused, and the call site names the move.  A forwarding reference
// that takes an lvalue moves the caller's pin out with no std::move in
// sight.
template <typename PinT>
concept IsOwnedExplicitSingletonPin = IsSingletonCpuPin<PinT> && !std::is_reference_v<PinT> && !std::is_const_v<PinT>
                                   && sf::pin_meets_posture(^^PinT, sf::PinningPosture::PinnedExplicit);

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

// The clamp: if the underlying clock goes backward, the previously observed
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
// owns Bg, Init or Test.  The name says monotonic, but the gate holds
// for every clock-backed source, because the replay argument does not
// depend on which clock diverges.
template <typename Ctx>
concept CtxFitsMonotonicClock = eff::CtxOwnsAnyOf<Ctx, eff::Effect::Bg, eff::Effect::Init, eff::Effect::Test>;

template <typename Ctx, ClockSource_v Source>
concept CtxFitsClockReaderMint = CtxFitsMonotonicClock<Ctx> && ClockBacked<Source>;

template <ClockSource_v Source>
    requires ClockBacked<Source>
struct ClockReader;

template <TscMode Mode, typename PinT>
    requires(Mode != TscMode::NotAllowed) && IsSingletonCpuPin<PinT>
struct TscReader;

struct PtpClockReader;

// The one door that stamps a value with the clock it came from.  The
// constructor of ClockSource is private and this struct is its sole
// friend.  The stamp here is private too, and the only friends are the
// three readers below, each of which calls it on the value it has just
// read from that same clock.  A reading of Boot therefore holds a value
// that CLOCK_BOOTTIME returned, which is the fact the SuspendBehavior
// and DetSafe folds rest on.
namespace detail {
struct clock_stamp_access final {
private:
    template <ClockSource_v Source, typename T>
    [[nodiscard]] static constexpr sf::ClockSource<Source, T>
    stamp(T raw) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return sf::ClockSource<Source, T>{std::move(raw)};
    }

    template <ClockSource_v Source>
        requires ClockBacked<Source>
    friend struct ::fixy::time::ClockReader;

    template <TscMode Mode, typename PinT>
        requires(Mode != TscMode::NotAllowed) && IsSingletonCpuPin<PinT>
    friend struct ::fixy::time::TscReader;

    friend struct ::fixy::time::PtpClockReader;
};
}  // namespace detail

template <ClockSource_v Source>
    requires ClockBacked<Source>
struct ClockReader final {
    using result_type = sf::ClockSource<Source, std::uint64_t>;
    static constexpr ClockSource_v source = Source;
    static constexpr bool is_clamped = is_non_decreasing(Source);

    // A read that fails, or that returns a value the stamp cannot hold,
    // gives an error and no reading.  The floor of a clamped reader does
    // not move on a failed read.
    [[nodiscard]] std::expected<result_type, std::error_code> read() const noexcept {
        return detail::read_clock_nanos(clockid_for(Source)).transform([this](std::uint64_t raw) noexcept {
            if constexpr (is_clamped) {
                return detail::clock_stamp_access::stamp<Source>(clamp_non_decreasing(last_, raw));
            } else {
                return detail::clock_stamp_access::stamp<Source>(raw);
            }
        });
    }

    // The reader stays in the frame that minted it.  The gate of the mint
    // says that the frame is off the replay-bound foreground path, and a
    // copy or a move could carry the reader onto that path.  A clamped
    // reader is pinned by its floor as well.
    ClockReader(const ClockReader&) = delete("a clock reader stays in the frame whose context the mint read");
    ClockReader(ClockReader&&) = delete("a clock reader stays in the frame whose context the mint read");
    ClockReader& operator=(const ClockReader&) = delete("a clock reader is not assignable");
    ClockReader& operator=(ClockReader&&) = delete("a clock reader is not assignable");
    ~ClockReader() = default;

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
    // const read.  AtomicMonotonic is pinned, so a clamped reader has one
    // address for the lifetime of its floor.  An unclamped reader carries
    // nothing.
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
    // No byte route builds a reader, so a reader exists only in a frame
    // whose context the mint read.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// The clamped, gated monotonic reader.
using MonotonicClock = ClockReader<ClockSource_v::Monotonic>;

template <typename Ctx, TscMode Mode, typename PinT>
concept CtxFitsTscReaderMint =
    CtxFitsMonotonicClock<Ctx> && (Mode != TscMode::NotAllowed) && IsOwnedExplicitSingletonPin<PinT>;

// The reader owns the pin proof for its whole lifetime, so the pin cannot be
// released while a read is still possible.
//
// The counter belongs to one core, and the pin to one thread.  So each
// read asks the pin whether it is in force on the calling thread, and
// gives an error and no reading when it is not.  A reader that a
// reference or a move took to another thread, and a reader whose thread
// pinned again, read nothing.  The check is one load of a thread-local
// value and one comparison.  It is not on the foreground path, because
// the mint refuses a foreground context.
template <TscMode Mode, typename PinT>
    requires(Mode != TscMode::NotAllowed) && IsSingletonCpuPin<PinT>
struct TscReader final {
    using result_type = std::conditional_t<Mode == TscMode::SerializedPinned, sf::TscSerializedBytes<std::uint64_t>,
                                           sf::TscBytes<std::uint64_t>>;
    static constexpr TscMode mode = Mode;

    TscReader(const TscReader&) = delete("a TSC reader owns its pin proof, and a pin claim is never held two times");
    TscReader& operator=(const TscReader&) = delete("a TSC reader owns its pin proof");
    TscReader(TscReader&&) noexcept = default;
    TscReader& operator=(TscReader&&) noexcept = default;
    ~TscReader() = default;

    [[nodiscard]] std::expected<result_type, std::error_code> read() const noexcept {
        if (!pin_.is_in_force()) [[unlikely]] {
            return std::unexpected{std::make_error_code(std::errc::operation_not_permitted)};
        }
        if constexpr (Mode == TscMode::SerializedPinned) {
            return detail::clock_stamp_access::stamp<result_type::source>(detail::read_raw_tsc_serialized());
        } else {
            return detail::clock_stamp_access::stamp<result_type::source>(detail::read_raw_tsc());
        }
    }

    [[nodiscard]] PinT const& pin() const& noexcept { return pin_; }

private:
    // One door.  The mint checks that the pin is in force on the calling
    // thread, and then gives the pin to this constructor.  The mint is
    // the only friend.
    explicit constexpr TscReader(PinT&& pin) noexcept : pin_{std::move(pin)} {}

    template <TscMode FriendMode, eff::IsExecCtx FriendCtx, typename FriendPinT>
        requires CtxFitsTscReaderMint<FriendCtx, FriendMode, FriendPinT>
    friend auto mint_tsc_reader(FriendCtx const&, FriendPinT&&) noexcept
        -> std::expected<TscReader<FriendMode, std::remove_cvref_t<FriendPinT>>, std::error_code>;

    PinT pin_;
};

// A PTP hardware clock sits behind an open /dev/ptpN descriptor, and the
// open is a file system call that can hold the caller.  The mint needs
// the monotonic-clock gate, which keeps a clock read off the
// replay-bound foreground path.  It also needs the file system gate,
// which demands IO and Block.
template <typename Ctx>
concept CtxFitsPtpClockReaderMint = CtxFitsMonotonicClock<Ctx> && ::fixy::fs::CtxAdmitsFs<Ctx>;

// The index of a /dev/ptpN device.  The kernel numbers PTP clocks from
// zero, and the bound keeps the device path in the fixed buffer that the
// mint builds it in.
using PtpDeviceIndex = sf::Capped<std::uint16_t{255}, std::uint16_t>;

// The reader of one PTP hardware clock.  It owns the descriptor of the
// device.  The clock id that it reads through then names a descriptor
// that is still open.
//
// The reader clamps no read.  The servo that disciplines a PHC can step
// it.  For that reason is_non_decreasing(PtpHwClock) is false, and a floor
// here invents an order that the clock does not have.  A read that fails,
// or that returns a value the stamp cannot hold, gives an error and no
// reading.
struct PtpClockReader final {
    using result_type = sf::PtpHwClockBytes<std::uint64_t>;
    static constexpr ClockSource_v source = ClockSource_v::PtpHwClock;
    static constexpr bool is_clamped = is_non_decreasing(ClockSource_v::PtpHwClock);
    static_assert(!is_clamped, "a clock that can step must not have a floor, because the floor invents an order");

    PtpClockReader(const PtpClockReader&) =
        delete("the reader owns its device descriptor, and a copy closes it two times");
    PtpClockReader& operator=(const PtpClockReader&) = delete("the reader owns its device descriptor");
    PtpClockReader(PtpClockReader&&) noexcept = default;
    PtpClockReader& operator=(PtpClockReader&&) noexcept = default;
    ~PtpClockReader() = default;

    [[nodiscard]] std::expected<result_type, std::error_code> read() const noexcept {
        return detail::read_clock_nanos(detail::ptp_clockid_from_fd(fd_.get()))
            .transform([](std::uint64_t nanos) noexcept {
                return detail::clock_stamp_access::stamp<ClockSource_v::PtpHwClock>(nanos);
            });
    }

    // The capabilities of the clock, as PTP_CLOCK_GETCAPS2 reports them, or
    // the errno of a failed query.  The reader owns the descriptor, so the
    // query names a device that is still open.
    [[nodiscard]] std::expected<::ptp_clock_caps, std::error_code> caps() const noexcept {
        ::ptp_clock_caps kernel_caps{};
        if (::ioctl(fd_.get(), PTP_CLOCK_GETCAPS2, &kernel_caps)
            != 0) {  // SYSCALL-CAP-OK: PtpClockReader::caps, sole builder mint_ptp_clock_reader ctx-gate (CtxFitsPtpClockReaderMint)
            return std::unexpected{std::error_code{errno, std::system_category()}};
        }
        return kernel_caps;
    }

private:
    // One door.  The mint opens the device and gives the descriptor to
    // this constructor.  The mint is the only friend.
    explicit PtpClockReader(::fixy::fs::OwnedFd fd) noexcept : fd_{std::move(fd)} {}

    template <eff::IsExecCtx Ctx>
        requires CtxFitsPtpClockReaderMint<Ctx>
    friend std::expected<PtpClockReader, std::error_code> mint_ptp_clock_reader(Ctx const&, PtpDeviceIndex) noexcept;

    ::fixy::fs::OwnedFd fd_;
};

// The door to the open of a /dev/ptpN node.  No object of it exists.  Its
// member is private, and its one friend is mint_ptp_clock_reader.
// fixy::fs::OwnedFd befriends this class, so a descriptor of a PTP device
// comes only from that mint, after its gate.  The door builds the device
// path from the index itself, so a caller cannot point it at a file that
// is not a PTP device node.
class PtpDeviceDoor final : ::foundation::NoObject<PtpDeviceDoor> {
    template <eff::IsExecCtx Ctx>
        requires CtxFitsPtpClockReaderMint<Ctx>
    friend std::expected<PtpClockReader, std::error_code> mint_ptp_clock_reader(Ctx const&, PtpDeviceIndex) noexcept;

    // Opens /dev/ptpN read-only and returns its descriptor, or the errno
    // of a failed open.
    [[nodiscard]] static std::expected<::fixy::fs::OwnedFd, int> open_(PtpDeviceIndex index) noexcept {
        // "/dev/ptp", a maximum of three digits for an index of 255 or
        // less, and the null at the end: 12 bytes, in a buffer of 16.
        std::array<char, 16> path{'/', 'd', 'e', 'v', '/', 'p', 't', 'p'};
        std::size_t length = 8;
        std::array<char, 3> digits{};
        std::size_t digit_count = 0;
        std::uint16_t remaining = index.value();
        do {
            digits[digit_count++] = static_cast<char>('0' + remaining % 10u);
            remaining = static_cast<std::uint16_t>(remaining / 10u);
        } while (remaining != 0);
        while (digit_count > 0) {
            path[length++] = digits[--digit_count];
        }
        path[length] = '\0';

        const int fd = ::open(
            path.data(),
            O_RDONLY
                | O_CLOEXEC);  // SYSCALL-CAP-OK: PtpDeviceDoor::open_, sole caller mint_ptp_clock_reader ctx-gate (CtxFitsPtpClockReaderMint)
        if (fd < 0) {
            return std::unexpected{errno};
        }
        return ::fixy::fs::OwnedFd{fd};
    }
};

// The largest bound a sleeper takes.  A deadline is the time of the
// monotonic clock plus the sleep, in nanoseconds.  The nanosecond field
// of the clock is below one second, so the sum fits in 64 bits for each
// bound up to 2^62.
inline constexpr std::uint64_t max_bounded_sleep_nanos = std::uint64_t{1} << 62;

template <std::uint64_t MaxNanos>
concept IsSleepBound = (MaxNanos > 0) && (MaxNanos <= max_bounded_sleep_nanos);

// The gate states the bound of the sleeper as well, so the concept is the
// whole rule of the mint.  A call with a bound out of range is a refused
// candidate: the return type BoundedSleeper<MaxNanos> fails its own
// constraint first.
template <typename Ctx, std::uint64_t MaxNanos>
concept CtxFitsBoundedSleepMint = eff::CtxOwnsCapability<Ctx, eff::Effect::Block> && IsSleepBound<MaxNanos>;

// The right to block the calling thread for at most MaxNanos in one call.
//
// The sleeper comes only from mint_bounded_sleep, whose gate asks for a
// context that owns Block.  Its constructor is private and the mint is
// the only friend.  It is neither copyable nor movable, so it stays in
// the frame whose context the gate read, and a thread that owns no Block
// does not get it by a copy or a move.
//
// A signal handler that runs during the sleep ends clock_nanosleep early
// with EINTR.  The sleep then continues to the same deadline on the
// monotonic clock, so a signal does not shorten it and a restart does
// not add the time already slept.  Another failure gives its error, and
// the caller must look at it.
template <std::uint64_t MaxNanos>
    requires IsSleepBound<MaxNanos>
struct BoundedSleeper final {
    static constexpr std::uint64_t max_nanos = MaxNanos;

    BoundedSleeper(const BoundedSleeper&) = delete("a sleeper stays in the frame whose context the mint read");
    BoundedSleeper(BoundedSleeper&&) = delete("a sleeper stays in the frame whose context the mint read");
    BoundedSleeper& operator=(const BoundedSleeper&) = delete("a sleeper is not assignable");
    BoundedSleeper& operator=(BoundedSleeper&&) = delete("a sleeper is not assignable");
    ~BoundedSleeper() = default;

    [[nodiscard]] std::expected<void, std::error_code> sleep_for(std::uint64_t nanos) const noexcept {
        CRUCIBLE_PRE(nanos <= MaxNanos);
        constexpr std::uint64_t nanos_per_second = 1000000000ULL;
        std::timespec deadline{};
        if (::clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
            [[unlikely]] {  // SYSCALL-CAP-OK: BoundedSleeper::sleep_for, sole builder mint_bounded_sleep ctx-gate (CtxFitsBoundedSleepMint)
            return std::unexpected{std::error_code{errno, std::system_category()}};
        }
        const std::uint64_t total_nanos = static_cast<std::uint64_t>(deadline.tv_nsec) + nanos;
        deadline.tv_sec += static_cast<std::time_t>(total_nanos / nanos_per_second);
        deadline.tv_nsec = static_cast<long>(total_nanos % nanos_per_second);
        int failure = 0;
        do {
            failure = ::clock_nanosleep(
                CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline,
                nullptr);  // SYSCALL-CAP-OK: BoundedSleeper::sleep_for, sole builder mint_bounded_sleep ctx-gate (CtxFitsBoundedSleepMint)
        } while (failure == EINTR);
        if (failure != 0) [[unlikely]] {
            return std::unexpected{std::error_code{failure, std::system_category()}};
        }
        return {};
    }

private:
    // No byte route builds a sleeper, so a thread that owns no Block does
    // not get one.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr BoundedSleeper() noexcept {}

    template <std::uint64_t FriendMaxNanos, eff::IsExecCtx FriendCtx>
        requires CtxFitsBoundedSleepMint<FriendCtx, FriendMaxNanos>
    friend constexpr auto mint_bounded_sleep(FriendCtx const&) noexcept -> BoundedSleeper<FriendMaxNanos>;
};

template <ClockSource_v Source, eff::IsExecCtx Ctx>
    requires CtxFitsClockReaderMint<Ctx, Source>
[[nodiscard]] constexpr ClockReader<Source> mint_clock_reader(Ctx const&) noexcept {
    return ClockReader<Source>{};
}

// Gives a reader that owns the pin, or operation_not_permitted when the
// pin is not in force on the calling thread.  A pin that another thread
// earned, and a pin whose thread pinned again, are refused.
//
// §XXI carve-out: cx=alloc — the mint reads the pin event of the calling thread.
template <TscMode Mode, eff::IsExecCtx Ctx, typename PinT>
    requires CtxFitsTscReaderMint<Ctx, Mode, PinT>
[[nodiscard]] std::expected<TscReader<Mode, std::remove_cvref_t<PinT>>, std::error_code>
mint_tsc_reader(Ctx const&, PinT&& pin) noexcept {
    if (!pin.is_in_force()) [[unlikely]] {
        return std::unexpected{std::make_error_code(std::errc::operation_not_permitted)};
    }
    return TscReader<Mode, std::remove_cvref_t<PinT>>{std::forward<PinT>(pin)};
}

template <std::uint64_t MaxNanos, eff::IsExecCtx Ctx>
    requires CtxFitsBoundedSleepMint<Ctx, MaxNanos>
[[nodiscard]] constexpr BoundedSleeper<MaxNanos> mint_bounded_sleep(Ctx const&) noexcept {
    return BoundedSleeper<MaxNanos>{};
}

// Opens /dev/ptpN read-only through PtpDeviceDoor and returns its reader,
// or the errno of a failed open.
//
// §XXI carve-out: cx=alloc — opening /dev/ptpN invokes the kernel.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPtpClockReaderMint<Ctx>
[[nodiscard]] std::expected<PtpClockReader, std::error_code> mint_ptp_clock_reader(Ctx const&,
                                                                                   PtpDeviceIndex index) noexcept {
    auto fd = PtpDeviceDoor::open_(index);
    if (!fd) {
        return std::unexpected{std::error_code{fd.error(), std::system_category()}};
    }
    return PtpClockReader{std::move(*fd)};
}

}  // namespace fixy::time
