#pragma once

// The state of a heap of the allocator of the base, its bins and its fast
// paths, the sentinel heap, and the cell of each thread that names its heap
// (the cell).
//
//   HeapState          the state of one heap: the biased chunk map of its
//                      window, the owner entry of its own chunks, a bin for
//                      each class, and the slab heaps of its slabs, on cache
//                      lines of its own
//   heap.allocate(c)   one block of the class c from its bin (the fast paths)
//   heap.release(c, b) the block b of the class c back into its bin
//   heap.census()      the counts of the chunks, slabs and blocks of the heap
//                      (the census, a testing door)
//   heap.flush_bins()  gives each block of each bin back to its slab
//   bin_stacks_at(s)   the address of the bin stacks of the heap whose slot
//                      starts at s (the bins)
//   sentinel_heap_     the heap of a thread before its claim (the sentinel)
//   tls_heap_          the cell of each thread: the address of its heap
//   current_heap()     the heap of the calling thread: one read of the cell
//   is_sentinel(heap)  true for the sentinel heap
//   claim_thread_heap()
//                      the heap of the calling thread after its claim
//   thread_heap()      the same heap, with the claim on a cold branch
//
// Bins
//   A bin is a stack of the addresses of free blocks of one class: three
//   pointers into the bin stacks of the heap.  The stack of a bin is the run
//   [full_begin, empty_end), and its blocks are the run [head, empty_end).
//   An allocation takes the block at head and moves head up.  A release
//   moves head down and writes the block there.  So the bin is empty when
//   head is empty_end, and full when head is full_begin.  A bin holds
//   bin_capacity(c) addresses (foundation/alloc/Layout.h): 128 for each
//   class to 128 bytes, then the slots of 16 KiB, and at least two.  The
//   stacks of the classes 1 to 64 hold 1,720 addresses, 13,760 bytes.
//   The words of a stack are AddressWords: no path reads through one.
//
//   The bin stacks sit in the slot of the heap in the heap table, after the
//   HeapState (the decision of 2026-11-20).  bin_stacks_at is the one door
//   that gives their address from the address of the slot, so a change of
//   their place is a change of one function.  A claim writes the HeapState
//   and the three pointers of each bin, and no word of a stack: the pages of
//   the stacks stay the zero pages of the kernel until the first refill or
//   release of their class.
//
// Fast paths
//   Each fast path is inline, with one compare for each branch, because
//   -fharden-compares doubles a compound condition (design 6, PF2):
//     allocate  load head, compare it with empty_end, branch to the cold
//               refill, load the block, add 8, store head
//     release   load head, compare it with full_begin, branch to the cold
//               flush, store the block at head minus 8, store head
//   No fast path takes an atomic, a lock or a system call.  The class of a
//   typed door is a constant, so the offset of its bin folds into each load.
//
//   The refill of an empty bin takes half a bin: from the partial slab of
//   the class with the lowest address, by a scan of its summary word and its
//   bitmap words with tzcnt and blsr, then from the next one, then from a
//   fresh slab.  A fresh slab comes from the empty slabs of its slab kind,
//   then from the next unit of the chunk of small slabs of the heap, or from
//   a chunk of the window.  The flush of a full bin gives back its older
//   half, so the hot top of the stack stays.  It takes the blocks one slab
//   at a time and writes each word of a bitmap, the summary and the free
//   count of a slab one time (the give-back of a run of Flush.cpp).  It ends
//   the process when an address is outside the window of the heap, when its
//   chunk has another owner, when its slab holds another class, when it is
//   not the first byte of a slot, and when its slot is free already (a
//   second release).  These checks see only an address that reaches a
//   flush.  The types of the typed door forbid each of these releases, and
//   the checked mode finds each one at the release itself (design 12).
//
//   A slab with no live block goes to the empty slabs of its kind at once,
//   so each slab heap holds only the slabs of its state.  A chunk with no
//   live block stays with the heap: the node lists give chunks back.
//
// Census
//   census() walks the chunks of the window that the heap owns, and the
//   slabs of each, and it counts the live slots, the empty, partial and full
//   slabs, the slabs of each slab heap and the blocks in the bins.  A test
//   reads it after each step.  It writes nothing, and only the owner thread
//   calls it.  It reads the map entry of each claimed chunk with a plain
//   load, also an entry that another heap writes, so its counts hold only
//   when no other thread takes a chunk during the call.
//
// Cell
//   tls_heap_ is a thread_local with the initial-exec model and constinit, so
//   a read is the load of its offset and one load through the thread pointer,
//   with no call of an init function of the TLS.  Its start value is the
//   address of the sentinel.  CRUCIBLE_PROCESS_WIDE makes the cell and the
//   sentinel one object for the process, so each shared library that holds
//   this header reads the same cell and the same sentinel on a thread.
//   test/test_process_wide_across_libraries.cpp holds the start value in two
//   libraries.  The cell adds 8 bytes to the static TLS of each library.
//
// Sentinel
//   The sentinel starts as zero bytes: no chunk map, the owner entry none,
//   which no chunk holds, and three null pointers in each bin, so each bin
//   is empty and full at once.  The first allocation and the first release
//   of a thread thus take a cold path, with no test of null on a fast path,
//   and the cold path claims a heap and runs the call again on it.  The init
//   of the root writes the chunk map of the host window into the sentinel
//   before it gives out the first block, because the free of a block reads
//   the map through the heap of the thread, and the first call of a thread
//   can be a free.
//
//   One thread writes a HeapState: its owner thread, after the claim.  The
//   init of the root writes the map of the sentinel one time, before a thread
//   can read it.
//
// This header is in the layer F0_primitives of utils/rule-table.txt, and it
// includes nothing above foundation/alloc/SlabHeap.h, so the allocation door
// of foundation/core/Heap.h can read the cell and the fast paths.  The door
// layer foundation/alloc/Root.h holds the root, and
// src/foundation/alloc/Root.cpp holds the claim and the three doors of the
// window.  src/foundation/alloc/Refill.cpp and src/foundation/alloc/Flush.cpp
// hold the cold bodies.

#include <foundation/Platform.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Layout.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/alloc/SlabHeap.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Quantity.h>

#include <cstddef>
#include <cstdint>

namespace foundation::alloc {

class HeapState;

namespace detail {

// The one writer of the fields of a new HeapState.  src/foundation/alloc/Root.cpp
// defines it.
struct HeapStateDoor;

// The doors of the window of a heap for the cold bodies of the refill and the
// census.  src/foundation/alloc/Root.cpp holds each body, which reads the host
// window and its commits in the root.  No fast path calls one.
//   claim_window_chunk_(owner)       a chunk of the window for the heap of
//                                    the entry: claimed, its extent and the
//                                    records of its 32 units committed, its
//                                    record started with the owner, and its
//                                    owner entry written.  A full window or a
//                                    refusal of the kernel ends the process
//   start_window_slab_(chunk, unit)  the record of a unit of a chunk of the
//                                    heap, in zero bytes
//   claimed_window_chunks_()         the chunks below the claim index of the
//                                    window
[[nodiscard, gnu::cold]] ChunkIndex claim_window_chunk_(ChunkOwner owner) noexcept;
[[gnu::cold]] void start_window_slab_(ChunkIndex chunk, UnitIndex unit) noexcept;
[[nodiscard, gnu::cold]] ::foundation::core::ElementCount claimed_window_chunks_() noexcept;

// The first word of the stack of each class in the bin stacks, and the count
// of the words of each stack before it, at the index of the class.  Index 0
// names no class, and kSizeClassCount + 1 holds the words of all stacks.
struct BinStackLayout final {
    std::uint32_t first[kSizeClassCount + 2]{};
};

[[nodiscard]] consteval BinStackLayout build_bin_stacks_() noexcept {
    BinStackLayout layout{};
    for (std::uint32_t value = 1; value <= kSizeClassCount; ++value) {
        layout.first[value + 1] = layout.first[value] + bin_capacity(SizeClass{static_cast<std::uint8_t>(value)});
    }
    return layout;
}

inline constexpr BinStackLayout kBinStacks = build_bin_stacks_();

}  // namespace detail

// The bytes of the bin stacks of one heap: 1,720 addresses (the bins).
inline constexpr std::size_t kBinStackBytes = std::size_t{detail::kBinStacks.first[kSizeClassCount + 1]} * 8;

// True for a class with a slab, 1 to 52: the classes that the refill and the
// flush serve.  The large runs serve the classes 53 to 64.
[[nodiscard]] constexpr bool is_slab_class(SizeClass size_class) noexcept {
    return is_size_class(size_class) && static_cast<std::uint8_t>(size_class) <= kLastMediumClass;
}

// The counts of one heap (the census).
class HeapCensus final {
public:
    constexpr HeapCensus() noexcept = default;

    // The chunks that the heap owns, and the slabs of those chunks.
    [[nodiscard]] constexpr ::foundation::core::ElementCount chunks() const noexcept { return chunks_; }
    [[nodiscard]] constexpr ::foundation::core::ElementCount slabs() const noexcept { return slabs_; }
    // The slots of the slabs that are not free in their bitmaps: the live
    // blocks and the blocks in the bins.
    [[nodiscard]] constexpr ::foundation::core::ElementCount taken_slots() const noexcept { return taken_slots_; }
    // The blocks in the bins.
    [[nodiscard]] constexpr ::foundation::core::ElementCount binned_blocks() const noexcept { return binned_blocks_; }
    // The slabs with each slot free, with some slots free, and with none.
    [[nodiscard]] constexpr ::foundation::core::ElementCount empty_slabs() const noexcept { return empty_slabs_; }
    [[nodiscard]] constexpr ::foundation::core::ElementCount partial_slabs() const noexcept { return partial_slabs_; }
    [[nodiscard]] constexpr ::foundation::core::ElementCount full_slabs() const noexcept { return full_slabs_; }
    // The slabs in the empty slab heaps, and in the partial slab heaps.
    [[nodiscard]] constexpr ::foundation::core::ElementCount empty_heap_slabs() const noexcept {
        return empty_heap_slabs_;
    }
    [[nodiscard]] constexpr ::foundation::core::ElementCount partial_heap_slabs() const noexcept {
        return partial_heap_slabs_;
    }
    // The chunks with a slot that is not free.
    [[nodiscard]] constexpr ::foundation::core::ElementCount chunks_with_taken_slots() const noexcept {
        return chunks_with_taken_slots_;
    }
    // The slabs whose bitmap does not agree with their free count or their
    // summary word, or holds a set bit past the slots of their class.
    [[nodiscard]] constexpr ::foundation::core::ElementCount slabs_with_wrong_bitmap() const noexcept {
        return slabs_with_wrong_bitmap_;
    }

private:
    friend class HeapState;

    ::foundation::core::ElementCount chunks_{std::size_t{0}};
    ::foundation::core::ElementCount slabs_{std::size_t{0}};
    ::foundation::core::ElementCount taken_slots_{std::size_t{0}};
    ::foundation::core::ElementCount binned_blocks_{std::size_t{0}};
    ::foundation::core::ElementCount empty_slabs_{std::size_t{0}};
    ::foundation::core::ElementCount partial_slabs_{std::size_t{0}};
    ::foundation::core::ElementCount full_slabs_{std::size_t{0}};
    ::foundation::core::ElementCount empty_heap_slabs_{std::size_t{0}};
    ::foundation::core::ElementCount partial_heap_slabs_{std::size_t{0}};
    ::foundation::core::ElementCount chunks_with_taken_slots_{std::size_t{0}};
    ::foundation::core::ElementCount slabs_with_wrong_bitmap_{std::size_t{0}};
};

// The state of one heap (the cell).  The map word is at offset 0 and the own
// entry at offset 8, the shape of the free test of the design probe.  A state
// lives at its place, so it does not copy or move.
class alignas(64) HeapState final {
public:
    constexpr HeapState() noexcept = default;
    HeapState(HeapState const&) = delete("a heap state lives at its place: the cell of a thread names it");
    HeapState& operator=(HeapState const&) = delete("a heap state lives at its place");

    // The biased chunk map of the window of the heap: a copy of the map of
    // the window record, so a free reads no root.
    [[nodiscard]] constexpr BiasedChunkMap chunk_map() const noexcept { return chunk_map_; }

    // The owner entry of the chunks of this heap, or none for the sentinel.
    [[nodiscard]] constexpr ChunkOwner self() const noexcept { return self_; }

    // One block of a class with a slab, from the bin of the class (the fast
    // paths).  Complexity: O(1), six instructions with a constant class.  An
    // empty bin takes the cold refill.  The check of the class is a fatal
    // invariant, because the cold bodies of the static library call the fast
    // paths, and it folds away for a constant class.
    [[nodiscard, gnu::always_inline]] AddressWord allocate(SizeClass size_class) noexcept {
        CRUCIBLE_FATAL_INVARIANT(is_size_class(size_class));
        Bin& bin = bins_[static_cast<std::uint8_t>(size_class)];
        AddressWord* const head = bin.head;
        if (head == bin.empty_end) [[unlikely]] {
            return refill_(size_class);
        }
        bin.head = head + 1;
        return *head;
    }

    // A block of the class back into the bin of the class (the fast paths).
    // The block came from allocate of this heap with the same class.
    // Complexity: O(1), six instructions with a constant class.  A full bin
    // takes the cold flush.
    [[gnu::always_inline]] void release(SizeClass size_class, AddressWord block) noexcept {
        CRUCIBLE_FATAL_INVARIANT(is_size_class(size_class));
        Bin& bin = bins_[static_cast<std::uint8_t>(size_class)];
        AddressWord* head = bin.head;
        if (head == bin.full_begin) [[unlikely]] {
            flush_(size_class, block);
            return;
        }
        --head;
        *head = block;
        bin.head = head;
    }

    // The counts of the heap (the census).  Only the owner thread calls it.
    // Complexity: linear in the chunks of the window that claims took, and in
    // the slabs of the heap.
    [[nodiscard, gnu::cold, gnu::noinline]] HeapCensus census() const noexcept;

    // Gives each block of each bin back to its slab, with each check of a
    // flush, and leaves each bin empty.  Complexity: linear in the blocks of
    // the bins.
    [[gnu::cold, gnu::noinline]] void flush_bins() noexcept;

    // True when each field is at the offset of its design, and the spare
    // bytes fill the last line.  The check file of this header holds it.
    [[nodiscard]] static consteval bool has_probe_shape() noexcept {
        return offsetof(HeapState, chunk_map_) == 0 && offsetof(HeapState, self_) == 8
            && offsetof(HeapState, self_padding_) == 10 && offsetof(HeapState, small_units_) == 12
            && offsetof(HeapState, small_chunk_) == 16 && offsetof(HeapState, chunks_taken_) == 20
            && offsetof(HeapState, window_base_) == 24 && offsetof(HeapState, window_bytes_) == 32
            && offsetof(HeapState, slab_records_) == 40 && offsetof(HeapState, header_spare_) == 48
            && sizeof(header_spare_) == 16 && offsetof(HeapState, bins_) == 64
            && offsetof(HeapState, partial_) == 64 + 24 * (kSizeClassCount + 1)
            && offsetof(HeapState, empty_small_) == offsetof(HeapState, partial_) + 4 * (kLastMediumClass + 1)
            && offsetof(HeapState, empty_medium_) == offsetof(HeapState, empty_small_) + 4
            && offsetof(HeapState, spare_) == offsetof(HeapState, empty_medium_) + 4
            && offsetof(HeapState, spare_) + sizeof(spare_) == sizeof(HeapState);
    }

private:
    friend struct detail::HeapStateDoor;

    // One bin (the bins).  The three pointers of a new bin are null, so it
    // is empty and full at once.
    struct Bin final {
        AddressWord* head = nullptr;
        AddressWord* empty_end = nullptr;
        AddressWord* full_begin = nullptr;
    };

    // The cold body of an empty bin: claims a heap for the sentinel, or
    // takes half a bin from the slabs of the class (Refill.cpp).
    [[nodiscard, gnu::cold, gnu::noinline]] AddressWord refill_(SizeClass size_class) noexcept;

    // The body of a full bin: claims a heap for the sentinel, or gives back
    // the older half of the bin and pushes the block (Flush.cpp).  A stream
    // of releases runs it once for each half bin, so it is speed code with no
    // cold mark: a cold body is code for size, and its copy of the newer half
    // takes two branches for each word.  The fast path calls it on an
    // unlikely branch.
    [[gnu::noinline]] void flush_(SizeClass size_class, AddressWord block) noexcept;

    // Takes up to wanted free slots of the partial slab of the class,
    // lowest first, into the words from first, and gives the count.  A slab
    // that has no free slot after it leaves the partial heap (Refill.cpp).
    [[nodiscard]] ::foundation::core::ElementCount take_slots_(SizeClass size_class, SlabLink slab, AddressWord* first,
                                                               ::foundation::core::ElementCount wanted) noexcept;

    // A slab of the class with each slot free, in the partial heap of the
    // class: an empty slab of its kind, or a fresh one (Refill.cpp).
    [[nodiscard]] SlabLink start_slab_(SizeClass size_class) noexcept;

    // The next unit of the chunk of small slabs as a fresh slab, and a fresh
    // medium slab of a whole chunk (Refill.cpp).
    [[nodiscard]] SlabLink start_small_unit_() noexcept;
    [[nodiscard]] SlabLink start_medium_chunk_() noexcept;

    // Gives the count blocks of the class from first back to their slabs, one
    // slab at a time, with the checks of the fast paths, and uses the words
    // of the run as scratch (the give-back of a run of Flush.cpp).
    void give_back_run_(SizeClass size_class, AddressWord* first, ::foundation::core::ElementCount count) noexcept;

    BiasedChunkMap chunk_map_{};
    ChunkOwner self_{ChunkOwner::none};
    std::uint16_t self_padding_{0};
    // The units of the chunk of small slabs that a slab took.  A new heap
    // has no such chunk, so its count is full.
    std::uint32_t small_units_{kUnitsPerChunk};
    ChunkIndex small_chunk_{0};
    std::uint32_t chunks_taken_{0};
    // A copy of the base and of the bytes of the window, and the records of
    // its slabs, so a flush reads no root.
    AddressWord window_base_{};
    std::uint64_t window_bytes_{0};
    SlabRecords slab_records_{};
    std::uint8_t header_spare_[16]{};
    // The bin of each class, at the index of the class.  Index 0 names no
    // class.
    Bin bins_[kSizeClassCount + 1]{};
    // The partial slabs of each class with a slab, at the index of the
    // class, and the empty slabs of each slab kind (the slab heap of
    // foundation/alloc/SlabHeap.h).
    SlabHeap partial_[kLastMediumClass + 1]{};
    SlabHeap empty_small_{};
    SlabHeap empty_medium_{};
    // The bytes to the end of the last cache line.  The outbox and the
    // inbox of the remote free take more lines here, inside the 20 KiB slot.
    std::uint8_t spare_[76]{};
};

// The address of the bin stacks of the heap whose slot of the heap table
// starts at slot: the first byte after its HeapState (the bins).  This is the
// one door of their place.  Complexity: O(1).
[[nodiscard]] constexpr AddressWord bin_stacks_at(AddressWord slot) noexcept {
    return AddressWord{static_cast<std::uint64_t>(slot) + sizeof(HeapState)};
}

// The heap of each thread before its claim (the sentinel).
CRUCIBLE_PROCESS_WIDE inline constinit HeapState sentinel_heap_{};

// The cell of each thread (the cell).
CRUCIBLE_PROCESS_WIDE
[[gnu::tls_model("initial-exec")]] inline constinit thread_local HeapState* tls_heap_ = &sentinel_heap_;

// The heap of the calling thread: the sentinel before its claim.
// Complexity: O(1), one read of the cell.
[[nodiscard, gnu::always_inline]] inline HeapState& current_heap() noexcept { return *tls_heap_; }

[[nodiscard]] inline bool is_sentinel(HeapState const& heap) noexcept { return &heap == &sentinel_heap_; }

// The heap of the calling thread after its claim: a thread on the sentinel
// takes a fresh heap of the heap table of the root and writes the cell, and
// a thread that holds a heap gets it back.  The cold path of each fast path
// that meets the sentinel calls it.  src/foundation/alloc/Root.cpp holds the
// body (the heap table of foundation/alloc/Root.h).
[[nodiscard, gnu::cold, gnu::noinline]] HeapState& claim_thread_heap() noexcept;

// The heap of the calling thread after its claim: one read of the cell, and
// the cold claim only for a thread on the sentinel.  GCC compiles a function
// that calls a cold function on each path as cold code, and each body that
// it calls too, so a door that opens a scope reads its heap here.
// Complexity: O(1).
[[nodiscard, gnu::always_inline]] inline HeapState& thread_heap() noexcept {
    HeapState& heap = current_heap();
    if (is_sentinel(heap)) [[unlikely]] {
        return claim_thread_heap();
    }
    return heap;
}

}  // namespace foundation::alloc
