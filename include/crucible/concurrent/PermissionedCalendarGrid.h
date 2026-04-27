#pragma once

// ═══════════════════════════════════════════════════════════════════
// PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap,
//                           KeyExtractor, QuantumNs, UserTag>
//
// Sub-nanosecond CSL-typed PRIORITY queue.  M producer rows × N
// priority-bucket columns of independent SpscRings.  Producer P writes
// to its OWN row (linear permission, no atomic on hot path); the
// bucket index is computed from the item's priority key K via
// `max(K / QuantumNs, current_bucket) % NumBuckets` — the calendar
// queue trick (Brown 1988) layered on per-producer SPSC sharding.
//
// A single drain consumer walks `current_bucket` monotonically,
// collecting items in priority order across all M producer rows for
// the current bucket before advancing.  current_bucket is an
// AtomicMonotonic; the consumer is the sole writer (linear permission),
// producers read-only via peek_relaxed.
//
// ─── Per-op cost ───────────────────────────────────────────────────
//
//   producer.try_push(item)        : ~5-8 ns  (1 div + max + SPSC release-store)
//   producer.try_push_batch(span)  : ~0.075-0.5 ns / item amortized
//                                    (caller pre-grouped by bucket → 1 SPSC
//                                     batched call per bucket; SPSC batched
//                                     per-item floor measured at L1 store-port)
//   consumer.try_pop()             : ~5-15 ns (per-bucket scan across M rows)
//   consumer.try_pop_batch(out)    : ~0.075-0.5 ns / item amortized
//                                    (single SPSC try_pop_batch per
//                                     non-empty cell; bucket advance is
//                                     a single AtomicMonotonic CAS only
//                                     when current bucket is fully empty)
//
//   No mutex.  No FAA on the hot path (cross-thread sync is
//   `current_bucket`'s monotonic CAS only on bucket-advance, NOT per
//   push/pop).  Every cell access stays in producer-private OR
//   consumer-private cache lines.
//
// ─── Storage ───────────────────────────────────────────────────────
//
//   NumProducers × NumBuckets × SpscRing<T, BucketCap>
//   For (M=8, N=64, C=64, T=u64): 8 × 64 × (~1KB SpscRing) ~= 512 KB
//   Fits in L2 per core.  Each SpscRing is alignas(64) on its head
//   and tail counters; no false sharing.
//
// ─── Why per-producer SPSC sharding (and NOT MpmcRing) ─────────────
//
//   FAA on a single shared cache line (the MpmcRing hot path) is
//   irreducibly ~15-25 ns even uncontended — `lock`-prefixed atomics
//   drain the store buffer.  To get sub-ns per item, the substrate
//   MUST avoid cross-thread atomics on the per-item path.  Per-
//   producer-private SpscRings achieve this: producer P writes to
//   ITS row; only the consumer ever reads from it.  No FAA, no MESI
//   ping-pong, no cache-line contention.
//
//   The trade-offs vs MpmcRing (documented in THREADING.md §5.5.1):
//     * Loses global FIFO (calendar queue's bucket order is the
//       priority order, not arrival order)
//     * Loses dynamic membership (NumProducers is compile-time)
//     * Loses shared-capacity (M independent rows; bursty producers
//       cannot use empty rows of other producers)
//     * Gains: sub-ns per-item batched throughput, NUMA locality,
//       compile-time slot identity, zero-FAA hot path
//
//   For priority scheduling (Deadline / Cfs / Eevdf), these
//   trade-offs are favorable: ordering is by priority not arrival,
//   producer count is bounded by core count, and sub-ns dispatch
//   matters more than capacity sharing.
//
// ─── CSL discipline ────────────────────────────────────────────────
//
//   Whole<UserTag> splits into:
//     * NumProducers Producer<UserTag, P> linear permissions
//     * 1 Consumer<UserTag> linear permission
//
//   Re-uses the FOUND-A22 split_grid<Whole, M, 1> factory with
//   N=1 — the consumer side has exactly one slot.
//
//     auto whole = permission_root_mint<calendar_tag::Whole<X>>();
//     auto perms = split_grid<calendar_tag::Whole<X>, M, 1>(whole);
//     auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
//     auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
//     // ...
//     auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));
//
//   The type system enforces:
//     * Each producer slot is single-owner (linear Permission move)
//     * Single consumer (linear Permission move)
//     * Producers can only push (no try_pop on ProducerHandle<P>)
//     * Consumer can only pop (no try_push on ConsumerHandle)
//     * Compile-time slot index — ProducerHandle<I> ≠ ProducerHandle<J>
//
// ─── KeyExtractor concept ──────────────────────────────────────────
//
//   Caller provides a stateless type with:
//     static std::uint64_t key(const T& item) noexcept;
//
//   The key is the PRIORITY (smaller = higher priority).  Typical
//   instances:
//     * struct DeadlineKey { static uint64_t key(const Job& j) noexcept
//         { return j.deadline_ns; } };
//     * struct VruntimeKey { static uint64_t key(const Task& t) noexcept
//         { return t.vruntime; } };
//     * struct VdeadlineKey { static uint64_t key(const Task& t) noexcept
//         { return t.vdeadline_ns; } };
//
//   These plug into Deadline / Cfs / Eevdf scheduler policies
//   (concurrent/scheduler/Policies.h, follow-up commit).
//
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/SpscRing.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/safety/Pinned.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ── Tag tree for PermissionedCalendarGrid ──────────────────────────
//
// Mirrors PermissionedShardedGrid's grid_tag pattern: Whole<UserTag>
// is the root, splits via FOUND-A22's split_grid<Whole, M, 1> into
// M producer slots + 1 consumer slot.  Producer index is part of
// the type (Slice<ProducerSide<Whole>, P>); the single consumer is
// Slice<ConsumerSide<Whole>, 0>.

namespace calendar_tag {

template <typename UserTag> struct Whole {};

template <typename UserTag, std::size_t P>
using Producer = safety::Producer<Whole<UserTag>, P>;

template <typename UserTag>
using Consumer = safety::Consumer<Whole<UserTag>, 0>;

}  // namespace calendar_tag

// ── KeyExtractor concept ──────────────────────────────────────────

template <typename E, typename T>
concept KeyExtractorOf =
    requires(const T& item) {
        { E::key(item) } noexcept -> std::same_as<std::uint64_t>;
    };

// ── PermissionedCalendarGrid<T, M, NumBuckets, Cap, KeyExtractor,
//                             QuantumNs, UserTag> ──────────────────

template <SpscValue T,
          std::size_t NumProducers,
          std::size_t NumBuckets,
          std::size_t BucketCap,
          typename KeyExtractor,
          std::uint64_t QuantumNs,
          typename UserTag = void>
class PermissionedCalendarGrid
    : public safety::Pinned<PermissionedCalendarGrid<T, NumProducers, NumBuckets,
                                                      BucketCap, KeyExtractor,
                                                      QuantumNs, UserTag>>
{
    static_assert(NumProducers > 0,
                  "PermissionedCalendarGrid: NumProducers must be > 0");
    static_assert(NumBuckets > 0,
                  "PermissionedCalendarGrid: NumBuckets must be > 0");
    static_assert(BucketCap > 0,
                  "PermissionedCalendarGrid: BucketCap must be > 0");
    static_assert(QuantumNs > 0,
                  "PermissionedCalendarGrid: QuantumNs must be > 0");
    static_assert(KeyExtractorOf<KeyExtractor, T>,
                  "KeyExtractor must provide static "
                  "uint64_t key(const T&) noexcept");

public:
    using value_type     = T;
    using user_tag       = UserTag;
    using whole_tag      = calendar_tag::Whole<UserTag>;
    using consumer_tag   = calendar_tag::Consumer<UserTag>;
    using key_extractor  = KeyExtractor;

    template <std::size_t P>
    using producer_tag = calendar_tag::Producer<UserTag, P>;

    static constexpr std::size_t num_producers = NumProducers;
    static constexpr std::size_t num_buckets   = NumBuckets;
    static constexpr std::size_t bucket_cap    = BucketCap;
    static constexpr std::uint64_t quantum_ns  = QuantumNs;

    constexpr PermissionedCalendarGrid() noexcept = default;

    // ── ProducerHandle<P> ─────────────────────────────────────────
    //
    // Linear single-owner over row P.  EBO-collapses the Permission
    // to zero bytes, so sizeof(ProducerHandle<P>) == sizeof(Channel*).
    // EXPOSES try_push / try_push_batch only — no consumer methods.

    template <std::size_t P>
    class ProducerHandle {
        static_assert(P < NumProducers,
                      "ProducerHandle<P>: P must be < NumProducers");

        PermissionedCalendarGrid& grid_;
        [[no_unique_address]] safety::Permission<producer_tag<P>> perm_;

        constexpr ProducerHandle(PermissionedCalendarGrid& g,
                                 safety::Permission<producer_tag<P>>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedCalendarGrid;

    public:
        ProducerHandle(const ProducerHandle&)
            = delete("ProducerHandle owns the row's Producer Permission — "
                     "copy would duplicate the linear token");
        ProducerHandle& operator=(const ProducerHandle&)
            = delete("ProducerHandle owns the row's Producer Permission — "
                     "assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&)
            = delete("ProducerHandle binds to ONE row for life — "
                     "the row index is part of the type");

        static constexpr std::size_t row_index = P;

        // Push one item.  Bucket index derived from KeyExtractor::key(item)
        // clamped to current_bucket (calendar queue: late items land at
        // the head bucket so they run immediately).  Returns false iff
        // the target SpscRing is full.
        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            const std::size_t b = grid_.bucket_for_(item);
            return grid_.rings_[P][b].try_push(item);
        }

        // Batched push.  Caller best-amortizes by pre-grouping items
        // by their target bucket (typical: items are produced in
        // priority order, all hit the same future bucket).  Per-item
        // cost amortizes to the SPSC store-port floor (~0.075 ns)
        // when items go to one bucket.
        //
        // Implementation collects contiguous runs with identical
        // target bucket, calls SpscRing::try_push_batch per run.
        // Stops at the first partial-batch (bucket full) and returns
        // total items pushed.
        [[nodiscard, gnu::hot]] std::size_t try_push_batch(
            std::span<const T> items) noexcept
        {
            std::size_t total = 0;
            std::size_t i = 0;
            while (i < items.size()) {
                const std::size_t bucket = grid_.bucket_for_(items[i]);
                std::size_t run_end = i + 1;
                while (run_end < items.size()
                    && grid_.bucket_for_(items[run_end]) == bucket)
                {
                    ++run_end;
                }
                const std::size_t want = run_end - i;
                const std::size_t pushed = grid_.rings_[P][bucket]
                    .try_push_batch(items.subspan(i, want));
                total += pushed;
                if (pushed < want) break;     // bucket full, stop
                i = run_end;
            }
            return total;
        }

        // Diagnostics (own row only, since handle owns row P).
        [[nodiscard]] static constexpr std::size_t capacity() noexcept {
            return NumBuckets * BucketCap;
        }
    };

    // ── ConsumerHandle ────────────────────────────────────────────
    //
    // Single linear consumer.  Drains in priority order: walks
    // buckets starting from current_bucket, collects items across
    // all M producer rows for the current bucket, advances
    // current_bucket monotonically only when the head bucket is
    // fully empty.  EXPOSES try_pop / try_pop_batch only.

    class ConsumerHandle {
        PermissionedCalendarGrid& grid_;
        [[no_unique_address]] safety::Permission<consumer_tag> perm_;

        constexpr ConsumerHandle(PermissionedCalendarGrid& g,
                                 safety::Permission<consumer_tag>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedCalendarGrid;

    public:
        ConsumerHandle(const ConsumerHandle&)
            = delete("ConsumerHandle owns the unique Consumer Permission "
                     "— copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&)
            = delete("ConsumerHandle owns the unique Consumer Permission "
                     "— assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&)
            = delete("ConsumerHandle binds to ONE consumer for life");

        // Pop the highest-priority item (lowest bucket index from
        // current_bucket).  Scans up to NumBuckets buckets; returns
        // nullopt iff queue is fully empty.
        //
        // Per-call cost: ~5-15 ns warm path (1 SPSC try_pop on the
        // current bucket's first non-empty row).  Worst case (queue
        // empty) scans NumBuckets × NumProducers cells.
        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            // Snapshot cur ONCE at entry — advancing inside the loop
            // would shift the scan window forward.  cur_origin +
            // scan is the absolute bucket counter being inspected;
            // % NumBuckets yields the physical bucket index.
            const std::uint64_t cur_origin =
                grid_.current_bucket_.peek_relaxed();
            for (std::size_t scan = 0; scan < NumBuckets; ++scan) {
                const std::uint64_t this_b = cur_origin + scan;
                const std::size_t   bucket = this_b % NumBuckets;
                for (std::size_t p = 0; p < NumProducers; ++p) {
                    if (auto v = grid_.rings_[p][bucket].try_pop()) {
                        return v;
                    }
                }
                // Bucket fully empty across all M producer rows.
                // Advance current_bucket past it (monotonic CAS;
                // no-op if already advanced by a prior call).
                grid_.advance_past_(this_b);
            }
            return std::nullopt;
        }

        // Batched pop.  Drains the current bucket fully (across all
        // M producer rows) before advancing.  Per-item cost
        // amortizes to the SPSC store-port floor (~0.075-0.5 ns)
        // when the current bucket has items.
        [[nodiscard, gnu::hot]] std::size_t try_pop_batch(
            std::span<T> out) noexcept
        {
            if (out.empty()) return 0;
            std::size_t total = 0;
            // Snapshot cur ONCE; absolute scan position is
            // cur_origin + scan.  Same rationale as try_pop.
            const std::uint64_t cur_origin =
                grid_.current_bucket_.peek_relaxed();
            for (std::size_t scan = 0;
                 scan < NumBuckets && total < out.size(); ++scan)
            {
                const std::uint64_t this_b = cur_origin + scan;
                const std::size_t   bucket = this_b % NumBuckets;
                std::size_t bucket_got = 0;
                for (std::size_t p = 0;
                     p < NumProducers && total < out.size(); ++p)
                {
                    const std::size_t got = grid_.rings_[p][bucket]
                        .try_pop_batch(out.subspan(total));
                    total += got;
                    bucket_got += got;
                }
                if (bucket_got == 0) {
                    // Bucket fully empty.  Advance and continue
                    // scanning later buckets.
                    grid_.advance_past_(this_b);
                    continue;
                }
                // Got items from this bucket.  Don't advance — bucket
                // may have more items added by concurrent producers
                // before consumer returns here.
                return total;
            }
            return total;
        }

        // Channel-level diagnostics.
        [[nodiscard]] bool empty_approx() const noexcept {
            return grid_.empty_approx();
        }
        [[nodiscard]] std::uint64_t current_bucket() const noexcept {
            return grid_.current_bucket_.peek_relaxed();
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept {
            return NumProducers * NumBuckets * BucketCap;
        }
    };

    // ── Factories ─────────────────────────────────────────────────
    //
    // Mirrors PermissionedShardedGrid: caller obtains permissions via
    // split_grid<calendar_tag::Whole<UserTag>, M, 1>(whole) and
    // hands them to producer<P>() and consumer().

    template <std::size_t P>
    [[nodiscard]] ProducerHandle<P> producer(
        safety::Permission<producer_tag<P>>&& perm) noexcept
    {
        static_assert(P < NumProducers, "producer<P>: P must be < NumProducers");
        return ProducerHandle<P>{*this, std::move(perm)};
    }

    [[nodiscard]] ConsumerHandle consumer(
        safety::Permission<consumer_tag>&& perm) noexcept
    {
        return ConsumerHandle{*this, std::move(perm)};
    }

    // ── Mode transition (linear-only — same shape as Spsc / ShardedGrid) ──
    //
    // Calendar grid has linear permissions on all M+1 endpoints — no
    // pool to drain.  Caller surrenders the recombined whole
    // permission as type-level proof of quiescence; body runs with
    // exclusive access to the entire M × NumBuckets storage; whole
    // permission is returned for re-split.
    template <typename Body>
        requires std::is_invocable_v<Body>
    [[nodiscard]] safety::Permission<whole_tag>
    with_recombined_access(safety::Permission<whole_tag>&& whole, Body&& body)
        noexcept(std::is_nothrow_invocable_v<Body>)
    {
        std::forward<Body>(body)();
        return std::move(whole);
    }

    // ── Diagnostics (universal Permissioned* surface) ─────────────

    // Linear permissions on all endpoints; no atomic exclusivity flag.
    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept {
        return false;
    }

    // Channel-level capacity = M × NumBuckets × BucketCap.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return NumProducers * NumBuckets * BucketCap;
    }

    // True iff every cell appears empty (snapshot read; not exact
    // under concurrent push).
    [[nodiscard]] bool empty_approx() const noexcept {
        for (std::size_t p = 0; p < NumProducers; ++p) {
            for (std::size_t b = 0; b < NumBuckets; ++b) {
                if (!rings_[p][b].empty_approx()) return false;
            }
        }
        return true;
    }

    // Sum of size_approx across all M × NumBuckets cells.
    [[nodiscard]] std::size_t size_approx() const noexcept {
        std::size_t total = 0;
        for (std::size_t p = 0; p < NumProducers; ++p) {
            for (std::size_t b = 0; b < NumBuckets; ++b) {
                total += rings_[p][b].size_approx();
            }
        }
        return total;
    }

    // Current bucket pointer (consumer's drain position).  Read-only
    // diagnostic; advances monotonically as consumer drains.
    [[nodiscard]] std::uint64_t current_bucket() const noexcept {
        return current_bucket_.peek_relaxed();
    }

private:
    // Compute target bucket index for an item.  Calendar queue rule:
    //   target = max(K / QuantumNs, current_bucket)
    //   bucket_index = target % NumBuckets
    //
    // Late items (K/QuantumNs < current_bucket) clamp to current
    // bucket so they run immediately.  Future items go to a bucket
    // ahead of current; consumer will drain them when it advances.
    //
    // Modulo wrap is acceptable: if a producer's K/QuantumNs is
    // ahead of current by MORE than NumBuckets, the wrap collides
    // with a "current" bucket — items from disparate priorities
    // mix in FIFO order within that physical bucket.  Caller sizes
    // NumBuckets to bound this collision (typical: NumBuckets >
    // expected priority window in QuantumNs units).
    [[nodiscard, gnu::pure]] std::size_t bucket_for_(const T& item) const noexcept {
        const std::uint64_t k       = KeyExtractor::key(item);
        const std::uint64_t target  = k / QuantumNs;
        const std::uint64_t cur     = current_bucket_.peek_relaxed();
        const std::uint64_t clamped = (target > cur) ? target : cur;
        return clamped % NumBuckets;
    }

    // Advance current_bucket past `value` if it's still at or below.
    // Single-consumer use; the CAS protects against the (unlikely)
    // case of the same consumer thread reentering through nested
    // try_pop.  Monotonic-direction enforced by AtomicMonotonic.
    void advance_past_(std::uint64_t value) noexcept {
        std::uint64_t expected = value;
        (void)current_bucket_.compare_exchange_advance_weak(
            expected, value + 1,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    // Storage: M × NumBuckets independent SpscRings.
    //
    // Layout note: the inner array is per-producer-row.  All buckets
    // for producer P sit contiguously; producer P's working set is
    // bucket-row-local.  Consumer scans bucket-by-bucket across rows
    // — slightly less cache-friendly than the alternative layout,
    // but it matches the "producer-private row" CSL discipline (each
    // ProducerHandle<P> holds Permission<Producer<P>> over rings_[P][*]).
    std::array<std::array<SpscRing<T, BucketCap>, NumBuckets>, NumProducers>
        rings_{};

    // Drain pointer.  Monotonic, single-consumer (linear permission
    // ensures sole writer).  Producers read-only via peek_relaxed.
    safety::AtomicMonotonic<std::uint64_t> current_bucket_{0};
};

}  // namespace crucible::concurrent

// ── splits_into_pack auto-spec ─────────────────────────────────────
//
// CalendarGrid uses the same FOUND-A22 auto-permission-tree as
// ShardedGrid.  No new specialization is needed: split_grid<Whole, M, N>
// already expands Whole into ProducerSide / ConsumerSide / Slices.
// Callers use `split_grid<calendar_tag::Whole<X>, M, 1>(whole)`.
