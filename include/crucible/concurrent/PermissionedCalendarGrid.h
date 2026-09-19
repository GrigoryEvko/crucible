#pragma once

// A calendar-queue priority queue laid over a grid of SPSC rings: one
// row per producer, one column per priority bucket.  A producer writes
// only to its own row, so no per-item operation crosses threads.  A
// single consumer walks the bucket pointer forward, draining a bucket
// across every row before it advances.  The pointer only ever moves
// forward, and only the consumer writes it.
//
// The rejected alternative is one shared multi-producer ring.  Its
// per-item index move is a lock-prefixed atomic on a contended line,
// which is the cost this design exists to avoid.  What that costs in
// return: arrival order is lost, since the bucket order is the priority
// order.  The producer count is fixed at compile time.  Capacity is
// per-row, so a bursty producer cannot borrow an idle producer's space.
// For priority scheduling those are the right losses to take.
//
// A key extractor supplies the priority for an item, and a smaller key
// means higher priority.

#include <crucible/Platform.h>
#include <crucible/concurrent/SpscRing.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/safety/_Pinned.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// The slot tags come from the permission-grid generator with a consumer
// side of width one, since there is exactly one consumer.

namespace calendar_tag {

template <typename UserTag>
struct Whole {};

template <typename UserTag, std::size_t P>
using Producer = safety::Producer<Whole<UserTag>, P>;

template <typename UserTag>
using Consumer = safety::Consumer<Whole<UserTag>, 0>;

}  // namespace calendar_tag

template <typename E, typename T>
concept KeyExtractorOf = requires(const T& item) {
    { E::key(item) } noexcept -> std::same_as<std::uint64_t>;
};

template <SpscValue T, std::size_t NumProducers, std::size_t NumBuckets, std::size_t BucketCap, typename KeyExtractor,
          std::uint64_t QuantumNs, typename UserTag = void>
class PermissionedCalendarGrid
    : public safety::Pinned<
          PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>> {
    static_assert(NumProducers > 0, "PermissionedCalendarGrid: NumProducers must be > 0");
    static_assert(NumBuckets > 0, "PermissionedCalendarGrid: NumBuckets must be > 0");
    static_assert(BucketCap > 0, "PermissionedCalendarGrid: BucketCap must be > 0");
    static_assert(QuantumNs > 0, "PermissionedCalendarGrid: QuantumNs must be > 0");
    static_assert(KeyExtractorOf<KeyExtractor, T>, "KeyExtractor must provide static "
                                                   "uint64_t key(const T&) noexcept");

public:
    using value_type = T;
    using user_tag = UserTag;
    using whole_tag = calendar_tag::Whole<UserTag>;
    using consumer_tag = calendar_tag::Consumer<UserTag>;
    using key_extractor = KeyExtractor;

    template <std::size_t P>
    using producer_tag = calendar_tag::Producer<UserTag, P>;

    static constexpr std::size_t num_producers = NumProducers;
    static constexpr std::size_t num_buckets = NumBuckets;
    static constexpr std::size_t bucket_cap = BucketCap;
    static constexpr std::uint64_t quantum_ns = QuantumNs;

    constexpr PermissionedCalendarGrid() noexcept = default;

    template <std::size_t P>
    class ProducerHandle {
        static_assert(P < NumProducers, "ProducerHandle<P>: P must be < NumProducers");

        PermissionedCalendarGrid& grid_;
        [[no_unique_address]] safety::Permission<producer_tag<P>> perm_;

        constexpr ProducerHandle(PermissionedCalendarGrid& g, safety::Permission<producer_tag<P>>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedCalendarGrid;

    public:
        ProducerHandle(const ProducerHandle&) = delete("ProducerHandle owns the row's Producer Permission — "
                                                       "copy would duplicate the linear token");
        ProducerHandle& operator=(const ProducerHandle&) = delete("ProducerHandle owns the row's Producer Permission — "
                                                                  "assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&) = delete("ProducerHandle binds to ONE row for life — "
                                                             "the row index is part of the type");

        static constexpr std::size_t row_index = P;

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            const std::size_t b = grid_.bucket_for_(item);
            return grid_.rings_[P][b].try_push(item);
        }

        // Only a run of items sharing a bucket becomes one batched
        // write, so a caller that hands over items already grouped by
        // bucket gets the whole benefit.
        [[nodiscard, gnu::hot]] std::size_t try_push_batch(std::span<const T> items) noexcept {
            std::size_t total = 0;
            std::size_t i = 0;
            while (i < items.size()) {
                const std::size_t bucket = grid_.bucket_for_(items[i]);
                std::size_t run_end = i + 1;
                while (run_end < items.size() && grid_.bucket_for_(items[run_end]) == bucket) {
                    ++run_end;
                }
                const std::size_t want = run_end - i;
                const std::size_t pushed = grid_.rings_[P][bucket].try_push_batch(items.subspan(i, want));
                total += pushed;
                if (pushed < want) break;
                i = run_end;
            }
            return total;
        }

        // Snapshots over this handle's row.  Sound for telemetry and
        // for deciding whether to keep retrying, never for a
        // correctness invariant.
        [[nodiscard]] std::size_t size_approx() const noexcept {
            std::size_t total = 0;
            for (std::size_t b = 0; b < NumBuckets; ++b) {
                total += grid_.rings_[P][b].size_approx();
            }
            return total;
        }
        [[nodiscard]] bool empty_approx() const noexcept {
            for (std::size_t b = 0; b < NumBuckets; ++b) {
                if (!grid_.rings_[P][b].empty_approx()) return false;
            }
            return true;
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return NumBuckets * BucketCap; }
    };

    class ConsumerHandle {
        PermissionedCalendarGrid& grid_;
        [[no_unique_address]] safety::Permission<consumer_tag> perm_;

        constexpr ConsumerHandle(PermissionedCalendarGrid& g, safety::Permission<consumer_tag>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedCalendarGrid;

    public:
        ConsumerHandle(const ConsumerHandle&) = delete("ConsumerHandle owns the unique Consumer Permission "
                                                       "— copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&) = delete("ConsumerHandle owns the unique Consumer Permission "
                                                                  "— assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&) = delete("ConsumerHandle binds to ONE consumer for life");

        // Returns the item of highest priority still queued, which is
        // the one in the earliest non-empty bucket.
        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            // The origin is sampled once.  Re-reading it inside the
            // loop would slide the scan window forward as the loop
            // advances the pointer, and the scan would never terminate
            // over a fixed set of buckets.  The running sum is an
            // absolute bucket counter, and the modulo maps it onto a
            // physical column.
            const std::uint64_t cur_origin = grid_.current_bucket_.peek_relaxed();
            for (std::size_t scan = 0; scan < NumBuckets; ++scan) {
                const std::uint64_t this_b = cur_origin + scan;
                const std::size_t bucket = this_b % NumBuckets;
                for (std::size_t p = 0; p < NumProducers; ++p) {
                    if (auto v = grid_.rings_[p][bucket].try_pop()) {
                        return v;
                    }
                }
                grid_.advance_past_(this_b);
            }
            return std::nullopt;
        }

        [[nodiscard, gnu::hot]] std::size_t try_pop_batch(std::span<T> out) noexcept {
            if (out.empty()) return 0;
            std::size_t total = 0;
            // Sampled once, for the reason given in try_pop.
            const std::uint64_t cur_origin = grid_.current_bucket_.peek_relaxed();
            for (std::size_t scan = 0; scan < NumBuckets && total < out.size(); ++scan) {
                const std::uint64_t this_b = cur_origin + scan;
                const std::size_t bucket = this_b % NumBuckets;
                std::size_t bucket_got = 0;
                for (std::size_t p = 0; p < NumProducers && total < out.size(); ++p) {
                    const std::size_t got = grid_.rings_[p][bucket].try_pop_batch(out.subspan(total));
                    total += got;
                    bucket_got += got;
                }
                if (bucket_got == 0) {
                    grid_.advance_past_(this_b);
                    continue;
                }
                // A bucket that yielded anything is left as the current
                // one, because a producer may add to it again before
                // the next call.
                return total;
            }
            return total;
        }

        // The consumer sees every cell, since it drains all of them.
        [[nodiscard]] bool empty_approx() const noexcept { return grid_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return grid_.size_approx(); }
        [[nodiscard]] std::uint64_t current_bucket() const noexcept { return grid_.current_bucket_.peek_relaxed(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return NumProducers * NumBuckets * BucketCap; }
    };

    template <std::size_t P>
    [[nodiscard]] constexpr ProducerHandle<P> producer(safety::Permission<producer_tag<P>>&& perm) noexcept {
        static_assert(P < NumProducers, "producer<P>: P must be < NumProducers");
        return ProducerHandle<P>{*this, std::move(perm)};
    }

    [[nodiscard]] constexpr ConsumerHandle consumer(safety::Permission<consumer_tag>&& perm) noexcept {
        return ConsumerHandle{*this, std::move(perm)};
    }

    // Scoped exclusive access to the whole grid.  Every endpoint holds
    // a linear token and there is no refcount to drain, so surrendering
    // the recombined whole permission is itself the proof that no
    // handle is alive.  It comes back so the caller can split it again.
    template <typename Body>
        requires std::is_invocable_v<Body>
    [[nodiscard]] safety::Permission<whole_tag>
    with_recombined_access(safety::Permission<whole_tag>&& whole,
                           Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        std::forward<Body>(body)();
        return std::move(whole);
    }

    // Always false, and present only so this grid matches the shape of
    // the pool-backed channels.  There is no exclusivity flag to read:
    // the linear permissions on every endpoint are what prove single
    // ownership.
    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept { return false; }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return NumProducers * NumBuckets * BucketCap; }

    // Snapshots.  Sound for telemetry and for deciding whether to keep
    // retrying, never for a correctness invariant.
    [[nodiscard]] bool empty_approx() const noexcept {
        for (std::size_t p = 0; p < NumProducers; ++p) {
            for (std::size_t b = 0; b < NumBuckets; ++b) {
                if (!rings_[p][b].empty_approx()) return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        std::size_t total = 0;
        for (std::size_t p = 0; p < NumProducers; ++p) {
            for (std::size_t b = 0; b < NumBuckets; ++b) {
                total += rings_[p][b].size_approx();
            }
        }
        return total;
    }

    [[nodiscard]] std::uint64_t current_bucket() const noexcept { return current_bucket_.peek_relaxed(); }

private:
    // Clamping to the current bucket is what makes a late item run at
    // once instead of waiting a full turn of the wheel.  An item due
    // later lands ahead of the pointer and waits for it.
    //
    // The wrap is deliberate but not free.  An item more than
    // NumBuckets quanta ahead of the pointer wraps onto a bucket the
    // consumer is about to reach, and mixes with items of a quite
    // different priority in arrival order.  The caller keeps NumBuckets
    // above the priority spread it expects, in units of the quantum.
    [[nodiscard, gnu::pure]] std::size_t bucket_for_(const T& item) const noexcept {
        const std::uint64_t k = KeyExtractor::key(item);
        const std::uint64_t target = k / QuantumNs;
        const std::uint64_t cur = current_bucket_.peek_relaxed();
        const std::uint64_t clamped = (target > cur) ? target : cur;
        return clamped % NumBuckets;
    }

    // Only the consumer calls this, so the exchange is not there to
    // arbitrate between threads.  It covers the case of that same
    // thread re-entering through a nested pop, where a plain store
    // would move the pointer backwards.
    void advance_past_(std::uint64_t value) noexcept {
        std::uint64_t expected = value;
        (void)current_bucket_.compare_exchange_advance_weak(expected, value + 1, std::memory_order_acq_rel,
                                                            std::memory_order_acquire);
    }

    // Row-major, so a producer's buckets are contiguous and its whole
    // working set is one row.  The consumer then scans across rows,
    // which is the less cache-friendly of the two orderings, but the
    // row is exactly the region one producer permission covers, and
    // keeping those aligned is worth the scan.
    std::array<std::array<SpscRing<T, BucketCap>, NumBuckets>, NumProducers> rings_{};

    // Only the consumer writes this.  Producers read it to place an
    // item, and never move it.
    safety::AtomicMonotonic<std::uint64_t> current_bucket_{0};
};

}  // namespace crucible::concurrent

// No split specialization appears here.  The permission-grid generator
// already expands a whole tag into the producer and consumer slots this
// grid uses.
