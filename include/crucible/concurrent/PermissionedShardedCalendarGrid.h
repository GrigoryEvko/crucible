#pragma once

// A ladder of independent calendar queues, one per shard.  A producer
// writes only to its own shard and a consumer drains only that same
// shard, and each shard carries its own bucket pointer.
//
// The rejected alternative is a single calendar with one bucket
// pointer.  Every producer there reads the one pointer that the one
// consumer keeps writing, so the line migrates on each push and the
// producer inherits the consumer's scheduling jitter.  Splitting the
// pointer per shard removes that read: when a shard's producer and
// consumer sit on the same core or a neighbouring one, the read is
// local.
//
// What that costs is global ordering.  Within a shard the bucket order
// is exact, so the item with the lowest key pops first.  Across shards
// the pointers move independently and nothing relates them, so two
// shards can be draining quite different priorities at the same
// moment.  A workload that needs one global order wants the
// single-calendar form instead.
//
// There is no stealing between shards.  A shard whose consumer falls
// behind its producer backs up while its siblings sit idle, so the
// producers have to be spread over the shards evenly.

#include <crucible/concurrent/SpscRing.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/safety/_Pinned.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// A smaller key means higher priority.

template <typename K, typename T>
concept ShardedCalendarKeyExtractorOf = requires(const T& v) {
    { K::key(v) } noexcept -> std::convertible_to<std::uint64_t>;
};

// The slot tags come straight from the permission-grid generator, which
// already knows how to split a whole tag into producer and consumer
// slots.

namespace sharded_calendar_tag {

template <typename UserTag>
struct Whole {};

template <typename UserTag, std::size_t S>
using Producer = safety::Producer<Whole<UserTag>, S>;

template <typename UserTag, std::size_t S>
using Consumer = safety::Consumer<Whole<UserTag>, S>;

}  // namespace sharded_calendar_tag

template <SpscValue T, std::size_t NumShards, std::size_t NumBuckets, std::size_t BucketCap, typename KeyExtractor,
          std::uint64_t QuantumNs, typename UserTag = void>
class PermissionedShardedCalendarGrid
    : public safety::Pinned<
          PermissionedShardedCalendarGrid<T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>> {
    static_assert(NumShards > 0, "NumShards must be > 0");
    static_assert(NumBuckets > 0, "NumBuckets must be > 0");
    static_assert(BucketCap > 0, "BucketCap must be > 0");
    static_assert(QuantumNs > 0, "QuantumNs must be > 0");
    static_assert(ShardedCalendarKeyExtractorOf<KeyExtractor, T>, "KeyExtractor must provide static "
                                                                  "uint64_t key(const T&) noexcept");

public:
    using value_type = T;
    using user_tag = UserTag;
    using whole_tag = sharded_calendar_tag::Whole<UserTag>;
    using key_extractor = KeyExtractor;

    template <std::size_t S>
    using shard_producer_tag = sharded_calendar_tag::Producer<UserTag, S>;
    template <std::size_t S>
    using shard_consumer_tag = sharded_calendar_tag::Consumer<UserTag, S>;

    static constexpr std::size_t num_shards = NumShards;
    static constexpr std::size_t num_buckets = NumBuckets;
    static constexpr std::size_t bucket_cap = BucketCap;
    static constexpr std::uint64_t quantum_ns = QuantumNs;

    // A shard holds every bucket's ring, so it is far too large to sit
    // inline in an automatic object.

private:
    struct Shard {
        std::array<SpscRing<T, BucketCap>, NumBuckets> buckets;
        safety::AtomicMonotonic<std::uint64_t, std::less<std::uint64_t>> current_bucket{0};
    };

    std::array<std::unique_ptr<Shard>, NumShards> shards_;

    // Clamping to the shard's pointer is what makes a late item run at
    // once instead of waiting a full turn of the wheel.
    [[nodiscard, gnu::hot]] std::size_t bucket_for_(std::size_t shard, const T& item) const noexcept {
        const std::uint64_t key = KeyExtractor::key(item);
        const std::uint64_t key_bucket = key / QuantumNs;
        const std::uint64_t cur = shards_[shard]->current_bucket.peek_relaxed();
        const std::uint64_t b = key_bucket > cur ? key_bucket : cur;
        return b % NumBuckets;
    }

public:
    PermissionedShardedCalendarGrid() {
        for (std::size_t s = 0; s < NumShards; ++s) {
            shards_[s] = std::make_unique<Shard>();
        }
    }

    template <std::size_t S>
    class ProducerHandle {
        static_assert(S < NumShards, "ProducerHandle<S>: S must be < NumShards");

        PermissionedShardedCalendarGrid& grid_;
        [[no_unique_address]] safety::Permission<shard_producer_tag<S>> perm_;

        constexpr ProducerHandle(PermissionedShardedCalendarGrid& g,
                                 safety::Permission<shard_producer_tag<S>>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedShardedCalendarGrid;

    public:
        ProducerHandle(const ProducerHandle&) = delete("ProducerHandle owns the shard's Producer Permission — "
                                                       "copy would duplicate the linear token");
        ProducerHandle&
        operator=(const ProducerHandle&) = delete("ProducerHandle owns the shard's Producer Permission — "
                                                  "assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&) = delete("ProducerHandle binds to ONE shard for life — "
                                                             "the shard index is part of the type");

        static constexpr std::size_t shard_index = S;

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            const std::size_t b = grid_.bucket_for_(S, item);
            return grid_.shards_[S]->buckets[b].try_push(item);
        }

        // Snapshots over this handle's shard.  Sound for telemetry and
        // for deciding whether to keep retrying, never for a
        // correctness invariant.
        [[nodiscard]] std::size_t size_approx() const noexcept { return grid_.size_approx(S); }
        [[nodiscard]] bool empty_approx() const noexcept { return grid_.size_approx(S) == 0; }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return NumBuckets * BucketCap; }
    };

    template <std::size_t S>
    class ConsumerHandle {
        static_assert(S < NumShards, "ConsumerHandle<S>: S must be < NumShards");

        PermissionedShardedCalendarGrid& grid_;
        [[no_unique_address]] safety::Permission<shard_consumer_tag<S>> perm_;

        constexpr ConsumerHandle(PermissionedShardedCalendarGrid& g,
                                 safety::Permission<shard_consumer_tag<S>>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedShardedCalendarGrid;

    public:
        ConsumerHandle(const ConsumerHandle&) = delete("ConsumerHandle owns the shard's Consumer Permission");
        ConsumerHandle&
        operator=(const ConsumerHandle&) = delete("ConsumerHandle owns the shard's Consumer Permission");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&) = delete("ConsumerHandle binds to ONE shard for life");

        static constexpr std::size_t shard_index = S;

        // Returns the item of highest priority still queued in this
        // shard.  The origin is sampled once, so the advance below
        // cannot slide the scan window forward under the loop.
        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            auto& shard = *grid_.shards_[S];
            const std::uint64_t cur_origin = shard.current_bucket.peek_relaxed();
            for (std::size_t scan = 0; scan < NumBuckets; ++scan) {
                const std::uint64_t this_b = cur_origin + scan;
                const std::size_t cell = this_b % NumBuckets;
                if (auto v = shard.buckets[cell].try_pop()) {
                    if (scan > 0) {
                        // The pointer only ever moves forward, so a
                        // refused advance means it already stands at or
                        // past this bucket.  Either outcome leaves it
                        // where this call needs it, and the result is
                        // discarded for that reason.
                        (void)shard.current_bucket.try_advance(this_b);
                    }
                    return v;
                }
            }
            return std::nullopt;
        }

        // Snapshots over this handle's shard.  Sound for telemetry and
        // for deciding whether to keep retrying, never for a
        // correctness invariant.
        [[nodiscard]] std::size_t size_approx() const noexcept { return grid_.size_approx(S); }
        [[nodiscard]] bool empty_approx() const noexcept { return grid_.size_approx(S) == 0; }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return NumBuckets * BucketCap; }
    };

    template <std::size_t S>
    [[nodiscard]] constexpr ProducerHandle<S> producer(safety::Permission<shard_producer_tag<S>>&& perm) noexcept {
        static_assert(S < NumShards, "producer<S>: S must be < NumShards");
        return ProducerHandle<S>{*this, std::move(perm)};
    }

    template <std::size_t S>
    [[nodiscard]] constexpr ConsumerHandle<S> consumer(safety::Permission<shard_consumer_tag<S>>&& perm) noexcept {
        static_assert(S < NumShards, "consumer<S>: S must be < NumShards");
        return ConsumerHandle<S>{*this, std::move(perm)};
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return NumShards * NumBuckets * BucketCap; }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        std::size_t total = 0;
        for (std::size_t s = 0; s < NumShards; ++s) {
            for (std::size_t b = 0; b < NumBuckets; ++b) {
                total += shards_[s]->buckets[b].size_approx();
            }
        }
        return total;
    }

    [[nodiscard]] std::size_t size_approx(std::size_t shard) const noexcept {
        std::size_t total = 0;
        for (std::size_t b = 0; b < NumBuckets; ++b) {
            total += shards_[shard]->buckets[b].size_approx();
        }
        return total;
    }

    [[nodiscard]] bool empty_approx() const noexcept { return size_approx() == 0; }

    // Always false, and present only so this ladder matches the shape
    // of the pool-backed channels.  There is no exclusivity flag to
    // read: the linear permissions on every endpoint are what prove
    // single ownership.
    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept { return false; }

    // Scoped exclusive access to every shard.  Surrendering the
    // recombined whole permission is itself the proof that no handle is
    // alive, and it comes back so the caller can split it again.
    template <typename Body>
    [[nodiscard]] safety::Permission<whole_tag>
    with_recombined_access(safety::Permission<whole_tag>&& whole,
                           Body&& body) noexcept(noexcept(std::forward<Body>(body)())) {
        std::forward<Body>(body)();
        return std::move(whole);
    }

    [[nodiscard]] static constexpr std::string_view graded_type_name() noexcept {
        return "PermissionedShardedCalendarGrid";
    }
};

}  // namespace crucible::concurrent
