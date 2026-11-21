#pragma once

// The slab heap of the allocator of the base: a pairing heap of the slabs of
// one heap, keyed by the number of a slab, with the removal of each slab
// (THE SLAB HEAP).
//
//   SlabRecords                 the slab records of one window, read through
//                               the link of a slab
//   SlabHeap                    the root of one slab heap: none, or the slab
//                               with the lowest number
//   heap.insert(records, slab)  puts a slab that is in no heap into the heap
//   heap.pop_min(records)       the lowest slab, out of the heap, or none
//   heap.remove(records, slab)  takes a slab of the heap out of the heap
//
// THE SLAB HEAP
//   A refill takes the slab with the lowest address first, so the live
//   blocks pack toward the low slabs, and the high slabs and chunks can empty
//   (design 5.8).  The partial slabs of each class and the empty slabs of
//   each slab kind are thus each one heap of slabs, keyed by the number of a
//   slab, which keeps the order of the addresses.  A flush can make each slab
//   of a heap empty, so each heap takes out each slab, not only its lowest.
//
//   The heap is a pairing heap (Fredman, Sedgewick, Sleator and Tarjan,
//   1986).  It is a tree in which each parent has a lower number than each of
//   its children.  Each slab holds three links in its SlabMeta: its first
//   child, its next sibling, and its last sibling, or its parent when it is a
//   first child.  The root has no sibling.  The steps:
//     meld      the root with the higher number becomes the first child of
//               the other root.  O(1)
//     insert    melds the slab with the root.  O(1)
//     pop_min   takes the root out, and melds its children in two passes:
//               each pair from the left, and then each melded pair from the
//               right into one tree.  O(log n) amortized
//     remove    cuts the slab out of its list of siblings, melds its children
//               in two passes, and melds the result with the root.  The root
//               is a pop_min.  O(log n) amortized
//   The links are fields of the records, so a heap allocates nothing, and a
//   slab is in one heap at most.  jemalloc keeps its extents in pairing heaps
//   too (ph.h, by recall).  The links hold the number of a slab plus one, so
//   a record of zero bytes is in no heap.
//
//   One thread changes a heap and the links of its slabs: the owner thread of
//   the HeapState that holds the root.  A slab in a heap names a record that
//   the window door started, so each link names a committed record.
//
// This header is in the layer F0_primitives of utils/rule-table.txt, beside
// foundation/alloc/Metadata.h.  src/foundation/alloc/SlabHeap.cpp holds the
// bodies.

#include <foundation/ByteSeal.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Quantity.h>

#include <cstdint>

namespace foundation::alloc {

// The slab records of one window: the address of the record of the slab 0.
// The empty records name no window: the sentinel heap holds them, and no
// path of the sentinel reads a record.  A record lives at its place in the
// region (THE RECORDS of foundation/alloc/Metadata.h), so the engine reads
// and writes it through the accessor at, and a test reads the links of a
// slab by value.
class SlabRecords final {
public:
    constexpr SlabRecords() noexcept = default;

    // The records whose first record, the record of the slab 0, is at the
    // address.  The window door gives the address (Window::slab_records_at).
    [[nodiscard]] static constexpr SlabRecords of(AddressWord first_record) noexcept {
        return SlabRecords{static_cast<std::uint64_t>(first_record)};
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return first_ == 0; }

    // The record of a linked slab.  The cold bodies of the static library
    // foundation call it, so the check is a fatal invariant in each build, as
    // in foundation/alloc/Metadata.h.  Complexity: O(1), one multiply and one
    // add.
    [[nodiscard]] SlabMeta& at(SlabLink slab) const noexcept {
        CRUCIBLE_FATAL_INVARIANT(slab != SlabLink::none);
        std::uint64_t const index = static_cast<std::uint64_t>(slab) - 1;
        return *__builtin_bit_cast(SlabMeta*, first_ + index * kSlabMetaBytes);
    }

    // The record of a slab by its number.
    [[nodiscard]] SlabMeta& at(SlabNumber slab) const noexcept { return at(link_of(slab)); }

    // The three links of a linked slab, by value: its first child, its next
    // sibling, and its last sibling or its parent.  A test of a slab heap
    // walks them.
    [[nodiscard]] SlabLink child_of(SlabLink slab) const noexcept { return at(slab).heap_child(); }
    [[nodiscard]] SlabLink next_of(SlabLink slab) const noexcept { return at(slab).heap_next(); }
    [[nodiscard]] SlabLink prev_of(SlabLink slab) const noexcept { return at(slab).heap_prev(); }

private:
    constexpr explicit SlabRecords(std::uint64_t first) noexcept : first_{first} {}

    std::uint64_t first_ = 0;
    // The assignments are user-provided, so std::bit_cast builds no records
    // from bytes (foundation/ByteSeal.h), and a copy stays trivial.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// The root of one slab heap (THE SLAB HEAP).  A new heap is empty.
class SlabHeap final {
public:
    constexpr SlabHeap() noexcept = default;

    [[nodiscard]] constexpr bool is_empty() const noexcept { return root_ == SlabLink::none; }

    // The slab with the lowest number, or none for an empty heap.
    [[nodiscard]] constexpr SlabLink min() const noexcept { return root_; }

    // Puts a slab that is in no heap into this heap.  Complexity: O(1).
    void insert(SlabRecords records, SlabLink slab) noexcept;

    // Takes the slab with the lowest number out of this heap and gives it, or
    // gives none for an empty heap.  The slab leaves with no link.
    // Complexity: O(log n) amortized over the slabs of the heap.
    [[nodiscard]] SlabLink pop_min(SlabRecords records) noexcept;

    // Takes a slab of this heap out of it.  The slab leaves with no link.
    // Complexity: O(log n) amortized over the slabs of the heap.
    void remove(SlabRecords records, SlabLink slab) noexcept;

    // The count of the slabs of this heap, by a walk of its links that keeps
    // no memory of its own: down each first child, then to the next sibling,
    // or up the last links to the parent.  The census of a heap reads it.
    // Complexity: linear in the slabs of the heap.
    [[nodiscard]] ::foundation::core::ElementCount count(SlabRecords records) const noexcept;

private:
    // Melds two roots that have no sibling, and gives the root of the
    // result.  The two roots can swap with no change of meaning.  O(1).
    [[nodiscard]] static SlabLink meld_(SlabRecords records, SlabLink first, SlabLink second) noexcept;

    // Melds a list of siblings into one tree in two passes, and gives its
    // root, or none for an empty list.
    [[nodiscard]] static SlabLink merge_siblings_(SlabRecords records, SlabLink first) noexcept;

    // The slab of a walk of count after a slab with no child.
    [[nodiscard]] static SlabLink next_in_walk_(SlabRecords records, SlabLink slab) noexcept;

    SlabLink root_{SlabLink::none};
};

}  // namespace foundation::alloc
