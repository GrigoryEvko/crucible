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
//
// WRAP-RegionCache-6 #991: every public method below carries the
// CRUCIBLE_NO_THREAD_SAFETY attribute so Clang's -Wthread-safety
// (P1179R1, enabled per Platform.h) accepts the unguarded mutable
// state without false positives.  The attribute is the load-bearing
// machine-readable form of the "NOT thread-safe" comment above:
//   * GCC builds: the macro is empty (Platform.h:192) — zero cost,
//     zero behavior change.
//   * Clang builds: the attribute tells -Wthread-safety to skip
//     the check on these methods, which would otherwise demand a
//     mutex annotation we deliberately don't carry.
//
// Adding a new public method to RegionCache requires marking it
// with CRUCIBLE_NO_THREAD_SAFETY — the discipline that converts
// "single-threaded by convention" from comment text into a
// machine-verified contract once Clang joins the build matrix.

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>           // WRAP-RegionCache-6 #991: CRUCIBLE_NO_THREAD_SAFETY
#include <crucible/safety/Cyclic.h>      // safety::Cyclic — ring write cursor (head_)
#include <crucible/safety/Mutation.h>    // safety::BoundedMonotonic — saturating fill counter (count_)
#include <crucible/safety/WeakRef.h>     // safety::WeakRef — nullable non-owning cache slot

#include <cassert>
#include <cstdint>

// WRAP-RegionCache-6 #991 sentinel: the marker MUST be available so
// every public method below carries the analyzer-skip attribute.
// If Platform.h ever drops the definition (unlikely but a real
// regression class), this fires at every TU that includes
// RegionCache.h — long before -Wthread-safety can scream at the
// unguarded mutable state without an opt-out.
#ifndef CRUCIBLE_NO_THREAD_SAFETY
#error "WRAP-RegionCache-6 #991: CRUCIBLE_NO_THREAD_SAFETY must be \
defined by Platform.h before this header — the 4 public RegionCache \
methods depend on the analyzer-skip attribute to compile cleanly \
under Clang's -Wthread-safety (P1179R1)."
#endif

namespace crucible {

struct RegionCache {
    static constexpr uint32_t CAP = 8;
    static_assert((CAP & (CAP - 1)) == 0, "CAP must be a power of 2");
    // (MASK retired in #989: head_ is now safety::Cyclic<uint32_t, CAP>, which
    //  owns the & (CAP-1) wrap-mask internally — see index()/index_back().)

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
    // WRAP-RegionCache-6 #991: machine-readable "fg-only" contract.
    void insert(const RegionNode* region) CRUCIBLE_NO_THREAD_SAFETY {
        assert(region && "inserting null region");

        const ContentHash hash = region->content_hash;

        // Dedup: scan inline content_hashes_ — zero pointer chasing.
        for (uint32_t i = 0; i < count_.get(); i++) {
            if (content_hashes_[head_.index_back(i)] == hash)
                return;
        }

        const uint32_t slot = head_.index();
        // WeakRef::from_raw is the populate-from-a-raw-pointer path; `region`
        // is asserted non-null above, but the slot type is deliberately
        // nullable so eviction/empty slots stay representable.
        regions_[slot]        = safety::WeakRef<const RegionNode>::from_raw(region);
        content_hashes_[slot] = hash;
        ops_[slot]            = region->ops;
        // Plan-less regions get num_ops_=0: find_alternate's bounds check
        // (pos >= 0) rejects them without a separate plan check.
        num_ops_[slot]        = region->plan ? region->num_ops : 0;
        head_.advance();
        // count_ saturates at CAP: the guard is LOAD-BEARING, not defensive —
        // BoundedMonotonic::bump() carries CRUCIBLE_PRE(get() < Max) and does
        // NOT self-saturate, so bumping at CAP would trip the precondition.
        if (count_.get() < CAP) count_.bump();
    }

    // Notify the cache that a region's plan has been set.
    // Updates num_ops_ from 0 (invisible) to the real count, making
    // the region eligible for find_alternate().
    // WRAP-RegionCache-6 #991: machine-readable "fg-only" contract.
    void notify_plan_ready(const RegionNode* region) CRUCIBLE_NO_THREAD_SAFETY {
        assert(region && "null region");
        for (uint32_t i = 0; i < count_.get(); i++) {
            const uint32_t idx = head_.index_back(i);
            if (regions_[idx].try_get() == region) {
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
    // WRAP-RegionCache-6 #991: machine-readable "fg-only" contract.
    [[nodiscard]] const RegionNode* find_alternate(
        uint32_t pos,
        SchemaHash schema, ShapeHash shape,
        const RegionNode* exclude = nullptr) const
        CRUCIBLE_LIFETIMEBOUND CRUCIBLE_NO_THREAD_SAFETY
    {
        for (uint32_t i = 0; i < count_.get(); i++) {
            const uint32_t idx = head_.index_back(i);

            // Filter: identity comparison + bounds check (inline, no ptr chase).
            if (regions_[idx].try_get() == exclude) continue;
            if (pos >= num_ops_[idx]) continue;

            // One pointer chase: ops_[idx] -> TraceEntry at pos.
            if (ops_[idx][pos].schema_hash == schema &&
                ops_[idx][pos].shape_hash  == shape)
                return regions_[idx].try_get();
        }
        return nullptr;
    }

    // Find by exact content hash.  Scans inline content_hashes_ —
    // zero pointer chasing into RegionNode.
    // WRAP-RegionCache-6 #991: machine-readable "fg-only" contract.
    [[nodiscard]] const RegionNode* find(ContentHash hash) const
        CRUCIBLE_LIFETIMEBOUND CRUCIBLE_NO_THREAD_SAFETY
    {
        for (uint32_t i = 0; i < count_.get(); i++) {
            const uint32_t idx = head_.index_back(i);
            if (content_hashes_[idx] == hash) return regions_[idx].try_get();
        }
        return nullptr;
    }

    // ── Queries ──

    [[nodiscard]] uint32_t size()  const { return count_.get(); }
    [[nodiscard]] bool     empty() const { return count_.get() == 0; }

 private:
    // SoA layout: each field in its own contiguous array.
    //
    // regions_:        8 * 8B = 64B = 1 cache line  (WeakRef slots: identity compare via try_get, return value)
    // content_hashes_: 8 * 8B = 64B = 1 cache line  (find, insert dedup)
    // ops_:            8 * 8B = 64B = 1 cache line  (direct ops[pos] access)
    // num_ops_:        8 * 4B = 32B                  (bounds check)
    //
    // Total: 224B + 8B (head_, count_) = 232B.
    // Each scan path touches only the arrays it needs:
    //   find():           content_hashes_ + regions_  = 2 cache lines
    //   insert() dedup:   content_hashes_             = 1 cache line
    //   find_alternate(): regions_ + num_ops_ + ops_  = ~3 cache lines
    // Nullable non-owning cache slots (WRAP-RegionCache-1, #986): each slot
    // starts empty, is populated with a DAG-owned RegionNode the cache does
    // NOT own, and is overwritten (evicted) on FIFO wrap.  WeakRef forces
    // identity access through try_get() so a slot can never be mistaken for
    // an owning pointer (TypeSafe).  Zero-cost: collapses to one pointer.
    safety::WeakRef<const RegionNode> regions_[CAP]{};
    ContentHash       content_hashes_[CAP]{}; // dedup + exact lookup (zero ptr chase)
    const TraceEntry* ops_[CAP]{};            // cached ops pointer (one fewer ptr chase)
    uint32_t          num_ops_[CAP]{};        // cached op count (bounds check inline)

    // Ring write cursor (WRAP-RegionCache-4, #989): a free-running counter read
    // as head_.index() (next-write slot) and head_.index_back(i) (i-th most
    // recent).  Cyclic carries the & (CAP-1) wrap-mask + advance discipline in
    // the type, so the open-coded masking that MASK used to express is gone.
    safety::Cyclic<uint32_t, CAP>           head_{};
    // Saturating fill counter, 0..CAP (WRAP-RegionCache-4, #989):
    // BoundedMonotonic enforces BOTH non-decrease and the CAP ceiling; insert()
    // guards bump() with `get() < CAP` so the saturate-at-CAP semantics hold.
    safety::BoundedMonotonic<uint32_t, CAP> count_{uint32_t{0}};
};

// Zero-cost wiring (WRAP-RegionCache-1, #986): WeakRef<const RegionNode>
// collapses to exactly one pointer, so the regions_[CAP] slot array keeps its
// 64B (one cache-line) footprint and the SoA byte-accounting above stays
// exact.  A regression in WeakRef's zero-cost guarantee reddens here.
static_assert(sizeof(safety::WeakRef<const RegionNode>[RegionCache::CAP])
              == RegionCache::CAP * sizeof(const RegionNode*),
              "WeakRef cache-slot array must stay layout-identical to raw pointers");

// Zero-cost ring state (WRAP-RegionCache-4, #989): head_ (Cyclic) and count_
// (BoundedMonotonic) each collapse to one uint32_t, so the 8B cursor+count
// footprint — and RegionCache's 232B total — is unchanged.  A regression in
// either wrapper's zero-cost guarantee reddens here.
static_assert(sizeof(safety::Cyclic<uint32_t, RegionCache::CAP>) == sizeof(uint32_t),
              "Cyclic ring cursor must stay layout-identical to a raw uint32_t");
static_assert(sizeof(safety::BoundedMonotonic<uint32_t, RegionCache::CAP>) == sizeof(uint32_t),
              "BoundedMonotonic fill counter must stay layout-identical to a raw uint32_t");

} // namespace crucible
