#pragma once

#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <algorithm>
#include <atomic>
#include <compare>
#include <cstdint>
#include <ctime>
#include <limits>
#include <type_traits>

namespace crucible::canopy {

// The counter is not a positive refinement.  The HLC algorithm sets it to
// zero after each advance of the physical time.
struct HlcTimestamp {
    std::uint64_t physical_ns = 0;
    std::uint32_t counter = 0;

    [[nodiscard]] friend constexpr auto operator<=>(HlcTimestamp const&, HlcTimestamp const&) = default;
};

using HlcClockTimestamp = ::fixy::Tagged<HlcTimestamp, ::fixy::tags::source::Hlc>;
using ExternalHlcTimestamp = ::fixy::Tagged<HlcTimestamp, ::fixy::tags::source::External>;

static_assert(sizeof(HlcTimestamp) == 16);
static_assert(std::is_trivially_copyable_v<HlcTimestamp>);
static_assert(std::is_trivially_destructible_v<HlcTimestamp>);
static_assert(sizeof(HlcClockTimestamp) == sizeof(HlcTimestamp));
static_assert(std::is_trivially_copyable_v<HlcClockTimestamp>);
static_assert(std::is_trivially_destructible_v<HlcClockTimestamp>);

namespace detail {

__extension__ using uint128_t = unsigned __int128;

[[nodiscard]] constexpr uint128_t pack_hlc_timestamp(HlcTimestamp ts) noexcept {
    return (uint128_t{ts.physical_ns} << 64) | (uint128_t{ts.counter} << 32);
}

[[nodiscard]] constexpr HlcTimestamp unpack_hlc_timestamp(uint128_t packed) noexcept {
    return HlcTimestamp{
        .physical_ns = static_cast<std::uint64_t>(packed >> 64),
        .counter = static_cast<std::uint32_t>((packed >> 32) & 0xffffffffu),
    };
}

// x86_64 has no atomic 128-bit load instruction.  The portable idiom is a
// `lock cmpxchg16b` whose expected and desired values are both zero.  With a
// zero cell the compare succeeds and writes zero back, which leaves the cell
// as it was, and the result is zero.  With any other cell value the compare
// fails, the hardware loads the cell into RDX:RAX and writes nothing, and the
// result is the loaded value.  The `lock` prefix is mandatory for both the
// atomicity and the acquire fence.
//
// Inlining the asm rather than calling the library 128-bit load keeps the
// operation lock-free even where the toolchain links a stub libatomic.  The
// idiom depends on the cell starting at zero, so nothing may store into it
// outside compare_exchange.
class alignas(16) AtomicPackedHlcState {
public:
    constexpr AtomicPackedHlcState() noexcept = default;

    [[nodiscard]] uint128_t load() const noexcept {
#if defined(__x86_64__)
        std::uint64_t lo = 0;
        std::uint64_t hi = 0;
        const std::uint64_t desired_lo = 0;
        const std::uint64_t desired_hi = 0;
        __asm__ __volatile__("lock cmpxchg16b %[cell]"
                             : [cell] "+m"(cell_), "+a"(lo), "+d"(hi)
                             : "b"(desired_lo), "c"(desired_hi)
                             : "cc", "memory");
        return (uint128_t{hi} << 64) | uint128_t{lo};
#else
        return cell_.load();
#endif
    }

    [[nodiscard]] bool compare_exchange(uint128_t& expected, uint128_t desired) noexcept {
#if defined(__x86_64__)
        std::uint64_t expected_lo = static_cast<std::uint64_t>(expected);
        std::uint64_t expected_hi = static_cast<std::uint64_t>(expected >> 64);
        const std::uint64_t desired_lo = static_cast<std::uint64_t>(desired);
        const std::uint64_t desired_hi = static_cast<std::uint64_t>(desired >> 64);
        unsigned char ok = 0;
        __asm__ __volatile__("lock cmpxchg16b %[cell]\n\t"
                             "setz %[ok]"
                             : [cell] "+m"(cell_), "+a"(expected_lo), "+d"(expected_hi), [ok] "=q"(ok)
                             : "b"(desired_lo), "c"(desired_hi)
                             : "cc", "memory");
        expected = (uint128_t{expected_hi} << 64) | uint128_t{expected_lo};
        return ok != 0;
#else
        return cell_.compare_exchange(expected, desired);
#endif
    }

private:
#if defined(__x86_64__)
    mutable uint128_t cell_ = 0;
#else
    // Where the ISA provides a lock-free 128-bit atomic, use it directly and
    // now() stays lock-free.  Where it does not, a 16-byte atomic lowers to a
    // hidden library mutex, so the fallback is a plain cell guarded by an
    // explicit test-and-set spinlock.  Making the mutual exclusion explicit
    // keeps its cost visible instead of buried in the standard library, and
    // the fallback contends only for the brief clock update.  The spinlock
    // imposes a total order and touches no floating point, so determinism
    // holds.
    struct LockFreeCell {
        mutable std::atomic<uint128_t> value_{
            0};  // LOCK-FREE-OK: selected only where the atomic is lock-free.  SpinlockCell covers every other target, so a static_assert here would fire wrongly
        [[nodiscard]] uint128_t load() const noexcept { return value_.load(std::memory_order_acquire); }
        [[nodiscard]] bool compare_exchange(uint128_t& expected, uint128_t desired) noexcept {
            return value_.compare_exchange_weak(expected, desired, std::memory_order_acq_rel,
                                                std::memory_order_acquire);
        }
    };
    struct SpinlockCell {
        mutable uint128_t value_ = 0;
        mutable std::atomic_flag lock_{};
        void acquire() const noexcept {
            while (lock_.test_and_set(std::memory_order_acquire)) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        void release() const noexcept { lock_.clear(std::memory_order_release); }
        [[nodiscard]] uint128_t load() const noexcept {
            acquire();
            const uint128_t snapshot = value_;
            release();
            return snapshot;
        }
        [[nodiscard]] bool compare_exchange(uint128_t& expected, uint128_t desired) noexcept {
            acquire();
            const bool matched = (value_ == expected);
            if (matched) {
                value_ = desired;
            } else {
                expected = value_;
            }
            release();
            return matched;
        }
    };
    using Cell = std::conditional_t<
        std::atomic<uint128_t>::
            is_always_lock_free,  // LOCK-FREE-OK: a dispatch predicate that picks the cell type, not an atomic field
        LockFreeCell, SpinlockCell>;
    mutable Cell cell_{};
#endif
};

static_assert(alignof(AtomicPackedHlcState) >= 16);

}  // namespace detail

class Hlc;

[[nodiscard]] constexpr Hlc mint_hlc(::foundation::effects::Init) noexcept;

// A hybrid logical clock.  The constructor is private and mint_hlc is its
// only friend, so each clock comes from a context that owns Init.  That
// context also owns the capability to read the wall clock, which now() and
// on_recv() do.
class alignas(64) Hlc : public ::foundation::Pinned<Hlc> {
public:
    using timestamp_type = HlcTimestamp;
    using tagged_timestamp_type = HlcClockTimestamp;

    [[nodiscard]] HlcTimestamp now() noexcept { return update_local_(read_realtime_ns_()); }

    [[nodiscard]] HlcTimestamp on_send() noexcept { return now(); }

    void on_recv(HlcTimestamp peer_ts) noexcept { (void)update_recv_(read_realtime_ns_(), peer_ts); }

    void on_recv(HlcClockTimestamp peer_ts) noexcept { on_recv(peer_ts.value()); }

    [[nodiscard]] HlcClockTimestamp tagged_on_send() noexcept {
        return ::fixy::mint_tagged<::fixy::tags::source::Hlc>(on_send());
    }

    [[nodiscard]] HlcTimestamp peek() const noexcept { return detail::unpack_hlc_timestamp(state_.load()); }

    [[nodiscard]] static constexpr HlcTimestamp local_event(HlcTimestamp old, std::uint64_t physical_now) noexcept {
        if (physical_now > old.physical_ns) {
            return HlcTimestamp{.physical_ns = physical_now, .counter = 0};
        }
        return bumped_(old.physical_ns, old.counter);
    }

    [[nodiscard]] static constexpr HlcTimestamp recv_event(HlcTimestamp old, HlcTimestamp peer,
                                                           std::uint64_t physical_now) noexcept {
        const std::uint64_t l_new = std::max({old.physical_ns, peer.physical_ns, physical_now});

        if (l_new == old.physical_ns && l_new == peer.physical_ns) {
            return bumped_(l_new, std::max(old.counter, peer.counter));
        }
        if (l_new == old.physical_ns) {
            return bumped_(l_new, old.counter);
        }
        if (l_new == peer.physical_ns) {
            return bumped_(l_new, peer.counter);
        }
        return HlcTimestamp{.physical_ns = l_new, .counter = 0};
    }

private:
    constexpr Hlc() noexcept = default;

    friend constexpr Hlc mint_hlc(::foundation::effects::Init) noexcept;

    [[nodiscard]] static constexpr HlcTimestamp bumped_(std::uint64_t physical_ns, std::uint32_t counter) noexcept {
        if (counter != std::numeric_limits<std::uint32_t>::max()) {
            return HlcTimestamp{
                .physical_ns = physical_ns,
                .counter = counter + 1u,
            };
        }
        if (physical_ns != std::numeric_limits<std::uint64_t>::max()) {
            return HlcTimestamp{.physical_ns = physical_ns + 1u, .counter = 0};
        }
        return HlcTimestamp{.physical_ns = physical_ns, .counter = counter};
    }

    // The wall clock can jump backwards under a time adjustment or a slew.
    // The HLC update takes the maximum of this value and the stored time, so
    // a step back advances the counter and does not move the clock back.  A
    // failed read, or a time before 1970, gives a value below one second, so
    // it cannot move a clock that holds a real time.  A time too large for 64
    // bits of nanoseconds gives the maximum.
    [[nodiscard]] static std::uint64_t read_realtime_ns_() noexcept {
        ::timespec reading{};
        const int status = ::clock_gettime(
            CLOCK_REALTIME,
            &reading);  // SYSCALL-CAP-OK: Hlc::read_realtime_ns_, sole builder mint_hlc takes the init context
        if (status != 0) [[unlikely]] {
            return std::uint64_t{1};
        }

        const std::uint64_t nanos =
            reading.tv_nsec > 0 ? static_cast<std::uint64_t>(reading.tv_nsec) : std::uint64_t{0};
        if (reading.tv_sec <= 0) {
            return nanos == 0 ? std::uint64_t{1} : nanos;
        }

        constexpr std::uint64_t nanos_per_second = 1000000000ULL;
        const std::uint64_t seconds = static_cast<std::uint64_t>(reading.tv_sec);
        if (seconds > (std::numeric_limits<std::uint64_t>::max() - nanos) / nanos_per_second) [[unlikely]] {
            return std::numeric_limits<std::uint64_t>::max();
        }
        const std::uint64_t total_ns = seconds * nanos_per_second + nanos;
        return total_ns == 0 ? std::uint64_t{1} : total_ns;
    }

    [[nodiscard]] HlcTimestamp update_local_(std::uint64_t physical_now) noexcept {
        auto expected = state_.load();
        for (;;) {
            const HlcTimestamp old = detail::unpack_hlc_timestamp(expected);
            const HlcTimestamp desired = local_event(old, physical_now);
            auto expected_copy = expected;
            if (state_.compare_exchange(expected_copy, detail::pack_hlc_timestamp(desired))) {
                return desired;
            }
            expected = expected_copy;
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    [[nodiscard]] HlcTimestamp update_recv_(std::uint64_t physical_now, HlcTimestamp peer) noexcept {
        auto expected = state_.load();
        for (;;) {
            const HlcTimestamp old = detail::unpack_hlc_timestamp(expected);
            const HlcTimestamp desired = recv_event(old, peer, physical_now);
            auto expected_copy = expected;
            if (state_.compare_exchange(expected_copy, detail::pack_hlc_timestamp(desired))) {
                return desired;
            }
            expected = expected_copy;
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    alignas(64) detail::AtomicPackedHlcState state_{};
};

static_assert(alignof(Hlc) == 64);
static_assert(sizeof(Hlc) == 64);
static_assert(!std::is_default_constructible_v<Hlc>);
static_assert(!std::is_copy_constructible_v<Hlc>);
static_assert(!std::is_move_constructible_v<Hlc>);

[[nodiscard]] constexpr Hlc mint_hlc(::foundation::effects::Init) noexcept { return Hlc{}; }

}  // namespace crucible::canopy
