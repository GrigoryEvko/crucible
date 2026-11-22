// The bodies of the flush of a heap of the allocator of the base (THE FAST
// PATHS of foundation/alloc/HeapState.h): the flush of a full bin, the
// give-back of a run of blocks to their slabs with each check, and the flush
// of each bin of the testing door.  This file holds no variable.
//
// THE GIVE-BACK OF A RUN
//   A flush gives back the older half of a bin, and the blocks of a half
//   usually lie in one slab and in few words of its bitmap.  So the give-back
//   takes the blocks one slab at a time, as the flush of a thread cache of
//   jemalloc takes them one slab at a time (tcache_bin_flush_impl, by recall).
//   One pass takes the slab of the first block of the run, gives back each
//   block of that slab, and keeps each block of another slab for the next
//   pass, at the front of the run.  In a pass the open word of the bitmap
//   stays in a register while the blocks stay in that word.  A pass reads and
//   writes the summary, the free count and the place in a slab heap of its
//   slab one time.  So a run of n blocks in one slab writes each word of the
//   bitmap one time, and costs no chain through memory for each block.
//
//   The checks of the chunk and of the class read the slab of a pass, so they
//   run one time for each slab.  A release of the erased path gives a raw
//   word (the types of the typed door forbid each bad release, design 12), so
//   three checks run for each block.  One compare of the offset of the block
//   in the slab of the pass with the end of its last slot holds the checks of
//   the window and of the slab, and only a block that fails it takes the
//   branch that keeps it for a later pass or ends the process.  Then one
//   multiply gives the slot, and the low word of the same product checks the
//   first byte of a slot (THE SLOT OF AN OFFSET of
//   foundation/alloc/Metadata.h).  One test of the bit finds a second
//   release.  A second release of one block in one run sets the bit of
//   the open word, or of a word that an earlier step of the pass wrote, so the
//   next test of the bit finds it.  Complexity: O(n k) for n blocks in k
//   slabs, at most 128 times 128 for one bin.

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

namespace {

// The facts of a bad release that a fatal text names: the block, the heap
// that took it, and the class of the release.
struct BadRelease final {
    AddressWord block{};
    ChunkOwner heap{ChunkOwner::none};
    SizeClass size_class{};
};

[[nodiscard]] std::uint64_t address_of(BadRelease const& release) noexcept {
    return static_cast<std::uint64_t>(release.block);
}

[[nodiscard]] std::uint32_t heap_of(BadRelease const& release) noexcept {
    return std::uint32_t{static_cast<std::uint16_t>(release.heap)};
}

[[nodiscard]] std::uint32_t class_value_of(SizeClass size_class) noexcept {
    return std::uint32_t{static_cast<std::uint8_t>(size_class)};
}

// The fatal texts of a give-back, out of its loop (THE GIVE-BACK OF A RUN).
[[noreturn]] CRUCIBLE_COLD void die_outside_window(BadRelease release) noexcept {
    core::fatal("foundation: a release gave the address {} to the heap {}, and the address is outside the window of "
                "the heap",
                address_of(release), heap_of(release));
}

[[noreturn]] CRUCIBLE_COLD void die_foreign_chunk(BadRelease release, ChunkOwner owner) noexcept {
    core::fatal("foundation: a release gave the address {} to the heap {}, and its chunk has the owner {}.  A block "
                "goes home to the heap of its chunk, through the remote free",
                address_of(release), heap_of(release), std::uint32_t{static_cast<std::uint16_t>(owner)});
}

[[noreturn]] CRUCIBLE_COLD void die_class_without_slab(BadRelease release) noexcept {
    core::fatal("foundation: a release gave the address {} with the class {}, which has no slab", address_of(release),
                class_value_of(release.size_class));
}

[[noreturn]] CRUCIBLE_COLD void die_other_class(BadRelease release, SizeClass slab_class) noexcept {
    core::fatal("foundation: a release gave the block at {} with the class {}, and its slab holds the class {}",
                address_of(release), class_value_of(release.size_class), class_value_of(slab_class));
}

[[noreturn]] CRUCIBLE_COLD void die_inside_slot(BadRelease release) noexcept {
    core::fatal("foundation: a release gave the address {}, which is not the first byte of a slot of its slab of the "
                "class {}",
                address_of(release), class_value_of(release.size_class));
}

[[noreturn]] CRUCIBLE_COLD void die_second_release(BadRelease release) noexcept {
    core::fatal("foundation: a second release of the block at {} of the class {}: its slot is free already, a double "
                "free",
                address_of(release), class_value_of(release.size_class));
}

}  // namespace

void HeapState::flush_(SizeClass size_class, AddressWord block) noexcept {
    if (is_sentinel(*this)) {
        claim_thread_heap().release(size_class, block);
        return;
    }
    Bin& bin = bins_[static_cast<std::uint8_t>(size_class)];
    auto const capacity = static_cast<std::size_t>(bin.empty_end - bin.full_begin);
    std::size_t const given = capacity / 2;
    std::size_t const kept = capacity - given;
    give_back_run_(size_class, bin.empty_end - given, core::ElementCount{given});
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
        Bin& bin = bins_[value];
        auto const count = static_cast<std::size_t>(bin.empty_end - bin.head);
        if (count != 0)
            give_back_run_(SizeClass{static_cast<std::uint8_t>(value)}, bin.head, core::ElementCount{count});
        bin.head = bin.empty_end;
    }
}

void HeapState::give_back_run_(SizeClass size_class, AddressWord* first, core::ElementCount count) noexcept {
    std::uint32_t const class_value = class_value_of(size_class);
    if (!is_slab_class(size_class)) die_class_without_slab(BadRelease{first[0], self_, size_class});
    // A medium slab is the whole chunk: the record of its unit 0.
    bool const is_small = class_value <= kLastSmallClass;
    std::uint32_t const unit_mask = is_small ? ~std::uint32_t{0} : ~(kUnitsPerChunk - 1);
    // A copy of the facts of the class, so the loop keeps them in registers.
    detail::SlotFacts const facts = detail::kSlotFacts.rows[class_value];
    std::uint64_t const slot_bytes = facts.slot_bytes;
    std::uint64_t const slab_slots = facts.slab_slots;
    // The bytes from the first byte of a slab to the end of its last slot.
    std::uint64_t const slot_span = slab_slots * slot_bytes;
    auto const window_base = static_cast<std::uint64_t>(window_base_);
    std::uint64_t const window_bytes = window_bytes_;
    SlabRecords const records = slab_records_;
    std::size_t remaining = count.raw();
    while (remaining != 0) {
        // The slab of the first block of the run is the slab of this pass.
        // Its chunk and its class have one check each.
        AddressWord const lead = first[0];
        std::uint64_t const lead_offset = static_cast<std::uint64_t>(lead) - window_base;
        if (lead_offset >= window_bytes) [[unlikely]] {
            die_outside_window(BadRelease{lead, self_, size_class});
        }
        ChunkOwner const owner = chunk_map_.owner_of(lead);
        if (owner != self_) [[unlikely]] {
            die_foreign_chunk(BadRelease{lead, self_, size_class}, owner);
        }
        auto const number = static_cast<std::uint32_t>(lead_offset >> kUnitShift) & unit_mask;
        SlabLink const slab = link_of(SlabNumber{number});
        SlabMeta& meta = records.at(slab);
        if (meta.size_class() != size_class) [[unlikely]] {
            die_other_class(BadRelease{lead, self_, size_class}, meta.size_class());
        }
        std::uint64_t const slab_address = window_base + (std::uint64_t{number} << kUnitShift);
        auto summary = static_cast<std::uint64_t>(meta.summary());
        // The word of the lead block is open first.  An offset in a slab lies
        // below the 64 KiB of a unit or the 2 MiB of a chunk of a medium slab,
        // so its slot is below 4096 and its word below 64 also before the
        // check of the slot.  mask() of such a word gives the word itself, and
        // each word of a slot below holds by the same bound.
        std::uint64_t const lead_in_slab = static_cast<std::uint64_t>(lead) - slab_address;
        BitmapWordIndex open =
            BitmapWordIndex::mask(detail::slot_product(facts, core::ByteCount{lead_in_slab}).slot() >> 6);
        auto bits = static_cast<std::uint64_t>(meta.bitmap_word(open));
        std::size_t kept = 0;
        for (std::size_t index = 0; index < remaining; ++index) {
            AddressWord const block = first[index];
            // One compare holds three checks: the block lies in the window, in
            // the slab of the pass, and below the end of its last slot.
            std::uint64_t const in_slab = static_cast<std::uint64_t>(block) - slab_address;
            if (in_slab >= slot_span) [[unlikely]] {
                std::uint64_t const offset = static_cast<std::uint64_t>(block) - window_base;
                if (offset >= window_bytes) die_outside_window(BadRelease{block, self_, size_class});
                if ((static_cast<std::uint32_t>(offset >> kUnitShift) & unit_mask) == number) {
                    die_inside_slot(BadRelease{block, self_, size_class});
                }
                first[kept] = block;
                ++kept;
                continue;
            }
            detail::SlotProduct const product = detail::slot_product(facts, core::ByteCount{in_slab});
            if (!product.is_slot_start(facts)) [[unlikely]] {
                die_inside_slot(BadRelease{block, self_, size_class});
            }
            std::uint64_t const slot = product.slot();
            // A word holds 64 slots, so the word changes at most one time in
            // 64 blocks of a run in the order of their slots.
            std::uint64_t const word = slot >> 6;
            if (word != open.raw()) [[unlikely]] {
                meta.set_bitmap_word(open, SlotBits{bits});
                summary |= std::uint64_t{1} << open.raw();
                open = BitmapWordIndex::mask(word);
                bits = static_cast<std::uint64_t>(meta.bitmap_word(open));
            }
            std::uint64_t const position = slot & 63;
            if (((bits >> position) & 1) != 0) [[unlikely]] {
                die_second_release(BadRelease{block, self_, size_class});
            }
            bits |= std::uint64_t{1} << position;
        }
        std::size_t const given = remaining - kept;
        // Each word that the pass wrote holds a bit that the pass set.
        meta.set_bitmap_word(open, SlotBits{bits});
        summary |= std::uint64_t{1} << open.raw();
        meta.set_summary(SummaryBits{summary});
        std::uint16_t const free_before = meta.free_count().raw();
        auto const free_after = static_cast<std::uint16_t>(free_before + given);
        meta.set_free_count(SlabSlotCount{free_after});
        SlabHeap& partial = partial_[class_value];
        if (free_after == slab_slots) {
            if (free_before != 0) partial.remove(records, slab);
            SlabHeap& empties = is_small ? empty_small_ : empty_medium_;
            empties.insert(records, slab);
        } else if (free_before == 0) {
            partial.insert(records, slab);
        }
        remaining = kept;
    }
}

}  // namespace foundation::alloc
