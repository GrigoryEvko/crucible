// The cold bodies of the root of the allocator of the base
// (foundation/alloc/Root.h) and the one writer of a HeapState
// (foundation/alloc/HeapState.h).  This file holds no variable: each mutable
// word is a field of the three process-wide objects of the two headers.

#include <foundation/alloc/Root.h>

#include <foundation/Platform.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/HeapState.h>
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

    // Builds the state of a heap at the first byte of its committed slot.
    [[nodiscard]] static HeapState& start(AddressWord slot, BiasedChunkMap chunk_map, HeapId id) noexcept {
        void* const place = __builtin_bit_cast(void*, static_cast<std::uint64_t>(slot));
        HeapState* const heap = ::new(place) HeapState{};
        heap->chunk_map_ = chunk_map;
        heap->self_ = owner_entry_of(id);
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
        HeapState& heap =
            HeapStateDoor::start(slot_address(root, index), root.windows_.host().chunk_map(), heap_id_of(index));
        tls_heap_ = &heap;
        return heap;
    }
};

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
