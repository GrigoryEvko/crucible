// The cold bodies of the root of the allocator of the base
// (foundation/alloc/Root.h) and the one writer of a HeapState
// (foundation/alloc/HeapState.h).  This file holds no variable: each mutable
// word is a field of the three process-wide objects of the two headers.

#include <foundation/alloc/Root.h>

#include <foundation/Platform.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Layout.h>
#include <foundation/alloc/SlabHeap.h>
#include <foundation/alloc/Window.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Report.h>
#include <foundation/core/Side.h>
#include <foundation/os/Errno.h>
#include <foundation/os/PageSource.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace foundation::alloc::detail {

struct HeapStateDoor {
    static void set_chunk_map(HeapState& heap, BiasedChunkMap chunk_map) noexcept { heap.chunk_map_ = chunk_map; }

    // Builds the state of a heap at the first byte of its committed slot,
    // with the facts of its window and the three pointers of each bin into
    // the bin stacks of the slot (THE BINS of foundation/alloc/HeapState.h).
    // It writes the HeapState, and no word of the stacks.
    [[nodiscard]] static HeapState& start(AddressWord slot, HeapId id, Window const& window) noexcept {
        void* const place = __builtin_bit_cast(void*, static_cast<std::uint64_t>(slot));
        HeapState* const heap = ::new(place) HeapState{};
        WindowRecord const record = window.record();
        heap->chunk_map_ = record.chunk_map();
        heap->self_ = owner_entry_of(id);
        heap->window_base_ = record.base();
        heap->window_bytes_ = record.length().raw();
        heap->slab_records_ = SlabRecords::at(window.slab_records_at());
        auto* const stacks = __builtin_bit_cast(AddressWord*, static_cast<std::uint64_t>(bin_stacks_at(slot)));
        for (std::uint32_t value = 1; value <= kSizeClassCount; ++value) {
            HeapState::Bin& bin = heap->bins_[value];
            bin.full_begin = stacks + detail::kBinStacks.first[value];
            bin.empty_end = stacks + detail::kBinStacks.first[value + 1];
            bin.head = bin.empty_end;
        }
        return *heap;
    }
};

namespace {

// The bytes from the first slot up to the end of the step that holds the end
// of BYTES (THE HEAP TABLE).
[[nodiscard]] constexpr std::size_t whole_steps(core::ByteCount bytes) noexcept {
    return (bytes.raw() + (kHeapTableStepBytes - 1)) & ~(kHeapTableStepBytes - 1);
}

[[nodiscard]] os::RangeStart start_at(core::ByteCount offset) noexcept {
    return core::side_as<os::side::RangeStart>(offset);
}

}  // namespace

struct RootDoor {
    // The init of THE ROOT, on the thread that moved the state word to
    // starting.
    static void start(AllocRoot& root) noexcept {
        WindowDiagnostic diagnostic{};
        core::Result<Window, WindowFull> reserved = reserve_window(core::to(diagnostic));
        if (reserved.is_err()) {
            core::fatal("{}", diagnostic.text());
        }
        root.host_window_.fill(std::move(reserved).expect("the ladder gave the host window"));
        publish_host_window(*root.host_window_.get(), root.windows_);
        core::ByteCount const table_bytes{kHeapTableGuardBytes + kHeapTableSlotsBytes + kHeapTableGuardBytes};
        core::Result<os::Reservation, os::Errno> table = os::reserve(table_bytes, os::ReserveAlignment::huge_page);
        if (table.is_err()) {
            core::fatal("foundation: the root of the allocator got no address space for its heap table of {} bytes, "
                        "with {}.  Raise the address-space limit of the process (ulimit -v), and start it again.",
                        table_bytes.raw(), core::enum_name_text(table.err().value_or(os::Errno::enomem)));
        }
        root.heap_table_.fill(std::move(table).expect("the page source gave the heap table"));
        HeapStateDoor::set_chunk_map(sentinel_heap_, root.windows_.host().chunk_map());
        root.state_.store_release(RootState::ready);
    }

    [[nodiscard]] static bool claim_start(AllocRoot& root) noexcept {
        return root.state_.cas_acq_rel(RootState::empty, RootState::starting).is_ok();
    }

    // Commits the slots from the front of the committed bytes up to the end
    // of the step of END, and raises the front (THE HEAP TABLE).
    static void commit_slots(AllocRoot& root, core::ByteCount end) noexcept {
        std::size_t const front = root.committed_heap_bytes_.load_acquire();
        if (end.raw() <= front) return;
        std::size_t const step_end = whole_steps(end);
        os::Reservation& table = *root.heap_table_.get();
        os::ReservedRange const range =
            table.range(start_at(core::ByteCount{kHeapTableGuardBytes + front}), core::ByteCount{step_end - front})
                .expect("the steps of the slots lie inside the heap table");
        core::Result<os::CommittedRange, os::Errno> const committed = table.commit(range);
        if (committed.is_err()) {
            core::fatal("foundation: the kernel refused the memory of the state of one more heap, with {}",
                        core::enum_name_text(committed.err().value_or(os::Errno::enomem)));
        }
        static_cast<void>(root.committed_heap_bytes_.fetch_max_acq_rel(step_end));
    }

    // The first byte of the slot of a heap.
    [[nodiscard]] static AddressWord slot_address(AllocRoot& root, HeapSlot slot) noexcept {
        os::Reservation& table = *root.heap_table_.get();
        std::uint64_t const first = table.address_of(table.whole());
        return AddressWord{first + kHeapTableGuardBytes + std::uint64_t{slot.raw()} * kHeapSlotBytes};
    }

    // The claim of THE HEAP TABLE for a thread on the sentinel.
    [[nodiscard]] static HeapState& claim(AllocRoot& root) noexcept {
        std::uint32_t const taken = root.claimed_heaps_.fetch_add_acq_rel(1);
        core::Option<HeapSlot> const slot = HeapSlot::check(taken);
        if (slot.is_none()) {
            core::fatal("foundation: the heap table of the allocator holds {} heaps, and each one has an owner, so "
                        "the thread gets no heap",
                        kHeapCount);
        }
        HeapSlot const index = core::Option<HeapSlot>{slot}.value_or(HeapSlot{core::StaticIndex<0>{}});
        commit_slots(root, core::ByteCount{(std::size_t{index.raw()} + 1) * kHeapSlotBytes});
        HeapState& heap = HeapStateDoor::start(slot_address(root, index), heap_id_of(index), *root.host_window_.get());
        tls_heap_ = &heap;
        return heap;
    }

    // The take of a chunk of the host window for the heap of the entry (the
    // doors of the window of foundation/alloc/HeapState.h).
    [[nodiscard]] static ChunkIndex claim_chunk(AllocRoot& root, ChunkOwner owner) noexcept {
        Window& window = *root.host_window_.get();
        WindowCommits& commits = root.host_commits_;
        core::Option<ChunkIndex> const claimed = window.claim_chunk(commits);
        if (claimed.is_none()) {
            core::fatal(
                "foundation: the window of the allocator holds no free chunk for the heap {}: a claim took each "
                "of its {} chunks.  The window is the address-space limit of the allocator.  Raise the "
                "address-space limit of the process (ulimit -v), and start it again.",
                std::uint32_t{static_cast<std::uint16_t>(owner)}, window.record().chunk_count());
        }
        ChunkIndex const chunk = core::Option<ChunkIndex>{claimed}.value_or(ChunkIndex{0});
        require_commit(window.commit_extent_of(commits, chunk));
        require_commit(window.commit_slab_records_of(commits, chunk));
        auto started = window.start_chunk_meta(commits, chunk);
        if (started.is_err()) {
            core::fatal("foundation: the kernel refused the memory of the record of a chunk of the allocator, with {}",
                        core::enum_name_text(started.err().value_or(os::Errno::enomem)));
        }
        auto record = std::move(started).expect("the record of the chunk is started");
        record->set_owner(owner);
        record->set_state(ChunkState::owned);
        window.set_owner(chunk, owner);
        return chunk;
    }

    static void start_slab(AllocRoot& root, ChunkIndex chunk, UnitIndex unit) noexcept {
        auto started = root.host_window_.get()->start_slab_meta(root.host_commits_, chunk, unit);
        if (started.is_err()) {
            core::fatal("foundation: the kernel refused the memory of the record of a slab of the allocator, with {}",
                        core::enum_name_text(started.err().value_or(os::Errno::enomem)));
        }
    }

    [[nodiscard]] static core::ElementCount claimed_chunks(AllocRoot& root) noexcept {
        return root.host_window_.get()->claimed_chunks(root.host_commits_);
    }

private:
    // Ends the process for a commit of the path of a chunk that the kernel
    // refused, such as enomem under strict overcommit.
    static void require_commit(core::Result<core::Unit, os::Errno> const& committed) noexcept {
        if (committed.is_err()) {
            core::fatal("foundation: the kernel refused the memory of a chunk of the allocator, with {}",
                        core::enum_name_text(committed.err().value_or(os::Errno::enomem)));
        }
    }
};

ChunkIndex claim_window_chunk_(ChunkOwner owner) noexcept { return RootDoor::claim_chunk(alloc_root_, owner); }

void start_window_slab_(ChunkIndex chunk, UnitIndex unit) noexcept { RootDoor::start_slab(alloc_root_, chunk, unit); }

core::ElementCount claimed_window_chunks_() noexcept { return RootDoor::claimed_chunks(alloc_root_); }

void start_root_() noexcept {
    AllocRoot& root = alloc_root_;
    if (RootDoor::claim_start(root)) {
        RootDoor::start(root);
        return;
    }
    while (!root.is_ready()) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

}  // namespace foundation::alloc::detail

namespace foundation::alloc {

HeapState& claim_thread_heap() noexcept {
    HeapState& current = current_heap();
    if (!is_sentinel(current)) return current;
    ensure_root();
    return detail::RootDoor::claim(alloc_root_);
}

}  // namespace foundation::alloc
