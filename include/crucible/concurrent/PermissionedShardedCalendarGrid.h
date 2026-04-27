#pragma once

// ═══════════════════════════════════════════════════════════════════
// concurrent/PermissionedShardedCalendarGrid.h
//
// PER-SHARD priority queue ladder.  N independent calendar grids
// (one per shard); producer<S> writes only to shard S; consumer<S>
// drains only shard S.  Each shard has its own current_bucket atomic
// — NO cross-thread reads on the producer push path.
//
// ─── Why this exists ──────────────────────────────────────────────
//
// PermissionedCalendarGrid (the single-grid variant) requires every
// producer's try_push to read the SOLE current_bucket, which the
// SOLE consumer is constantly writing.  That cross-thread atomic
// read widens the per-push measurement window from ~5ns (pure
// SpscRing write) to ~50ns (cache-line migration) and exposes
// producers to OS scheduling jitter — empirically observed as
// 100-200μs p99.9 tail under 4-producer contention.
//
// This wrapper mirrors what Linux CFS/EEVDF does at the kernel
// level: per-CPU red-black trees with no inter-CPU coordination on
// enqueue.  Each shard is a self-contained calendar; producer<S>
// reads only shards_[S].current_bucket which only consumer<S>
// writes.  In well-pinned production code (producer P pinned to
// core C, consumer P also on core C or its NUMA-local sibling), the
// read is same-core or single-hop — eliminating the 100μs tail
// at the cost of approximate cross-shard priority.
//
// ─── Per-shard priority semantics (exact)  ────────────────────────
//
// Within shard S: bucket-clamp invariant holds exactly.  The
// consumer<S> sees items in monotone bucket order from any single
// producer<S>.  Lowest priority key (clamped to current bucket)
// always pops first.
//
// ─── Cross-shard priority semantics (approximate) ─────────────────
//
// Across shards: each shard advances current_bucket independently.
// Shard A may be at bucket 100 draining items with key=100;
// Shard B may be at bucket 200 draining items with key=200.
// They are NOT globally ordered — this is the trade-off.
//
// Production usage: pin producer P → shard S(P) (typically
// S = P % NumShards or NUMA-node-of(P)).  If your workload requires
// global priority correctness, use the single-grid variant.
//
// ─── No work-stealing in the MVP ──────────────────────────────────
//
// If shard S's consumer is slower than its producer, shard S backs
// up while sibling shards are idle.  Mitigations:
//   * Static balance: assign producers evenly across shards.
//   * Dynamic balance: future Fix 3.5 adds opt-in stealing where
//     consumer<S> can drain peer shards when its own is empty.
//
// References:
//   THREADING.md §5.5.2 — scheduler flavours
//   misc/27_04_2026.md §1.4 — CSL discipline at every channel boundary
//   PermissionedCalendarGrid.h — the single-shard prior art
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/SpscRing.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/safety/Pinned.h>

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

// ── KeyExtractor concept (matches PermissionedCalendarGrid's) ─────

template <typename K, typename T>
concept ShardedCalendarKeyExtractorOf = requires(const T& v) {
    { K::key(v) } noexcept -> std::convertible_to<std::uint64_t>;
};

// ── Tag tree ─────────────────────────────────────────────────────
//
// Reuses safety::Producer/Consumer from PermissionGridGenerator.h
// — the auto-generated splits_into_pack handles the M-producer ×
// N-consumer split automatically.

namespace sharded_calendar_tag {

template <typename UserTag> struct Whole {};

template <typename UserTag, std::size_t S>
using Producer = safety::Producer<Whole<UserTag>, S>;

template <typename UserTag, std::size_t S>
using Consumer = safety::Consumer<Whole<UserTag>, S>;

}  // namespace sharded_calendar_tag

// ── PermissionedShardedCalendarGrid<T, NumShards, NumBuckets,
//                                    BucketCap, KeyExtractor,
//                                    QuantumNs, UserTag> ────────────

template <SpscValue T,
          std::size_t NumShards,
          std::size_t NumBuckets,
          std::size_t BucketCap,
          typename KeyExtractor,
          std::uint64_t QuantumNs,
          typename UserTag = void>
class PermissionedShardedCalendarGrid
    : public safety::Pinned<PermissionedShardedCalendarGrid<
          T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>>
{
    static_assert(NumShards   > 0, "NumShards must be > 0");
    static_assert(NumBuckets  > 0, "NumBuckets must be > 0");
    static_assert(BucketCap   > 0, "BucketCap must be > 0");
    static_assert(QuantumNs   > 0, "QuantumNs must be > 0");
    static_assert(ShardedCalendarKeyExtractorOf<KeyExtractor, T>,
                  "KeyExtractor must provide static "
                  "uint64_t key(const T&) noexcept");

public:
    using value_type      = T;
    using user_tag        = UserTag;
    using whole_tag       = sharded_calendar_tag::Whole<UserTag>;
    using key_extractor   = KeyExtractor;

    template <std::size_t S>
    using shard_producer_tag = sharded_calendar_tag::Producer<UserTag, S>;
    template <std::size_t S>
    using shard_consumer_tag = sharded_calendar_tag::Consumer<UserTag, S>;

    static constexpr std::size_t   num_shards    = NumShards;
    static constexpr std::size_t   num_buckets   = NumBuckets;
    static constexpr std::size_t   bucket_cap    = BucketCap;
    static constexpr std::uint64_t quantum_ns    = QuantumNs;

    // ── Per-shard storage (heap-allocated; each is large) ──────────
    //
    // sizeof(Shard) ≈ NumBuckets × BucketCap × sizeof(T) + slack.
    // For NumBuckets=1024, BucketCap=64, T=24B: ~1.5 MB per shard.
    // 4 shards = 6 MB total; well within stack-avoidance threshold.

private:
    struct Shard {
        std::array<SpscRing<T, BucketCap>, NumBuckets>      buckets;
        safety::AtomicMonotonic<std::uint64_t,
                                std::less<std::uint64_t>>   current_bucket{0};
    };

    std::array<std::unique_ptr<Shard>, NumShards> shards_;

    // Bucket math — matches PermissionedCalendarGrid::bucket_for_.
    // Producer-side called from shard S; reads shards_[S].current_bucket
    // (SAME-CORE in well-pinned production code — no cross-thread cost).
    [[nodiscard, gnu::hot]] std::size_t bucket_for_(std::size_t shard,
                                                     const T& item) const noexcept
    {
        const std::uint64_t key        = KeyExtractor::key(item);
        const std::uint64_t key_bucket = key / QuantumNs;
        const std::uint64_t cur        = shards_[shard]->current_bucket.peek_relaxed();
        const std::uint64_t b          = key_bucket > cur ? key_bucket : cur;
        return b % NumBuckets;
    }

public:
    PermissionedShardedCalendarGrid() {
        for (std::size_t s = 0; s < NumShards; ++s) {
            shards_[s] = std::make_unique<Shard>();
        }
    }

    // ── ProducerHandle<S> — linear ownership of shard S's producer ─
    //
    // try_push reads ONLY shards_[S].current_bucket (same core as
    // consumer<S> in pinned production code) and writes ONLY to
    // shards_[S].buckets[B].  No coordination with peer shards.

    template <std::size_t S>
    class ProducerHandle {
        static_assert(S < NumShards,
                      "ProducerHandle<S>: S must be < NumShards");

        PermissionedShardedCalendarGrid& grid_;
        [[no_unique_address]] safety::Permission<shard_producer_tag<S>> perm_;

        constexpr ProducerHandle(PermissionedShardedCalendarGrid& g,
                                 safety::Permission<shard_producer_tag<S>>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedShardedCalendarGrid;

    public:
        ProducerHandle(const ProducerHandle&)
            = delete("ProducerHandle owns the shard's Producer Permission — "
                     "copy would duplicate the linear token");
        ProducerHandle& operator=(const ProducerHandle&)
            = delete("ProducerHandle owns the shard's Producer Permission — "
                     "assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&)
            = delete("ProducerHandle binds to ONE shard for life — "
                     "the shard index is part of the type");

        static constexpr std::size_t shard_index = S;

        // Push one item into shard S's calendar.  Bucket index from
        // KeyExtractor::key(item), clamped to shards_[S].current_bucket.
        // Returns false iff the target SpscRing is full.
        //
        // Per-call shape: 1 same-core atomic load (current_bucket) +
        // 1 SpscRing acquire/release pair on the target cell.  Total
        // ~5-10 ns when consumer<S> is on same core or NUMA-local
        // sibling.
        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            const std::size_t b = grid_.bucket_for_(S, item);
            return grid_.shards_[S]->buckets[b].try_push(item);
        }
    };

    // ── ConsumerHandle<S> — linear ownership of shard S's consumer ─
    //
    // try_pop scans shards_[S]'s buckets forward from current_bucket;
    // first non-empty cell yields the item.  Advances current_bucket
    // past skipped empty cells (single writer to current_bucket: only
    // this consumer).

    template <std::size_t S>
    class ConsumerHandle {
        static_assert(S < NumShards,
                      "ConsumerHandle<S>: S must be < NumShards");

        PermissionedShardedCalendarGrid& grid_;
        [[no_unique_address]] safety::Permission<shard_consumer_tag<S>> perm_;

        constexpr ConsumerHandle(PermissionedShardedCalendarGrid& g,
                                 safety::Permission<shard_consumer_tag<S>>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedShardedCalendarGrid;

    public:
        ConsumerHandle(const ConsumerHandle&)
            = delete("ConsumerHandle owns the shard's Consumer Permission");
        ConsumerHandle& operator=(const ConsumerHandle&)
            = delete("ConsumerHandle owns the shard's Consumer Permission");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&)
            = delete("ConsumerHandle binds to ONE shard for life");

        static constexpr std::size_t shard_index = S;

        // Pop highest-priority item from shard S's calendar.
        // Snapshots current_bucket once at entry to prevent scan
        // drift while the consumer's own try_advance happens.
        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            auto& shard = *grid_.shards_[S];
            const std::uint64_t cur_origin = shard.current_bucket.peek_relaxed();
            for (std::size_t scan = 0; scan < NumBuckets; ++scan) {
                const std::uint64_t this_b = cur_origin + scan;
                const std::size_t cell = this_b % NumBuckets;
                if (auto v = shard.buckets[cell].try_pop()) {
                    if (scan > 0) {
                        // Skipped past empty buckets — advance the
                        // monotone counter.  Only this consumer
                        // writes current_bucket, so the advance is
                        // race-free.
                        shard.current_bucket.try_advance(this_b);
                    }
                    return v;
                }
            }
            return std::nullopt;
        }
    };

    // ── Handle factories (linear; consume the Permission) ──────────

    template <std::size_t S>
    [[nodiscard]] ProducerHandle<S> producer(
        safety::Permission<shard_producer_tag<S>>&& perm) noexcept
    {
        static_assert(S < NumShards, "producer<S>: S must be < NumShards");
        return ProducerHandle<S>{*this, std::move(perm)};
    }

    template <std::size_t S>
    [[nodiscard]] ConsumerHandle<S> consumer(
        safety::Permission<shard_consumer_tag<S>>&& perm) noexcept
    {
        static_assert(S < NumShards, "consumer<S>: S must be < NumShards");
        return ConsumerHandle<S>{*this, std::move(perm)};
    }

    // ── Diagnostics ────────────────────────────────────────────────

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return NumShards * NumBuckets * BucketCap;
    }

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

    [[nodiscard]] bool empty_approx() const noexcept {
        return size_approx() == 0;
    }

    // ── Mode transition (linear-only — no atomic pool to drain) ────

    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept {
        return false;
    }

    template <typename Body>
    [[nodiscard]] safety::Permission<whole_tag>
    with_recombined_access(safety::Permission<whole_tag>&& whole, Body&& body)
        noexcept(noexcept(std::forward<Body>(body)()))
    {
        std::forward<Body>(body)();
        return std::move(whole);
    }

    // ── Diagnostic surface (matches concurrent/traits/Concepts.h) ──

    [[nodiscard]] static constexpr std::string_view graded_type_name() noexcept {
        return "PermissionedShardedCalendarGrid";
    }
};

}  // namespace crucible::concurrent
