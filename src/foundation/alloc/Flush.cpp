// The cold bodies of the flush of a heap of the allocator of the base (THE
// FAST PATHS of foundation/alloc/HeapState.h): the flush of a full bin, the
// give-back of one block to its slab with each check, and the flush of each
// bin of the testing door.  This file holds no variable.

#include <foundation/alloc/HeapState.h>

#include <foundation/Platform.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Layout.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/alloc/SlabHeap.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Index.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <cstdint>

namespace foundation::alloc {

void HeapState::flush_(SizeClass size_class, AddressWord block) noexcept {
    if (is_sentinel(*this)) {
        claim_thread_heap().release(size_class, block);
        return;
    }
    Bin& bin = bins_[static_cast<std::uint8_t>(size_class)];
    auto const capacity = static_cast<std::size_t>(bin.empty_end - bin.full_begin);
    std::size_t const given = capacity / 2;
    std::size_t const kept = capacity - given;
    AddressWord* const older = bin.empty_end - given;
    for (std::size_t index = 0; index < given; ++index) {
        give_back_(size_class, older[index]);
    }
    // The newer half moves up to end at empty_end.  The run moves up, so the
    // copy starts at its last word.
    for (std::size_t index = kept; index-- > 0;) {
        bin.full_begin[given + index] = bin.full_begin[index];
    }
    AddressWord* const head = bin.full_begin + given - 1;
    *head = block;
    bin.head = head;
}

void HeapState::flush_bins() noexcept {
    if (is_sentinel(*this)) return;
    for (std::uint32_t value = 1; value <= kSizeClassCount; ++value) {
        SizeClass const size_class{static_cast<std::uint8_t>(value)};
        Bin& bin = bins_[value];
        for (AddressWord* word = bin.head; word != bin.empty_end; ++word) {
            give_back_(size_class, *word);
        }
        bin.head = bin.empty_end;
    }
}

void HeapState::give_back_(SizeClass size_class, AddressWord block) noexcept {
    auto const address = static_cast<std::uint64_t>(block);
    auto const class_value = std::uint32_t{static_cast<std::uint8_t>(size_class)};
    std::uint32_t const heap = static_cast<std::uint16_t>(self_);
    std::uint64_t const offset = address - static_cast<std::uint64_t>(window_base_);
    if (offset >= window_bytes_) {
        core::fatal("foundation: a release gave the address {} to the heap {}, and the address is outside the window "
                    "of the heap",
                    address, heap);
    }
    ChunkOwner const owner = chunk_map_.owner_of(block);
    if (owner != self_) {
        core::fatal("foundation: a release gave the address {} to the heap {}, and its chunk has the owner {}.  A "
                    "block goes home to the heap of its chunk, through the remote free",
                    address, heap, std::uint32_t{static_cast<std::uint16_t>(owner)});
    }
    if (!is_slab_class(size_class)) {
        core::fatal("foundation: a release gave the address {} with the class {}, which has no slab", address,
                    class_value);
    }
    // A medium slab is the whole chunk: the record of its unit 0.
    bool const is_small = slab_kind_of(size_class) == SlabKind::small;
    std::uint32_t const unit_mask = is_small ? ~std::uint32_t{0} : ~(kUnitsPerChunk - 1);
    SlabNumber const number{static_cast<std::uint32_t>(offset >> kUnitShift) & unit_mask};
    SlabLink const slab = link_of(number);
    SlabMeta& meta = slab_records_.meta(slab);
    if (meta.size_class() != size_class) {
        core::fatal("foundation: a release gave the block at {} with the class {}, and its slab holds the class {}",
                    address, class_value, std::uint32_t{static_cast<std::uint8_t>(meta.size_class())});
    }
    std::uint64_t const slab_offset = offset - (std::uint64_t{number.raw()} << kUnitShift);
    core::Option<SlabSlotIndex> const slot = slot_at(size_class, core::ByteCount{slab_offset});
    if (slot.is_none()) {
        core::fatal("foundation: a release gave the address {}, which is not the first byte of a slot of its slab of "
                    "the class {}",
                    address, class_value);
    }
    std::size_t const index = core::Option<SlabSlotIndex>{slot}.value_or(SlabSlotIndex{core::StaticIndex<0>{}}).raw();
    BitmapWordIndex const word = BitmapWordIndex::check(index >> 6).expect("a slot of a slab lies below 4096");
    std::uint64_t const bit = std::uint64_t{1} << (index & 63);
    auto const bits = static_cast<std::uint64_t>(meta.bitmap_word(word));
    if ((bits & bit) != 0) {
        core::fatal("foundation: a second release of the block at {} of the class {}: its slot is free already, a "
                    "double free",
                    address, class_value);
    }
    meta.set_bitmap_word(word, SlotBits{bits | bit});
    meta.set_summary(SummaryBits{static_cast<std::uint64_t>(meta.summary()) | (std::uint64_t{1} << word.raw())});
    std::uint16_t const free_before = meta.free_count().raw();
    auto const free_after = static_cast<std::uint16_t>(free_before + 1);
    meta.set_free_count(SlabSlotCount{free_after});
    std::uint32_t const slots = detail::kSlotFacts.rows[class_value].slab_slots;
    SlabHeap& partial = partial_[class_value];
    if (free_after == slots) {
        if (free_before != 0) partial.remove(slab_records_, slab);
        SlabHeap& empties = is_small ? empty_small_ : empty_medium_;
        empties.insert(slab_records_, slab);
    } else if (free_before == 0) {
        partial.insert(slab_records_, slab);
    }
}

}  // namespace foundation::alloc
