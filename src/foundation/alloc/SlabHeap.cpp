// The bodies of the slab heap of the allocator of the base
// (foundation/alloc/SlabHeap.h, THE SLAB HEAP).  This file holds no variable:
// each link is a field of a record of the metadata region.

#include <foundation/alloc/SlabHeap.h>

#include <foundation/Platform.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/core/Quantity.h>

#include <cstddef>
#include <cstdint>

namespace foundation::alloc {

namespace {

// Takes the next sibling of a slab off it, and gives that sibling.  The slab
// keeps no sibling link.
[[nodiscard]] SlabLink detach_next(SlabMeta& meta) noexcept {
    SlabLink const next = meta.heap_next();
    meta.set_heap_next(SlabLink::none);
    meta.set_heap_prev(SlabLink::none);
    return next;
}

}  // namespace

// The root of the result has no sibling either: the root with the higher
// number becomes the first child of the other.
SlabLink SlabHeap::meld_(SlabRecords records, SlabLink first, SlabLink second) noexcept {
    bool const is_first_lower = first < second;
    SlabLink const winner = is_first_lower ? first : second;
    SlabLink const loser = is_first_lower ? second : first;
    SlabMeta& top = records.at(winner);
    SlabMeta& child = records.at(loser);
    SlabLink const old_child = top.heap_child();
    child.set_heap_prev(winner);
    child.set_heap_next(old_child);
    if (old_child != SlabLink::none) records.at(old_child).set_heap_prev(loser);
    top.set_heap_child(loser);
    return winner;
}

// The root has no sibling.  Complexity: linear in the siblings, O(log n)
// amortized over the heap.
SlabLink SlabHeap::merge_siblings_(SlabRecords records, SlabLink first) noexcept {
    if (first == SlabLink::none) return SlabLink::none;
    // The first pass melds each pair from the left.  The melded roots form a
    // list through their next links, the last pair first.
    SlabLink pairs = SlabLink::none;
    SlabLink cursor = first;
    while (cursor != SlabLink::none) {
        SlabLink const right = detach_next(records.at(cursor));
        SlabLink melded = cursor;
        SlabLink after = SlabLink::none;
        if (right != SlabLink::none) {
            after = detach_next(records.at(right));
            melded = meld_(records, cursor, right);
        }
        records.at(melded).set_heap_next(pairs);
        pairs = melded;
        cursor = after;
    }
    // The second pass melds each pair into the tree from the right.
    SlabLink root = pairs;
    SlabLink rest = detach_next(records.at(root));
    while (rest != SlabLink::none) {
        SlabLink const after = detach_next(records.at(rest));
        root = meld_(records, root, rest);
        rest = after;
    }
    return root;
}

// The next slab is the next sibling, or the next sibling of the nearest
// ancestor that has one, or none at the end of the walk.  The parent of a
// list of siblings is the last link of its first child, so the climb walks
// the list back one time.
SlabLink SlabHeap::next_in_walk_(SlabRecords records, SlabLink slab) noexcept {
    SlabLink current = slab;
    for (;;) {
        SlabMeta const& meta = records.at(current);
        if (meta.heap_next() != SlabLink::none) return meta.heap_next();
        SlabLink child = current;
        SlabLink prev = meta.heap_prev();
        while (prev != SlabLink::none && records.at(prev).heap_child() != child) {
            child = prev;
            prev = records.at(prev).heap_prev();
        }
        if (prev == SlabLink::none) return SlabLink::none;
        current = prev;
    }
}

core::ElementCount SlabHeap::count(SlabRecords records) const noexcept {
    std::size_t total = 0;
    SlabLink slab = root_;
    while (slab != SlabLink::none) {
        ++total;
        SlabLink const child = records.at(slab).heap_child();
        slab = child != SlabLink::none ? child : next_in_walk_(records, slab);
    }
    return core::ElementCount{total};
}

void SlabHeap::insert(SlabRecords records, SlabLink slab) noexcept {
    SlabMeta const& meta = records.at(slab);
    // A slab in a heap has a link, unless it is the root of a heap of one
    // slab, so each check is a fatal invariant in each build.
    CRUCIBLE_FATAL_INVARIANT(meta.heap_child() == SlabLink::none);
    CRUCIBLE_FATAL_INVARIANT(meta.heap_next() == SlabLink::none);
    CRUCIBLE_FATAL_INVARIANT(meta.heap_prev() == SlabLink::none);
    CRUCIBLE_FATAL_INVARIANT(slab != root_);
    root_ = root_ == SlabLink::none ? slab : meld_(records, root_, slab);
}

SlabLink SlabHeap::pop_min(SlabRecords records) noexcept {
    SlabLink const top = root_;
    if (top == SlabLink::none) return SlabLink::none;
    SlabMeta& meta = records.at(top);
    root_ = merge_siblings_(records, meta.heap_child());
    meta.set_heap_child(SlabLink::none);
    return top;
}

void SlabHeap::remove(SlabRecords records, SlabLink slab) noexcept {
    CRUCIBLE_FATAL_INVARIANT(slab != SlabLink::none);
    if (slab == root_) {
        static_cast<void>(pop_min(records));
        return;
    }
    SlabMeta& meta = records.at(slab);
    SlabLink const prev = meta.heap_prev();
    SlabLink const next = meta.heap_next();
    // A slab of the heap below the root has a parent or a last sibling.
    CRUCIBLE_FATAL_INVARIANT(prev != SlabLink::none);
    SlabMeta& prev_meta = records.at(prev);
    if (prev_meta.heap_child() == slab) {
        prev_meta.set_heap_child(next);
    } else {
        prev_meta.set_heap_next(next);
    }
    if (next != SlabLink::none) records.at(next).set_heap_prev(prev);
    SlabLink const children = merge_siblings_(records, meta.heap_child());
    meta.set_heap_child(SlabLink::none);
    meta.set_heap_next(SlabLink::none);
    meta.set_heap_prev(SlabLink::none);
    if (children != SlabLink::none) root_ = meld_(records, root_, children);
}

}  // namespace foundation::alloc
