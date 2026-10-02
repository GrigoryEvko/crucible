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
#include <fixy/Mutation.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>

#include <array>
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
        // The contract assertion states the precondition and rejects the
        // caller in both build modes: the release preset evaluates contracts
        // under the `observe` semantic and the violation handler is noreturn.
        // It is a contract_assert and not a CRUCIBLE_PRE: under the `ignore`
        // semantic, CRUCIBLE_PRE tells the optimizer to assume its predicate,
        // and that assumption would let the optimizer delete the repeat
        // below.  A contract_assert under `ignore` gives the optimizer no
        // assumption.
        contract_assert(region != nullptr);

        // The repeat below covers the one case the assertion does not. The
        // semantic is a build option of each translation unit, and
        // CRUCIBLE_CONTRACT_IGNORE_OPTIONS sets `ignore` for some units of
        // this tree. Under `ignore`, no contract assertion in the headers of
        // such a unit checks. The read on the next line would fault on its
        // own even then, but only after this has been inlined into a caller
        // several frames up; the named check reports the contract that was
        // broken rather than an address.
        CRUCIBLE_FATAL_INVARIANT(region != nullptr);

        const ContentHash hash = region->content_hash;

        for (uint32_t i = 0; i < count_.get(); i++) {
            if (content_hashes_[head_.index_back(i)] == hash) return;
        }

        const uint32_t slot = head_.index();
        // The slot type stays nullable so an empty or evicted slot remains
        // representable.  This write binds the region itself, which the
        // check above proved non-null.
        regions_[slot] = ::fixy::WeakRef<const RegionNode>{*region};
        content_hashes_[slot] = hash;
        ops_[slot] = ::fixy::mint_tagged<::fixy::tags::source::RegionOps, const TraceEntry*>(region->ops);
        // The op count, and nothing else. It used to be written as
        // `region->plan ? region->num_ops : 0`, which overloaded it with a
        // second meaning: a planless region was stored with a count of zero
        // so that the position bound in find_alternate would reject it.
        //
        // That made a legal value do duty as a poison. A zero was both "this
        // region has no ops" and "this region had no plan when it was
        // inserted", and only the second one was recoverable -- by
        // notify_plan_ready, a repair path that no caller ever called, so
        // the entry stayed unfindable for the rest of its life in the cache.
        //
        // find_alternate now asks the region itself whether it has a plan.
        // That read is of live state rather than of a snapshot taken at
        // insert time, so a plan that arrives later needs no repair and
        // there is nothing left for a repair path to do.
        num_ops_[slot] = region->num_ops;
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
    // mode: CrucibleContext::activate returns false on a null plan. The read
    // goes through the slot's own pointer rather than through a flag written
    // at insert time, so a region whose plan is built after it was cached
    // becomes eligible on its own.
    [[nodiscard]] const RegionNode* find_alternate(uint32_t pos, SchemaHash schema, ShapeHash shape,
                                                   const RegionNode* exclude = nullptr) const CRUCIBLE_LIFETIMEBOUND
    CRUCIBLE_NO_THREAD_SAFETY {
        for (uint32_t i = 0; i < count_.get(); i++) {
            const uint32_t idx = head_.index_back(i);

            // One read of the slot, checked once. The slot type is nullable
            // by design, and every use below dereferences it.
            const RegionNode* candidate = regions_[idx].try_get();
            if (candidate == nullptr || candidate == exclude) continue;
            if (candidate->plan == nullptr) continue;
            if (pos >= num_ops_[idx]) continue;

            const TraceEntry* ops_ptr = ops_[idx].value();
            if (ops_ptr[pos].schema_hash == schema && ops_ptr[pos].shape_hash == shape) return candidate;
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
    std::array<::fixy::WeakRef<const RegionNode>, CAP> regions_{};
    std::array<ContentHash, CAP> content_hashes_{};
    // The tag records that the pointer was taken out of a region at the
    // moment it was inserted here.
    using OpsPtr = ::fixy::Tagged<const TraceEntry*, ::fixy::tags::source::RegionOps>;
    std::array<OpsPtr, CAP> ops_{};
    std::array<uint32_t, CAP> num_ops_{};

    // A free-running counter. index() is the next slot to write and
    // index_back(i) is the i-th most recently written one.
    ::fixy::Cyclic<uint32_t, CAP> head_{};
    ::fixy::BoundedMonotonic<uint32_t, CAP> count_ = ::fixy::mint_bounded_monotonic<uint32_t, CAP>(0u);

    // The wrappers add no storage, so each slot array keeps the layout of
    // the raw array it replaces and each counter the layout of a uint32_t.
    static_assert(sizeof(OpsPtr) == sizeof(const TraceEntry*) && alignof(OpsPtr) == alignof(const TraceEntry*));
    static_assert(sizeof(regions_) == CAP * sizeof(const RegionNode*));
    static_assert(sizeof(ops_) == CAP * sizeof(const TraceEntry*));
    static_assert(sizeof(head_) == sizeof(uint32_t));
    static_assert(sizeof(count_) == sizeof(uint32_t));
};

}  // namespace crucible
