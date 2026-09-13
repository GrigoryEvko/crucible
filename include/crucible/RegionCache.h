#pragma once

// Holds the regions compiled for earlier shapes so that a shape change can
// switch to a matching one instead of recording a whole iteration again. The
// switch can happen at any op position, not only at an iteration boundary.
//
// Nothing here is thread-safe. Every entry point runs on the foreground
// thread, and each carries the attribute that tells a thread-safety analyzer
// so. A new entry point has to carry it too, or the analyzer demands a lock
// this class deliberately does not have.

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/safety/Cyclic.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/WeakRef.h>

#include <cassert>
#include <cstdint>

#ifndef CRUCIBLE_NO_THREAD_SAFETY
#error "CRUCIBLE_NO_THREAD_SAFETY must be defined before this header: every \
public RegionCache method needs it to compile under a thread-safety analyzer."
#endif

namespace crucible {

struct RegionCache {
    static constexpr uint32_t CAP = 8;
    static_assert((CAP & (CAP - 1)) == 0, "CAP must be a power of 2");

    RegionCache() = default;
    RegionCache(const RegionCache&) = delete("embedded in Vigil; no reason to copy");
    RegionCache& operator=(const RegionCache&) = delete("embedded in Vigil; no reason to copy");
    RegionCache(RegionCache&&) = delete("embedded in Vigil; no reason to move");
    RegionCache& operator=(RegionCache&&) = delete("embedded in Vigil; no reason to move");

    void insert(const RegionNode* region) CRUCIBLE_NO_THREAD_SAFETY {
        assert(region && "inserting null region");

        const ContentHash hash = region->content_hash;

        for (uint32_t i = 0; i < count_.get(); i++) {
            if (content_hashes_[head_.index_back(i)] == hash) return;
        }

        const uint32_t slot = head_.index();
        // The slot type stays nullable so an empty or evicted slot remains
        // representable, even though this particular write is non-null.
        regions_[slot] = safety::WeakRef<const RegionNode>::from_raw(region);
        content_hashes_[slot] = hash;
        ops_[slot] = OpsPtr{region->ops};
        // A region with no memory plan is stored with a count of zero, which
        // the position bound in find_alternate rejects. That is why there is
        // no separate plan check there.
        num_ops_[slot] = region->plan ? region->num_ops : 0;
        head_.advance();
        // The counter does not saturate on its own. Its increment carries a
        // precondition that the value is below the bound, so this guard is
        // what keeps a full cache from tripping it.
        if (count_.get() < CAP) count_.bump();
    }

    // Call this once a region's memory plan exists, which is what makes the
    // region eligible for find_alternate.
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

    // The excluded region is the one that just diverged. The scan runs from
    // the most recently inserted entry backwards, because a shape that
    // matched recently is the one most likely to match again.
    [[nodiscard]] const RegionNode* find_alternate(uint32_t pos, SchemaHash schema, ShapeHash shape,
                                                   const RegionNode* exclude = nullptr) const CRUCIBLE_LIFETIMEBOUND
    CRUCIBLE_NO_THREAD_SAFETY {
        for (uint32_t i = 0; i < count_.get(); i++) {
            const uint32_t idx = head_.index_back(i);

            if (regions_[idx].try_get() == exclude) continue;
            if (pos >= num_ops_[idx]) continue;

            const TraceEntry* ops_ptr = ops_[idx].value();
            if (ops_ptr[pos].schema_hash == schema && ops_ptr[pos].shape_hash == shape) return regions_[idx].try_get();
        }
        return nullptr;
    }

    [[nodiscard]] const RegionNode* find(ContentHash hash) const CRUCIBLE_LIFETIMEBOUND CRUCIBLE_NO_THREAD_SAFETY {
        for (uint32_t i = 0; i < count_.get(); i++) {
            const uint32_t idx = head_.index_back(i);
            if (content_hashes_[idx] == hash) return regions_[idx].try_get();
        }
        return nullptr;
    }

    [[nodiscard]] uint32_t size() const { return count_.get(); }
    [[nodiscard]] bool empty() const { return count_.get() == 0; }

private:
    // One array per field rather than one array of records, so that each
    // scan reads only the arrays it needs. The three pointer-wide arrays are
    // one cache line each at this capacity.
    //
    // A slot holds a region the graph owns and this cache does not. The slot
    // type makes that explicit and keeps the identity read behind an accessor
    // that can report an empty slot.
    safety::WeakRef<const RegionNode> regions_[CAP]{};
    ContentHash content_hashes_[CAP]{};
    // The tag records that the pointer was taken out of a region at the
    // moment it was inserted here.
    using OpsPtr = ::crucible::safety::Tagged<const TraceEntry*, ::crucible::safety::source::RegionOps>;
    OpsPtr ops_[CAP]{};
    uint32_t num_ops_[CAP]{};

    // A free-running counter. index() is the next slot to write and
    // index_back(i) is the i-th most recently written one.
    safety::Cyclic<uint32_t, CAP> head_{};
    safety::BoundedMonotonic<uint32_t, CAP> count_{uint32_t{0}};
};

static_assert(sizeof(::crucible::safety::Tagged<const TraceEntry*, ::crucible::safety::source::RegionOps>)
                  == sizeof(const TraceEntry*),
              "Tagged<const TraceEntry*, source::RegionOps> must preserve pointer size");
static_assert(alignof(::crucible::safety::Tagged<const TraceEntry*, ::crucible::safety::source::RegionOps>)
                  == alignof(const TraceEntry*),
              "Tagged<const TraceEntry*, source::RegionOps> must preserve pointer alignment");
static_assert(sizeof(safety::WeakRef<const RegionNode>[RegionCache::CAP])
                  == RegionCache::CAP * sizeof(const RegionNode*),
              "WeakRef cache-slot array must stay layout-identical to raw pointers");

static_assert(sizeof(safety::Cyclic<uint32_t, RegionCache::CAP>) == sizeof(uint32_t),
              "Cyclic ring cursor must stay layout-identical to a raw uint32_t");
static_assert(sizeof(safety::BoundedMonotonic<uint32_t, RegionCache::CAP>) == sizeof(uint32_t),
              "BoundedMonotonic fill counter must stay layout-identical to a raw uint32_t");

}  // namespace crucible
