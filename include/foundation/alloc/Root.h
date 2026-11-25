#pragma once

// The one state of the allocator of the base for the process: the root
// (the root).
//
//   AllocRoot        the state word, the table of the windows, the host
//                    window and the heap table (the heap table)
//   alloc_root_      the root of the process
//   ensure_root()    initializes the root at its first use, and reads one
//                    state word after it
//
// Root
//   libfoundation.a links into each shared library of the product, and the
//   tree hides each symbol, so a variable of a source file would exist one
//   time in each library.  So the allocator keeps no state in a source file.
//   Each mutable word is reachable from three objects, each inline, constinit
//   and CRUCIBLE_PROCESS_WIDE: alloc_root_ of this header, and sentinel_heap_
//   and tls_heap_ of foundation/alloc/HeapState.h.
//
//   The root starts as zero bytes.  Its first use initializes it behind one
//   state word: the first thread moves the word from empty to starting,
//   reserves the host window through the window door, writes its record
//   into the host slot of the table and the chunk map into the sentinel, and
//   publishes the word as ready with release.  Each other thread spins with
//   CRUCIBLE_SPIN_PAUSE until it reads ready with acquire.  The process holds
//   the host window to its end: it is in an Immortal of foundation/core, so
//   no destructor unmaps it while code at the end of the process still frees
//   blocks.  A full window ends the process with the remedy text of the
//   window door (the full window of foundation/alloc/Window.h).
//
//   No fast path reads the window through the root.  A heap holds a copy of
//   the chunk map, the base and the slab records in its HeapState, and the
//   root is read only at the init, at a claim, and in the three doors of the
//   window of foundation/alloc/HeapState.h: the take of a chunk, the start of
//   a slab record, and the claim index for the census.
//
// Heap table
//   The root owns one reservation for the HeapState of each heap, apart from
//   the metadata region of each window (the decision of 2026-11-20): one
//   table and one id space hold the host heaps and the device heaps, with no
//   change to the layout of a window.  Slot S holds the heap of the id S + 1,
//   kHeapSlotBytes after slot S - 1, so the ids are 1 to 65,534, and the id
//   0xFFFF stays the entry of a chunk of a span.  A guard of
//   kHeapTableGuardBytes with no access lies before the first slot and after
//   the last.  The init of the root reserves the table, and it commits
//   nothing.
//
//   A slot holds the HeapState and then the bin stacks of its heap
//   (bin_stacks_at of foundation/alloc/HeapState.h).  A slot is 20 KiB, five
//   pages (the decision of 2026-11-21): the stacks take 13,760 bytes, and
//   the HeapState with the outbox and the inbox of the remote free takes
//   3,544 to 4,056 bytes, more than the 16 KiB of the first plan.  So the
//   slots of the table take 1.25 GiB of address space, and a step of 2 MiB
//   holds 102 slots.  A slot is a whole count of pages, so no two heaps share
//   a page, and the first touch of the owner thread puts each page of its
//   heap on its node.  Under AddressSanitizer the page source maps the table
//   through the C library (the sanitizers of foundation/os/PageSource.h), and
//   the table lives in an Immortal, so no end of the process unmaps it.
//
//   A claim takes the next slot with one atomic add.  The slots commit from
//   the front in steps of kHeapTableStepBytes: the claim of slot S reads the
//   front with acquire, commits each byte from the front to the end of the
//   step of S, and then raises the front to that end with release.  Each byte
//   below a front that a claim reads is thus committed.  Two claims can
//   commit one step at the same time, and the kernel gives the step the same
//   access two times.  The claim then builds the HeapState in its slot: the
//   chunk map, the base, the bytes and the slab records of the host window,
//   the entry of its id, and the three pointers of each bin.  It writes no word of the bin stacks.
//   Then it writes the cell of the thread.  A claim past the last slot ends
//   the process with the
//   count of the table, and a commit that the kernel refuses, such as enomem
//   under strict overcommit, ends it with the error.
//
// This header is in the layer F0_doors of utils/rule-table.txt, beside the
// window door.  src/foundation/alloc/Root.cpp holds the cold init.

#include <foundation/Platform.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Window.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Immortal.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/os/PageSource.h>

#include <cstddef>
#include <cstdint>

namespace foundation::alloc {

// The bytes of one slot of the heap table, the guard on each side of it, and
// the step of a commit of its slots (the heap table).  The slot is 20 KiB
// (the decision of 2026-11-21), and this constant is its one place.
inline constexpr std::size_t kHeapSlotBytes = std::size_t{20} << 10;
inline constexpr std::size_t kHeapTableGuardBytes = std::size_t{2} << 20;
inline constexpr std::size_t kHeapTableStepBytes = std::size_t{2} << 20;

// The bytes of the slots of the table, a whole count of steps.
inline constexpr std::size_t kHeapTableSlotsBytes =
    (kHeapCount * kHeapSlotBytes + (kHeapTableStepBytes - 1)) & ~(kHeapTableStepBytes - 1);

// The state word of the root (the root).
enum class RootState : std::uint32_t {
    empty = 0,
    starting = 1,
    ready = 2,
};

namespace detail {

// The one writer of the root.  src/foundation/alloc/Root.cpp defines it.
struct RootDoor;

// The init of the root at its first use, on the cold path of ensure_root.
[[gnu::cold, gnu::noinline]] void start_root_() noexcept;

}  // namespace detail

// The root of the allocator (the root).
class AllocRoot final {
public:
    constexpr AllocRoot() noexcept = default;
    AllocRoot(AllocRoot const&) = delete("the root is one object for the process");
    AllocRoot& operator=(AllocRoot const&) = delete("the root is one object for the process");

    // True after the init.  One acquire load, which is a plain load on x86-64.
    [[nodiscard]] bool is_ready() const noexcept { return state_.load_acquire() == RootState::ready; }

    // The record of the window of a slot, by value.  A slot that a window
    // fills is read after the init or after the publish of that window.
    [[nodiscard]] constexpr WindowRecord window(WindowSlot slot) const noexcept { return windows_.record(slot); }

    // The count of the slots of the heap table that claims took (the heap
    // table).
    [[nodiscard]] std::uint32_t claimed_heaps() const noexcept { return claimed_heaps_.load_acquire(); }

    // The committed bytes of the slots of the heap table, from the first
    // slot (the heap table).
    [[nodiscard]] ::foundation::core::ByteCount committed_heap_bytes() const noexcept {
        return ::foundation::core::ByteCount{committed_heap_bytes_.load_acquire()};
    }

private:
    friend struct detail::RootDoor;

    ::foundation::core::Atomic<RootState> state_{RootState::empty};
    WindowTable windows_{};
    // The host window with the words of its commits, which each heap changes
    // when it takes a chunk (the chunk source of foundation/alloc/Window.h).
    ::foundation::core::Immortal<ChunkSource> host_source_{};
    ::foundation::core::Atomic<std::uint32_t> claimed_heaps_{0};
    ::foundation::core::Atomic<std::size_t> committed_heap_bytes_{0};
    ::foundation::core::Immortal<::foundation::os::Reservation> heap_table_{};
};

// The root of the process (the root).
CRUCIBLE_PROCESS_WIDE inline constinit AllocRoot alloc_root_{};

// Initializes the root at its first use (the root).  After the init, one
// acquire load of the state word and its test.
[[gnu::always_inline]] inline void ensure_root() noexcept {
    if (!alloc_root_.is_ready()) [[unlikely]] {
        detail::start_root_();
    }
}

}  // namespace foundation::alloc
