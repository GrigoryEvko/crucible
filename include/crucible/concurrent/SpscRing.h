#pragma once

// One producer thread is the sole writer of head_ and of the slots in
// [tail, head). One consumer thread is the sole writer of tail_ and the sole
// reader of those slots. Nothing enforces this at run time. A second producer
// or a second consumer corrupts the ring silently.

#include <crucible/Platform.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

namespace crucible::concurrent {

template <typename T>
concept SpscValue = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;

template <SpscValue T, std::size_t Capacity>
class SpscRing : public safety::Pinned<SpscRing<T, Capacity>> {
public:
    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;

    static_assert(std::has_single_bit(Capacity), "Capacity must be a power of two");
    static_assert(Capacity > 0, "Capacity must be greater than zero");

private:
    static constexpr std::uint64_t MASK = std::uint64_t{Capacity - 1};

public:
    SpscRing() noexcept = default;

    // The slot at (h & MASK) is free once h - t < Capacity holds: the consumer
    // last read that slot at tail position h - Capacity and has moved past it.
    // So (h & MASK) and (t & MASK) never name the same slot while both are in
    // flight, and the non-atomic cell write races with nothing.
    [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
        const std::uint64_t h = head_.peek_relaxed();
        const std::uint64_t t = tail_.get();
        if (h - t >= Capacity) [[unlikely]] {
            return false;
        }
        buffer_[h & MASK] = item;
        head_.advance(h + 1);
        return true;
    }

    [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
        const std::uint64_t t = tail_.peek_relaxed();
        const std::uint64_t h = head_.get();
        if (h == t) [[unlikely]] {
            return std::nullopt;
        }
        const T result = buffer_[t & MASK];
        tail_.advance(t + 1);
        return result;
    }

    // The bulk copies go through std::memcpy rather than a std::simd store
    // loop. The compiler lowers a bounded contiguous memcpy to the widest
    // store the target ISA offers, so the explicit vector form buys nothing
    // and adds a header dependency.
    [[nodiscard, gnu::hot]] std::size_t try_push_batch(std::span<const T> items) noexcept {
        if (items.empty()) [[unlikely]]
            return 0;
        const std::uint64_t h = head_.peek_relaxed();
        const std::uint64_t t = tail_.get();
        const std::uint64_t free_slots = Capacity - (h - t);
        if (free_slots == 0) [[unlikely]]
            return 0;
        const std::size_t count = std::min<std::size_t>(items.size(), free_slots);

        const std::uint64_t start_pos = h & MASK;
        const std::size_t first = std::min<std::size_t>(count, Capacity - start_pos);
        const std::size_t second = count - first;

        std::memcpy(buffer_.data() + start_pos, items.data(), first * sizeof(T));
        if (second > 0) [[unlikely]] {
            std::memcpy(buffer_.data(), items.data() + first, second * sizeof(T));
        }

        head_.advance(h + count);
        return count;
    }

    [[nodiscard, gnu::hot]] std::size_t try_pop_batch(std::span<T> out) noexcept {
        if (out.empty()) [[unlikely]]
            return 0;
        const std::uint64_t t = tail_.peek_relaxed();
        const std::uint64_t h = head_.get();
        const std::uint64_t available = h - t;
        if (available == 0) [[unlikely]]
            return 0;
        const std::size_t count = std::min<std::size_t>(out.size(), available);

        const std::uint64_t start_pos = t & MASK;
        const std::size_t first = std::min<std::size_t>(count, Capacity - start_pos);
        const std::size_t second = count - first;

        std::memcpy(out.data(), buffer_.data() + start_pos, first * sizeof(T));
        if (second > 0) [[unlikely]] {
            std::memcpy(out.data() + first, buffer_.data(), second * sizeof(T));
        }

        tail_.advance(t + count);
        return count;
    }

    [[nodiscard]] bool empty_approx() const noexcept { return head_.get() == tail_.get(); }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        // size_t and uint64_t are the same type on every supported platform,
        // so the return converts exactly. An explicit cast would trip
        // -Werror=useless-cast.
        const std::uint64_t h = head_.get();
        const std::uint64_t t = tail_.get();
        return h - t;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    // head_ and tail_ occupy separate cache lines. On one shared line every
    // push would invalidate the consumer's copy and every pop the producer's.
    //
    // Each side reads its own index with peek_relaxed: that index is written
    // by this thread alone, so no ordering is needed to observe the latest
    // value. The other side's index is read with get, which acquires, and
    // published with advance, which releases. That pair is what carries the
    // cell contents across.

    alignas(64) safety::AtomicMonotonic<std::uint64_t> head_{0};
    alignas(64) safety::AtomicMonotonic<std::uint64_t> tail_{0};

    alignas(64) std::array<T, Capacity> buffer_{};
};

}  // namespace crucible::concurrent
