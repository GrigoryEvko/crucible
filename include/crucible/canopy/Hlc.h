#pragma once

// Hybrid Logical Clock (HLC) primitive.
//
// Implements Kulkarni-Demirbas-Madappa-Avva-Leone logical physical
// clocks for per-Cog event ordering.  The public wire timestamp keeps
// the canonical `(physical_ns, counter)` shape; provenance-sensitive
// boundaries use Tagged<HlcTimestamp, source::Hlc>.

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <algorithm>
#include <atomic>
#include <compare>
#include <cstdint>
#include <ctime>
#include <limits>
#include <optional>
#include <type_traits>

namespace crucible::canopy {

struct HlcTimestamp {
    std::uint64_t physical_ns = 0;
    std::uint32_t counter = 0;

    [[nodiscard]] friend constexpr auto
    operator<=>(HlcTimestamp const&, HlcTimestamp const&) = default;
};

using HlcClockTimestamp =
    safety::Tagged<HlcTimestamp, safety::source::Hlc>;
using ExternalHlcTimestamp =
    safety::Tagged<HlcTimestamp, safety::source::External>;

// Positive deltas are the true refinement boundary.  The stored HLC
// counter itself may be zero after any physical-time advance, per the
// HLC algorithm, so wrapping the field in Refined<positive> would be
// unsound.
using HlcCounterDelta =
    safety::Refined<safety::positive, std::uint32_t>;

static_assert(sizeof(HlcTimestamp) == 16);
static_assert(std::is_trivially_copyable_v<HlcTimestamp>);
static_assert(std::is_trivially_destructible_v<HlcTimestamp>);
static_assert(sizeof(HlcClockTimestamp) == sizeof(HlcTimestamp));
static_assert(std::is_trivially_copyable_v<HlcClockTimestamp>);
static_assert(std::is_trivially_destructible_v<HlcClockTimestamp>);
static_assert(sizeof(HlcCounterDelta) == sizeof(std::uint32_t));

namespace detail {

__extension__ using uint128_t = unsigned __int128;

[[nodiscard]] constexpr uint128_t
pack_hlc_timestamp(HlcTimestamp ts) noexcept {
    return (uint128_t{ts.physical_ns} << 64)
         | (uint128_t{ts.counter} << 32);
}

[[nodiscard]] constexpr HlcTimestamp
unpack_hlc_timestamp(uint128_t packed) noexcept {
    return HlcTimestamp{
        .physical_ns = static_cast<std::uint64_t>(packed >> 64),
        .counter = static_cast<std::uint32_t>((packed >> 32) & 0xffff'ffffu),
    };
}

class alignas(16) AtomicPackedHlcState {
public:
    constexpr AtomicPackedHlcState() noexcept = default;

    [[nodiscard]] uint128_t load() const noexcept {
#if defined(__x86_64__)
        std::uint64_t lo = 0;
        std::uint64_t hi = 0;
        const std::uint64_t desired_lo = 0;
        const std::uint64_t desired_hi = 0;
        __asm__ __volatile__(
            "lock cmpxchg16b %[cell]"
            : [cell] "+m"(cell_), "+a"(lo), "+d"(hi)
            : "b"(desired_lo), "c"(desired_hi)
            : "cc", "memory");
        return (uint128_t{hi} << 64) | uint128_t{lo};
#else
        return cell_.load(std::memory_order_acquire);
#endif
    }

    [[nodiscard]] bool compare_exchange(
        uint128_t& expected,
        uint128_t desired) noexcept {
#if defined(__x86_64__)
        std::uint64_t expected_lo = static_cast<std::uint64_t>(expected);
        std::uint64_t expected_hi = static_cast<std::uint64_t>(expected >> 64);
        const std::uint64_t desired_lo = static_cast<std::uint64_t>(desired);
        const std::uint64_t desired_hi = static_cast<std::uint64_t>(desired >> 64);
        unsigned char ok = 0;
        __asm__ __volatile__(
            "lock cmpxchg16b %[cell]\n\t"
            "setz %[ok]"
            : [cell] "+m"(cell_),
              "+a"(expected_lo),
              "+d"(expected_hi),
              [ok] "=q"(ok)
            : "b"(desired_lo), "c"(desired_hi)
            : "cc", "memory");
        expected = (uint128_t{expected_hi} << 64) | uint128_t{expected_lo};
        return ok != 0;
#else
        return cell_.compare_exchange_weak(
            expected,
            desired,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
#endif
    }

private:
#if defined(__x86_64__)
    mutable uint128_t cell_ = 0;
#else
    mutable std::atomic<uint128_t> cell_{0};
#endif
};

static_assert(alignof(AtomicPackedHlcState) >= 16);

}  // namespace detail

class alignas(64) Hlc : public safety::Pinned<Hlc> {
public:
    using timestamp_type = HlcTimestamp;
    using tagged_timestamp_type = HlcClockTimestamp;

    Hlc() noexcept = default;

    [[nodiscard]] HlcTimestamp now() noexcept {
        return update_local_(read_realtime_ns_());
    }

    [[nodiscard]] HlcTimestamp on_send() noexcept {
        return now();
    }

    void on_recv(HlcTimestamp peer_ts) noexcept {
        (void)update_recv_(read_realtime_ns_(), peer_ts);
    }

    void on_recv(HlcClockTimestamp peer_ts) noexcept {
        on_recv(peer_ts.value());
    }

    [[nodiscard]] HlcClockTimestamp tagged_now() noexcept {
        return HlcClockTimestamp{now()};
    }

    [[nodiscard]] HlcClockTimestamp tagged_on_send() noexcept {
        return HlcClockTimestamp{on_send()};
    }

    [[nodiscard]] HlcTimestamp peek() const noexcept {
        return detail::unpack_hlc_timestamp(state_.load());
    }

    [[nodiscard]] static constexpr HlcTimestamp local_event(
        HlcTimestamp old,
        std::uint64_t physical_now) noexcept {
        if (physical_now > old.physical_ns) {
            return HlcTimestamp{.physical_ns = physical_now, .counter = 0};
        }
        return bumped_(old.physical_ns, old.counter);
    }

    [[nodiscard]] static constexpr HlcTimestamp recv_event(
        HlcTimestamp old,
        HlcTimestamp peer,
        std::uint64_t physical_now) noexcept {
        const std::uint64_t l_new =
            std::max({old.physical_ns, peer.physical_ns, physical_now});

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
    [[nodiscard]] static constexpr HlcTimestamp bumped_(
        std::uint64_t physical_ns,
        std::uint32_t counter) noexcept {
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

    [[nodiscard]] static std::uint64_t read_realtime_ns_() noexcept {
        ::timespec ts{};
        if (::clock_gettime(CLOCK_REALTIME, &ts) != 0) [[unlikely]] {
            return std::uint64_t{1};
        }

        const std::uint64_t nsec =
            ts.tv_nsec > 0
                ? static_cast<std::uint64_t>(ts.tv_nsec)
                : std::uint64_t{0};
        if (ts.tv_sec <= 0) {
            return nsec == 0 ? std::uint64_t{1} : nsec;
        }

        constexpr std::uint64_t kNanosPerSecond = 1'000'000'000ULL;
        const std::uint64_t sec = static_cast<std::uint64_t>(ts.tv_sec);
        if (sec > (std::numeric_limits<std::uint64_t>::max() - nsec)
                    / kNanosPerSecond) [[unlikely]] {
            return std::numeric_limits<std::uint64_t>::max();
        }
        const std::uint64_t out = sec * kNanosPerSecond + nsec;
        return out == 0 ? std::uint64_t{1} : out;
    }

    [[nodiscard]] HlcTimestamp update_local_(std::uint64_t physical_now) noexcept {
        auto expected = state_.load();
        for (;;) {
            const HlcTimestamp old = detail::unpack_hlc_timestamp(expected);
            const HlcTimestamp desired = local_event(old, physical_now);
            auto expected_copy = expected;
            if (state_.compare_exchange(
                    expected_copy,
                    detail::pack_hlc_timestamp(desired))) {
                return desired;
            }
            expected = expected_copy;
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    [[nodiscard]] HlcTimestamp update_recv_(
        std::uint64_t physical_now,
        HlcTimestamp peer) noexcept {
        auto expected = state_.load();
        for (;;) {
            const HlcTimestamp old = detail::unpack_hlc_timestamp(expected);
            const HlcTimestamp desired = recv_event(old, peer, physical_now);
            auto expected_copy = expected;
            if (state_.compare_exchange(
                    expected_copy,
                    detail::pack_hlc_timestamp(desired))) {
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
static_assert(!std::is_copy_constructible_v<Hlc>);
static_assert(!std::is_move_constructible_v<Hlc>);

[[nodiscard]] inline Hlc mint_hlc(effects::Init) noexcept {
    return Hlc{};
}

template <std::size_t Capacity, typename UserTag>
using HlcTimestampChannel =
    concurrent::PermissionedSpscChannel<HlcTimestamp, Capacity, UserTag>;

template <typename ProducerHandle>
[[nodiscard]] bool try_push_hlc_timestamp(
    ProducerHandle& producer,
    HlcClockTimestamp timestamp) noexcept(noexcept(producer.try_push(timestamp.value()))) {
    return producer.try_push(timestamp.value());
}

template <typename ConsumerHandle>
[[nodiscard]] std::optional<HlcClockTimestamp>
try_pop_hlc_timestamp(ConsumerHandle& consumer) noexcept(noexcept(consumer.try_pop())) {
    auto ts = consumer.try_pop();
    if (!ts) {
        return std::nullopt;
    }
    return HlcClockTimestamp{*ts};
}

}  // namespace crucible::canopy
