#pragma once

// The state of a heap of the allocator of the base, the sentinel heap, and
// the cell of each thread that names its heap (THE CELL).
//
//   HeapState          the state of one heap: the biased chunk map of its
//                      window and the owner entry of its own chunks, on cache
//                      lines of its own
//   sentinel_heap_     the heap of a thread before its claim (THE SENTINEL)
//   tls_heap_          the cell of each thread: the address of its heap
//   current_heap()     the heap of the calling thread: one read of the cell
//   is_sentinel(heap)  true for the sentinel heap
//   claim_thread_heap()
//                      the heap of the calling thread after its claim
//
// THE CELL
//   tls_heap_ is a thread_local with the initial-exec model and constinit, so
//   a read is the load of its offset and one load through the thread pointer,
//   with no call of an init function of the TLS.  Its start value is the
//   address of the sentinel.  CRUCIBLE_PROCESS_WIDE makes the cell and the
//   sentinel one object for the process, so each shared library that holds
//   this header reads the same cell and the same sentinel on a thread.
//   test/test_process_wide_across_libraries.cpp holds the start value in two
//   libraries.  The cell adds 8 bytes to the static TLS of each library.
//
// THE SENTINEL
//   The sentinel starts as zero bytes: no chunk map and the owner entry none,
//   which no chunk holds.  The init of the root writes the chunk map of the
//   host window into it before it gives out the first block, because the
//   free of a block reads the map through the heap of the thread, and the
//   first call of a thread can be a free.  A free needs a block, and a block
//   needs the init, so each read of the map of the sentinel reads the map of
//   the host window.  The entry none then fails the test of a local free, so
//   the free takes its cold path, which claims a heap.
//
//   One thread writes a HeapState: its owner thread, after the claim.  The
//   init of the root writes the map of the sentinel one time, before a thread
//   can read it.
//
// This header is in the layer F0_primitives of utils/rule-table.txt, and it
// includes nothing above foundation/alloc/ChunkMap.h, so the allocation door
// of foundation/core/Heap.h can read the cell.  The door layer
// foundation/alloc/Root.h holds the root, and src/foundation/alloc/Root.cpp
// holds the one writer of a HeapState.

#include <foundation/Platform.h>
#include <foundation/alloc/ChunkMap.h>

#include <cstdint>

namespace foundation::alloc {

class HeapState;

namespace detail {

// The one writer of a HeapState.  src/foundation/alloc/Root.cpp defines it.
struct HeapStateDoor;

}  // namespace detail

// The state of one heap (THE CELL).  The map word is at offset 0 and the own
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

    // True when each field is at the offset of the design probe, and the
    // spare bytes fill the line.  The check file of this header holds it.
    [[nodiscard]] static consteval bool has_probe_shape() noexcept {
        return offsetof(HeapState, chunk_map_) == 0 && offsetof(HeapState, self_) == 8
            && offsetof(HeapState, spare_) == 10 && sizeof(spare_) == 54;
    }

private:
    friend struct detail::HeapStateDoor;

    BiasedChunkMap chunk_map_{};
    ChunkOwner self_{ChunkOwner::none};
    std::uint8_t spare_[54]{};
};

// The heap of each thread before its claim (THE SENTINEL).
CRUCIBLE_PROCESS_WIDE inline constinit HeapState sentinel_heap_{};

// The cell of each thread (THE CELL).
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
// body (THE HEAP TABLE of foundation/alloc/Root.h).
[[nodiscard, gnu::cold, gnu::noinline]] HeapState& claim_thread_heap() noexcept;

}  // namespace foundation::alloc
