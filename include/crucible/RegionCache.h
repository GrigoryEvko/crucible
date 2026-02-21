#pragma once

// RegionCache: caches multiple compiled regions for instant switching.
//
// When input shapes change between iterations, the active region diverges.
// Instead of falling back to RECORDING for a full iteration, the cache
// stores previous regions so dispatch_op can instantly switch to a matching
// variant — including mid-iteration switches with pool data migration.
//
// Typical use: dynamic batch sizes.  Region A compiled for batch=128,
// Region B for batch=256.  On divergence at any op position,
// find_alternate() locates Region B and Vigil switches — zero recording
// overhead, zero background thread work.
//
// Capacity: 8 regions (power-of-2 for bitmask indexing).  FIFO eviction
// when full.  Searched MRU-first (most recently activated regions are
// most likely to match again).
//
// Layout: SoA (Structure of Arrays) — parallel arrays for each field.
// find() and insert() dedup scan content_hashes_ without touching
// RegionNode memory.  find_alternate() uses inline num_ops_ and
// has_plan_ for filtering, then accesses ops_[idx][pos] with one
// pointer chase (vs two through region->ops in the original design).
//
// Thread safety: NOT thread-safe.  All access is on the foreground
// thread — insert() from try_align_(), find_alternate() from the
// dispatch_op divergence path.

#include <crucible/MerkleDag.h>

#include <cassert>
#include <cstdint>

namespace crucible {

struct RegionCache {
    static constexpr uint32_t CAP = 8;
    static constexpr uint32_t MASK = CAP - 1;
    static_assert((CAP & (CAP - 1)) == 0, "CAP must be a power of 2");

    RegionCache() = default;
    RegionCache(const RegionCache&)            = delete("embedded in Vigil; no reason to copy");
    RegionCache& operator=(const RegionCache&) = delete("embedded in Vigil; no reason to copy");
    RegionCache(RegionCache&&)                 = delete("embedded in Vigil; no reason to move");
    RegionCache& operator=(RegionCache&&)      = delete("embedded in Vigil; no reason to move");

    // Insert a region.  No-op if already cached (same content_hash).
    // If full, evicts the oldest entry (FIFO).
    //
    // Regions without a MemoryPlan are stored with num_ops_=0, making
    // them invisible to find_alternate() (any pos >= 0 fails the bounds
    // check).  Call notify_plan_ready() when the plan arrives.
    void insert(const RegionNode* region) {
        assert(region && "inserting null region");

        const ContentHash hash = region->content_hash;

        // Dedup: scan inline content_hashes_ — zero pointer chasing.
        for (uint32_t i = 0; i < count_; i++) {
            if (content_hashes_[(head_ - 1 - i) & MASK] == hash)
                return;
        }

        const uint32_t slot = head_ & MASK;
        regions_[slot]        = region;
        content_hashes_[slot] = hash;
        ops_[slot]            = region->ops;
        // Plan-less regions get num_ops_=0: find_alternate's bounds check
        // (pos >= 0) rejects them without a separate plan check.
        num_ops_[slot]        = region->plan ? region->num_ops : 0;
        head_++;
        if (count_ < CAP) count_++;
    }

    // Notify the cache that a region's plan has been set.
    // Updates num_ops_ from 0 (invisible) to the real count, making
    // the region eligible for find_alternate().
    void notify_plan_ready(const RegionNode* region) {
        assert(region && "null region");
        for (uint32_t i = 0; i < count_; i++) {
            const uint32_t idx = (head_ - 1 - i) & MASK;
            if (regions_[idx] == region) {
                num_ops_[idx] = region->plan ? region->num_ops : 0;
                return;
            }
        }
    }

    // Find a cached region whose op at position `pos` matches the given
    // schema+shape hashes.  Excludes `exclude` (the region that just
    // diverged).  Searched MRU-first.
    //
    // SoA hot path: regions_ for pointer comparison, ops_ for one-chase
    // hash lookup, num_ops_ for bounds check.  The plan eligibility check
    // is folded into num_ops_ (plan-less regions have num_ops_=0).
    //
    // Returns nullptr if no match found.
    [[nodiscard]] const RegionNode* find_alternate(
        uint32_t pos,
        SchemaHash schema, ShapeHash shape,
        const RegionNode* exclude = nullptr) const CRUCIBLE_LIFETIMEBOUND
    {
        for (uint32_t i = 0; i < count_; i++) {
            const uint32_t idx = (head_ - 1 - i) & MASK;

            // Filter: pointer comparison + bounds check (inline, no ptr chase).
            if (regions_[idx] == exclude) continue;
            if (pos >= num_ops_[idx]) continue;

            // One pointer chase: ops_[idx] -> TraceEntry at pos.
            if (ops_[idx][pos].schema_hash == schema &&
                ops_[idx][pos].shape_hash  == shape)
                return regions_[idx];
        }
        return nullptr;
    }

    // Find by exact content hash.  Scans inline content_hashes_ —
    // zero pointer chasing into RegionNode.
    [[nodiscard]] const RegionNode* find(ContentHash hash) const CRUCIBLE_LIFETIMEBOUND {
        for (uint32_t i = 0; i < count_; i++) {
            const uint32_t idx = (head_ - 1 - i) & MASK;
            if (content_hashes_[idx] == hash) return regions_[idx];
        }
        return nullptr;
    }

    // ── Queries ──

    [[nodiscard]] uint32_t size()  const { return count_; }
    [[nodiscard]] bool     empty() const { return count_ == 0; }

 private:
    // SoA layout: each field in its own contiguous array.
    //
    // regions_:        8 * 8B = 64B = 1 cache line  (pointer comparison, return value)
    // content_hashes_: 8 * 8B = 64B = 1 cache line  (find, insert dedup)
    // ops_:            8 * 8B = 64B = 1 cache line  (direct ops[pos] access)
    // num_ops_:        8 * 4B = 32B                  (bounds check)
    //
    // Total: 224B + 8B (head_, count_) = 232B.
    // Each scan path touches only the arrays it needs:
    //   find():           content_hashes_ + regions_  = 2 cache lines
    //   insert() dedup:   content_hashes_             = 1 cache line
    //   find_alternate(): regions_ + num_ops_ + ops_  = ~3 cache lines
    const RegionNode* regions_[CAP]{};        // region pointers
    ContentHash       content_hashes_[CAP]{}; // dedup + exact lookup (zero ptr chase)
    const TraceEntry* ops_[CAP]{};            // cached ops pointer (one fewer ptr chase)
    uint32_t          num_ops_[CAP]{};        // cached op count (bounds check inline)

    uint32_t          head_{0};               // next insertion index (wraps via & MASK)
    uint32_t          count_{0};              // filled entries (0..CAP)
};

} // namespace crucible
