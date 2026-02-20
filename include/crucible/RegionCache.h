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
    void insert(const RegionNode* region) {
        assert(region && "inserting null region");

        // Dedup: don't cache the same computation twice.
        for (uint32_t i = 0; i < count_; i++) {
            if (regions_[(head_ - 1 - i) & MASK]->content_hash == region->content_hash)
                return;
        }

        regions_[head_ & MASK] = region;
        head_++;
        if (count_ < CAP) count_++;
    }

    // Find a cached region whose op at position `pos` matches the given
    // schema+shape hashes.  Excludes `exclude` (the region that just
    // diverged).  Searched MRU-first.
    //
    // Only considers regions that have a MemoryPlan (required for Tier 1
    // compiled replay).  Returns nullptr if no match found.
    [[nodiscard]] const RegionNode* find_alternate(
        uint32_t pos,
        SchemaHash schema, ShapeHash shape,
        const RegionNode* exclude = nullptr) const
    {
        for (uint32_t i = 0; i < count_; i++) {
            const auto* r = regions_[(head_ - 1 - i) & MASK];
            if (r == exclude) continue;
            if (!r->plan) continue;
            if (pos >= r->num_ops) continue;
            if (r->ops[pos].schema_hash == schema &&
                r->ops[pos].shape_hash  == shape)
                return r;
        }
        return nullptr;
    }

    // Find by exact content hash.
    [[nodiscard]] const RegionNode* find(ContentHash hash) const {
        for (uint32_t i = 0; i < count_; i++) {
            const auto* r = regions_[(head_ - 1 - i) & MASK];
            if (r->content_hash == hash) return r;
        }
        return nullptr;
    }

    // ── Queries ──

    [[nodiscard]] uint32_t size()  const { return count_; }
    [[nodiscard]] bool     empty() const { return count_ == 0; }

 private:
    const RegionNode* regions_[CAP]{};  // circular buffer (FIFO eviction, MRU-first search)
    uint32_t          head_{0};         // next insertion index (wraps via & MASK)
    uint32_t          count_{0};        // filled entries (0..CAP)
};

} // namespace crucible
