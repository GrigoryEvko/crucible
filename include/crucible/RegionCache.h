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
#include <fixy/Borrowed.h>
#include <fixy/Cyclic.h>
#include <fixy/FixedArray.h>
#include <fixy/Mutation.h>
#include <foundation/Platform.h>

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

    // A region of a content hash that the cache holds already takes no
    // second slot.  A full cache drops its oldest entry.
    void insert(const RegionNode& region) CRUCIBLE_NO_THREAD_SAFETY {
        const ContentHash hash = region.content_hash;

        for (uint32_t i = 0; i < count_.get(); i++) {
            if (content_hashes_[head_.index_back(i)] == hash) return;
        }

        const uint32_t slot = head_.index();
        regions_[slot] = ::fixy::WeakRef<const RegionNode>{region};
        content_hashes_[slot] = hash;
        head_.advance();
        // The counter does not saturate on its own. Its increment carries a
        // precondition that the value is below the bound, so this guard is
        // what keeps a full cache from tripping it.
        if (count_.get() < CAP) count_.bump();
    }

    // The excluded region is the one that just diverged. The scan runs from
    // the most recently inserted entry backwards, because a shape that
    // matched recently is the one most likely to match again.
    //
    // A region is eligible only once it carries a memory plan, because
    // switching to one without a plan cannot put the context into compiled
    // mode: CrucibleContext::activate returns false on a null plan.  The scan
    // reads the plan, the op count and the ops off the region itself, and
    // not off a snapshot of the insert.  So a region whose plan is built
    // after it was cached becomes eligible on its own.
    //
    // The region store of the pipeline owns each region, and the cache only
    // points at it.  The store keeps each region that the cache holds.
    [[nodiscard]] ::fixy::WeakRef<const RegionNode> find_alternate(uint32_t pos, SchemaHash schema, ShapeHash shape,
                                                                   ::fixy::WeakRef<const RegionNode> exclude = {}) const
        CRUCIBLE_NO_THREAD_SAFETY {
        for (uint32_t i = 0; i < count_.get(); i++) {
            const ::fixy::WeakRef<const RegionNode> candidate = regions_[head_.index_back(i)];
            if (!candidate.has_value() || candidate == exclude) continue;
            if (candidate->plan == nullptr) continue;
            if (pos >= candidate->num_ops()) continue;
            if (candidate->op_at(pos).schema_hash == schema && candidate->op_at(pos).shape_hash == shape)
                return candidate;
        }
        return {};
    }

    [[nodiscard]] ::fixy::WeakRef<const RegionNode> find(ContentHash hash) const CRUCIBLE_NO_THREAD_SAFETY {
        for (uint32_t i = 0; i < count_.get(); i++) {
            const uint32_t idx = head_.index_back(i);
            if (content_hashes_[idx] == hash) return regions_[idx];
        }
        return {};
    }

    [[nodiscard]] uint32_t size() const { return count_.get(); }
    [[nodiscard]] bool empty() const { return count_.get() == 0; }

    // The region of the entry of the given age, where age 0 is the newest
    // entry, or an empty reference past the count.  The foreground gives
    // these regions to the region store of the pipeline, which keeps each
    // region that the cache holds.
    [[nodiscard]] ::fixy::WeakRef<const RegionNode> region_at(uint32_t age) const CRUCIBLE_NO_THREAD_SAFETY {
        if (age >= count_.get()) return {};
        return regions_[head_.index_back(age)];
    }

private:
    // One array per field rather than one array of records, so that the
    // dedup scan of an insert and the scan of find read only the hashes.
    //
    // A slot holds a region the region store owns and this cache does not.
    // The slot type makes that explicit and keeps the identity read behind
    // an accessor that can report an empty slot.
    ::fixy::FixedArray<::fixy::WeakRef<const RegionNode>, CAP> regions_{};
    ::fixy::FixedArray<ContentHash, CAP> content_hashes_{};

    // A free-running counter. index() is the next slot to write and
    // index_back(i) is the i-th most recently written one.
    ::fixy::Cyclic<uint32_t, CAP> head_{};
    ::fixy::BoundedMonotonic<uint32_t, CAP> count_ = ::fixy::mint_bounded_monotonic<uint32_t, CAP>(0u);
};

}  // namespace crucible
