// The cold bodies of the refill of a heap of the allocator of the base (THE
// FAST PATHS of foundation/alloc/HeapState.h): the refill of an empty bin,
// the start of a slab, and the census of the testing door.  The take of a
// chunk is a door of the window in src/foundation/alloc/Root.cpp.  This file
// holds no variable: each word is a field of a HeapState or of a record of
// the metadata region.

#include <foundation/alloc/HeapState.h>

#include <foundation/Platform.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Layout.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/alloc/SlabHeap.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Index.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <cstdint>

namespace foundation::alloc {

namespace {

// The number of a linked slab.
[[nodiscard]] SlabNumber number_of(SlabLink slab) noexcept { return SlabNumber{static_cast<std::uint32_t>(slab) - 1}; }

// The first byte of a slab: its number times the bytes of a unit, after the
// base of the window.  A medium slab holds the unit 0 of its chunk, so the
// same product gives the first byte of its chunk.
[[nodiscard]] std::uint64_t slab_first_byte(AddressWord window_base, SlabLink slab) noexcept {
    return static_cast<std::uint64_t>(window_base) + (std::uint64_t{number_of(slab).raw()} << kUnitShift);
}

// Writes the bitmap of a slab of the class with each slot free: the first
// slab_slots(class) bits set, and the summary bit of each word that holds
// one.  A slab that held another class can hold more set bits, so the call
// writes each word.  Complexity: kSlabBitmapWords words.
void start_bitmap(SlabMeta& meta, SizeClass size_class) noexcept {
    std::uint32_t const slots = detail::kSlotFacts.rows[static_cast<std::uint8_t>(size_class)].slab_slots;
    std::uint64_t summary = 0;
    for (BitmapWordIndex const word : core::IndexRange<kSlabBitmapWords>{}) {
        auto const first = static_cast<std::uint32_t>(word.raw() * 64);
        std::uint64_t bits = 0;
        if (first + 64 <= slots) {
            bits = ~std::uint64_t{0};
        } else if (first < slots) {
            bits = (std::uint64_t{1} << (slots - first)) - 1;
        }
        meta.set_bitmap_word(word, SlotBits{bits});
        if (bits != 0) summary |= std::uint64_t{1} << word.raw();
    }
    meta.set_size_class(size_class);
    meta.set_free_count(SlabSlotCount{static_cast<std::uint16_t>(slots)});
    meta.set_summary(SummaryBits{summary});
}

// True when the bitmap of a slab agrees with its free count and its summary
// word, and holds no set bit past the slots of its class (THE CENSUS).
// Complexity: kSlabBitmapWords words.
[[nodiscard]] bool is_bitmap_sound(SlabMeta const& meta) noexcept {
    std::uint32_t const slots = detail::kSlotFacts.rows[static_cast<std::uint8_t>(meta.size_class())].slab_slots;
    auto const summary = static_cast<std::uint64_t>(meta.summary());
    std::size_t free = 0;
    bool is_sound = true;
    for (BitmapWordIndex const word : core::IndexRange<kSlabBitmapWords>{}) {
        auto const bits = static_cast<std::uint64_t>(meta.bitmap_word(word));
        auto const first = static_cast<std::uint32_t>(word.raw() * 64);
        std::uint64_t allowed = 0;
        if (first + 64 <= slots) {
            allowed = ~std::uint64_t{0};
        } else if (first < slots) {
            allowed = (std::uint64_t{1} << (slots - first)) - 1;
        }
        bool const has_summary_bit = ((summary >> word.raw()) & 1U) != 0;
        is_sound = is_sound && (bits & ~allowed) == 0 && has_summary_bit == (bits != 0);
        free += static_cast<std::size_t>(core::popcount(bits));
    }
    return is_sound && free == meta.free_count().raw();
}

// The fatal text of a refill of a class with no slab, out of the refill.
[[noreturn]] CRUCIBLE_COLD void die_refill_without_slab(SizeClass size_class) noexcept {
    core::fatal("foundation: a refill of the class {} of the allocator needs the large runs, which no heap has.  The "
                "typed door takes only a class with a slab, 1 to 52",
                std::uint32_t{static_cast<std::uint8_t>(size_class)});
}

}  // namespace

AddressWord HeapState::refill_(SizeClass size_class) noexcept {
    if (is_sentinel(*this)) [[unlikely]] {
        return claim_thread_heap().allocate(size_class);
    }
    if (!is_slab_class(size_class)) [[unlikely]] {
        die_refill_without_slab(size_class);
    }
    Bin& bin = bins_[static_cast<std::uint8_t>(size_class)];
    // A bin holds at least two addresses, so the half is at least one.
    auto const wanted = static_cast<std::size_t>(bin.empty_end - bin.full_begin) / 2;
    AddressWord* const first = bin.empty_end - wanted;
    std::size_t filled = 0;
    while (filled < wanted) {
        SlabLink slab = partial_[static_cast<std::uint8_t>(size_class)].min();
        if (slab == SlabLink::none) slab = start_slab_(size_class);
        filled += take_slots_(size_class, slab, first + filled, core::ElementCount{wanted - filled}).raw();
    }
    bin.head = first + 1;
    return *first;
}

core::ElementCount HeapState::take_slots_(SizeClass size_class, SlabLink slab, AddressWord* first,
                                          core::ElementCount wanted) noexcept {
    CRUCIBLE_FATAL_INVARIANT(wanted.raw() != 0);
    SlabMeta& meta = slab_records_.at(slab);
    std::uint64_t const base = slab_first_byte(window_base_, slab);
    std::uint64_t const slot = detail::kSlotFacts.rows[static_cast<std::uint8_t>(size_class)].slot_bytes;
    std::uint64_t summary = static_cast<std::uint64_t>(meta.summary());
    std::size_t const count = wanted.raw();
    std::size_t taken = 0;
    // Each loop has one compare for each branch, because -fharden-compares
    // doubles a compound condition (design 6, PF2).  A set bit of the summary
    // names a word with a free slot, and the caller wants at least one block,
    // so each turn of a loop takes one block before its tests.
    while (summary != 0) {
        auto const word = static_cast<std::uint32_t>(core::countr_zero(summary));
        BitmapWordIndex const index = BitmapWordIndex::check(word).expect("a bit of a summary names a word below 64");
        std::uint64_t bits = static_cast<std::uint64_t>(meta.bitmap_word(index));
        // The summary and the words are metadata in memory, so no type holds
        // their agreement.  The check runs one time for each word.
        CRUCIBLE_FATAL_INVARIANT(bits != 0);
        std::uint64_t const word_first_byte = base + std::uint64_t{word} * 64 * slot;
        for (;;) {
            auto const bit = static_cast<std::uint64_t>(core::countr_zero(bits));
            bits &= bits - 1;
            first[taken] = AddressWord{word_first_byte + bit * slot};
            ++taken;
            if (bits == 0) break;
            if (taken == count) break;
        }
        meta.set_bitmap_word(index, SlotBits{bits});
        if (bits == 0) summary &= summary - 1;
        if (taken == count) break;
    }
    std::uint16_t const free_before = meta.free_count().raw();
    CRUCIBLE_FATAL_INVARIANT(taken <= free_before);
    auto const free_after = static_cast<std::uint16_t>(free_before - taken);
    meta.set_summary(SummaryBits{summary});
    meta.set_free_count(SlabSlotCount{free_after});
    if (free_after == 0) partial_[static_cast<std::uint8_t>(size_class)].remove(slab_records_, slab);
    return core::ElementCount{taken};
}

SlabLink HeapState::start_slab_(SizeClass size_class) noexcept {
    bool const is_small = static_cast<std::uint8_t>(size_class) <= kLastSmallClass;
    SlabHeap& empties = is_small ? empty_small_ : empty_medium_;
    SlabLink slab = empties.pop_min(slab_records_);
    if (slab == SlabLink::none) slab = is_small ? start_small_unit_() : start_medium_chunk_();
    start_bitmap(slab_records_.at(slab), size_class);
    partial_[static_cast<std::uint8_t>(size_class)].insert(slab_records_, slab);
    return slab;
}

SlabLink HeapState::start_small_unit_() noexcept {
    if (small_units_ == kUnitsPerChunk) {
        small_chunk_ = detail::claim_window_chunk_(self_);
        small_units_ = 0;
        ++chunks_taken_;
    }
    UnitIndex const unit = UnitIndex::check(small_units_).expect("the units of a chunk of small slabs lie below 32");
    ++small_units_;
    detail::start_window_slab_(small_chunk_, unit);
    return link_of(slab_number_of(small_chunk_, unit));
}

SlabLink HeapState::start_medium_chunk_() noexcept {
    ChunkIndex const chunk = detail::claim_window_chunk_(self_);
    ++chunks_taken_;
    UnitIndex const unit{core::StaticIndex<0>{}};
    detail::start_window_slab_(chunk, unit);
    return link_of(slab_number_of(chunk, unit));
}

HeapCensus HeapState::census() const noexcept {
    HeapCensus counts{};
    if (is_sentinel(*this)) return counts;
    std::size_t const claimed = detail::claimed_window_chunks_().raw();
    std::size_t chunks = 0;
    std::size_t slabs = 0;
    std::size_t taken = 0;
    std::size_t empty = 0;
    std::size_t partial = 0;
    std::size_t full = 0;
    std::size_t chunks_with_taken = 0;
    std::size_t wrong_bitmaps = 0;
    for (std::size_t raw = 0; raw < claimed; ++raw) {
        ChunkIndex const chunk{static_cast<std::uint32_t>(raw)};
        AddressWord const first_byte{static_cast<std::uint64_t>(window_base_) + (std::uint64_t{raw} << kChunkShift)};
        if (is_guard_chunk(chunk) || chunk_map_.owner_of(first_byte) != self_) continue;
        ++chunks;
        UnitIndex const first_unit{core::StaticIndex<0>{}};
        SizeClass const first_class = slab_records_.at(slab_number_of(chunk, first_unit)).size_class();
        std::uint32_t units = kUnitsPerChunk;
        if (static_cast<std::uint8_t>(first_class) > kLastSmallClass) {
            units = 1;
        } else if (chunk == small_chunk_) {
            units = small_units_;
        }
        bool has_taken = false;
        for (std::uint32_t raw_unit = 0; raw_unit < units; ++raw_unit) {
            UnitIndex const unit = UnitIndex::check(raw_unit).expect("a unit of a chunk lies below 32");
            SlabMeta const& meta = slab_records_.at(slab_number_of(chunk, unit));
            std::size_t const slots = detail::kSlotFacts.rows[static_cast<std::uint8_t>(meta.size_class())].slab_slots;
            std::size_t const free = meta.free_count().raw();
            ++slabs;
            taken += slots - free;
            if (free == slots) {
                ++empty;
            } else if (free == 0) {
                ++full;
            } else {
                ++partial;
            }
            has_taken = has_taken || free != slots;
            if (!is_bitmap_sound(meta)) ++wrong_bitmaps;
        }
        if (has_taken) ++chunks_with_taken;
    }
    std::size_t binned = 0;
    for (std::uint32_t value = 1; value <= kSizeClassCount; ++value) {
        binned += static_cast<std::size_t>(bins_[value].empty_end - bins_[value].head);
    }
    std::size_t partial_heaps = 0;
    for (std::uint32_t value = 1; value <= kLastMediumClass; ++value) {
        partial_heaps += partial_[value].count(slab_records_).raw();
    }
    counts.chunks_ = core::ElementCount{chunks};
    counts.slabs_ = core::ElementCount{slabs};
    counts.taken_slots_ = core::ElementCount{taken};
    counts.binned_blocks_ = core::ElementCount{binned};
    counts.empty_slabs_ = core::ElementCount{empty};
    counts.partial_slabs_ = core::ElementCount{partial};
    counts.full_slabs_ = core::ElementCount{full};
    counts.empty_heap_slabs_ =
        core::ElementCount{empty_small_.count(slab_records_).raw() + empty_medium_.count(slab_records_).raw()};
    counts.partial_heap_slabs_ = core::ElementCount{partial_heaps};
    counts.chunks_with_taken_slots_ = core::ElementCount{chunks_with_taken};
    counts.slabs_with_wrong_bitmap_ = core::ElementCount{wrong_bitmaps};
    return counts;
}

}  // namespace foundation::alloc
