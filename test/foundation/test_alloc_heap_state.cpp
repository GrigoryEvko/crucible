// The heap state of foundation/alloc/HeapState.h through its engine doors:
// the bins, the refill from the bitmaps, the flush to them, and the census.
//
//   - Each class with a slab, 1 to 52: three bins and one block more of
//     allocations, then the releases in the order of the stack.  The census
//     after each step agrees with the blocks of the test and of the bins,
//     each slab is in the slab heap of its state, and each bitmap agrees
//     with its free count.
//   - The same, with the releases in the order of the allocations.
//   - A Philox order of allocations and releases over the classes 1 to 52.
//   - After each sequence, a flush of the bins leaves each slab empty and in
//     an empty slab heap, and no chunk of the heap holds a taken slot.
//   - The Philox sequence on the fresh heaps of two threads gives the same
//     class, the same ordinal of the chunk in the heap, and the same offset in
//     the chunk for each block.
//   - The first allocation of a new thread claims its heap through the
//     sentinel.
//   - A flush of a run whose blocks of two slabs alternate, and of a run
//     whose blocks alternate between two words of a bitmap, keeps each
//     bitmap sound.
//   - In a child process, each with releases after the bad one that fill the
//     bin and force a flush with the bad address in its older half: a second
//     release of one block dies, also with a block of a second slab between
//     the two releases, a release of an address inside a block dies, a
//     release with the wrong class dies, a release of a block of another heap
//     dies, and a release of an address outside the window dies.

#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Layout.h>
#include <foundation/alloc/Root.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Philox.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace alloc = ::foundation::alloc;
namespace core = ::foundation::core;

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;
using ::fixy::check::pass;

// The blocks that a test holds at one time, the steps of the Philox
// sequence, and its key.
constexpr std::size_t kMostHeld = 4096;
constexpr std::size_t kSteps = 12000;
constexpr std::uint64_t kSequenceKey = 0x48454150535441ULL;
// The chunks of a heap that the ordinals of a trace count.
constexpr std::size_t kMostChunks = 256;

using HeldIndex = core::Index<kMostHeld>;
using ChunkOrdinal = core::Index<kMostChunks>;

[[nodiscard]] alloc::SizeClass class_of(std::uint32_t value) noexcept {
    return alloc::SizeClass{static_cast<std::uint8_t>(value)};
}

[[nodiscard]] std::uint32_t draw_of(std::size_t index) noexcept {
    core::Philox4x32::Counter const words = core::Philox4x32::generate(std::uint64_t{index / 4}, kSequenceKey);
    return words.at(core::Index<4>::check(index % 4).expect("a word of a block lies below 4"));
}

// The blocks that a test holds, with their classes.
struct Held {
    core::FixedArray<alloc::AddressWord, kMostHeld> blocks{};
    core::FixedArray<alloc::SizeClass, kMostHeld> classes{};
    std::size_t count = 0;
};

[[nodiscard]] HeldIndex held_at(std::size_t raw) noexcept {
    return HeldIndex::check(raw).expect("a held block lies below kMostHeld");
}

// True when the census agrees with the blocks of the test and of the bins,
// each slab is in the slab heap of its state, and each bitmap is sound.
[[nodiscard]] bool is_census_sound(alloc::HeapState const& heap, core::ElementCount held) noexcept {
    alloc::HeapCensus const census = heap.census();
    return census.taken_slots().raw() == held.raw() + census.binned_blocks().raw()
        && census.empty_slabs().raw() == census.empty_heap_slabs().raw()
        && census.partial_slabs().raw() == census.partial_heap_slabs().raw()
        && census.slabs().raw() == census.empty_slabs().raw() + census.partial_slabs().raw() + census.full_slabs().raw()
        && census.slabs_with_wrong_bitmap().raw() == 0;
}

// True when a flush of the bins leaves each slab empty and in an empty slab
// heap, and no chunk with a taken slot.
[[nodiscard]] bool is_empty_after_flush(alloc::HeapState& heap) noexcept {
    heap.flush_bins();
    alloc::HeapCensus const census = heap.census();
    return census.taken_slots().raw() == 0 && census.binned_blocks().raw() == 0
        && census.empty_slabs().raw() == census.slabs().raw() && census.empty_heap_slabs().raw() == census.slabs().raw()
        && census.partial_heap_slabs().raw() == 0 && census.chunks_with_taken_slots().raw() == 0
        && census.slabs_with_wrong_bitmap().raw() == 0 && census.chunks().raw() != 0;
}

// The order of the releases of one class.
enum class ReleaseOrder : std::uint8_t {
    stack,
    allocation,
};

// Each class with a slab: three bins and one block more of allocations, then
// the releases in the order, with the census after each step.
[[nodiscard]] bool run_each_class(alloc::HeapState& heap, Held& held, ReleaseOrder order) noexcept {
    bool is_sound = true;
    for (std::uint32_t value = 1; value <= alloc::kLastMediumClass; ++value) {
        alloc::SizeClass const size_class = class_of(value);
        std::size_t const blocks = 3 * std::size_t{alloc::bin_capacity(size_class)} + 1;
        for (std::size_t step = 0; step < blocks; ++step) {
            alloc::AddressWord const block = heap.allocate(size_class);
            held.blocks.with_element(held_at(step), [block](alloc::AddressWord& slot) noexcept { slot = block; });
            is_sound = is_sound && is_census_sound(heap, core::ElementCount{step + 1});
        }
        for (std::size_t step = 0; step < blocks; ++step) {
            std::size_t const position = order == ReleaseOrder::stack ? blocks - 1 - step : step;
            heap.release(size_class, held.blocks.at(held_at(position)));
            is_sound = is_sound && is_census_sound(heap, core::ElementCount{blocks - 1 - step});
        }
    }
    return is_sound;
}

// The trace of a Philox sequence: a hash of the class, the ordinal of the
// chunk in the heap and the offset in the chunk of each block, and the facts
// of the census.
// The FNV-1a hash of the words of a trace.
struct TraceHash {
    std::uint64_t value = 0xcbf29ce484222325ULL;

    void add(std::uint64_t word) noexcept { value = (value ^ word) * 0x100000001b3ULL; }
};

struct Trace {
    TraceHash hash{};
    std::size_t allocations = 0;
    bool is_sound = true;
};

// The ordinals of the chunks of a heap, in the order of their first block.
struct Ordinals {
    core::FixedArray<std::uint32_t, kMostChunks> chunks{};
    std::size_t count = 0;

    [[nodiscard]] std::size_t ordinal_of(alloc::ChunkIndex chunk) noexcept {
        for (std::size_t raw = 0; raw < count; ++raw) {
            if (chunks.at(ChunkOrdinal::check(raw).expect("an ordinal below the count")) == chunk.raw()) return raw;
        }
        chunks.with_element(ChunkOrdinal::check(count).expect("a heap of the test takes at most kMostChunks chunks"),
                            [chunk](std::uint32_t& ordinal) noexcept { ordinal = chunk.raw(); });
        return count++;
    }
};

// A Philox order of allocations and releases over the classes 1 to 52 on a
// heap, with the census after each step, and the release of each block at
// the end.
[[nodiscard]] Trace run_sequence(alloc::HeapState& heap, Held& held) noexcept {
    Trace trace{};
    Ordinals ordinals{};
    auto const base = static_cast<std::uint64_t>(alloc::alloc_root_.window(alloc::kHostWindow).base());
    for (std::size_t step = 0; step < kSteps; ++step) {
        std::uint32_t const word = draw_of(step);
        bool const should_allocate = held.count == 0 || ((word & 3) != 0 && held.count < kMostHeld);
        if (should_allocate) {
            // The classes to 128 bytes come more often, as in a real load.
            std::uint32_t const value = (word & 4) != 0 ? 1 + ((word >> 8) % 8) : 1 + ((word >> 8) % 52);
            alloc::SizeClass const size_class = class_of(value);
            alloc::AddressWord const block = heap.allocate(size_class);
            held.blocks.with_element(held_at(held.count), [block](alloc::AddressWord& slot) noexcept { slot = block; });
            held.classes.with_element(held_at(held.count),
                                      [size_class](alloc::SizeClass& slot) noexcept { slot = size_class; });
            ++held.count;
            std::uint64_t const offset = static_cast<std::uint64_t>(block) - base;
            std::size_t const ordinal =
                ordinals.ordinal_of(alloc::ChunkIndex{static_cast<std::uint32_t>(offset >> alloc::kChunkShift)});
            trace.hash.add(value);
            trace.hash.add(ordinal);
            trace.hash.add(offset & (alloc::kChunkBytes - 1));
            ++trace.allocations;
        } else {
            std::size_t const position = (word >> 8) % held.count;
            HeldIndex const last = held_at(held.count - 1);
            heap.release(held.classes.at(held_at(position)), held.blocks.at(held_at(position)));
            alloc::AddressWord const last_block = held.blocks.at(last);
            alloc::SizeClass const last_class = held.classes.at(last);
            held.blocks.with_element(held_at(position),
                                     [last_block](alloc::AddressWord& slot) noexcept { slot = last_block; });
            held.classes.with_element(held_at(position),
                                      [last_class](alloc::SizeClass& slot) noexcept { slot = last_class; });
            --held.count;
        }
        trace.is_sound = trace.is_sound && is_census_sound(heap, core::ElementCount{held.count});
    }
    while (held.count != 0) {
        HeldIndex const last = held_at(held.count - 1);
        heap.release(held.classes.at(last), held.blocks.at(last));
        --held.count;
    }
    trace.is_sound =
        trace.is_sound && is_census_sound(heap, core::ElementCount{std::size_t{0}}) && is_empty_after_flush(heap);
    return trace;
}

// The value of body, which runs on a second thread that the call joins.
template <class Body>
[[nodiscard]] auto on_other_thread(Body body) {
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto const values = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body))
                            .expect("the host starts the thread of the test");
    return values.at(::fixy::index<0>());
}

void each_class_in_both_orders(TestRunnerCtx const& test) {
    core::Box<Held> held = core::mint_box<Held>(::foundation::effects::testing::test().alloc);
    alloc::HeapState& heap = alloc::claim_thread_heap();
    CRUCIBLE_TEST_ASSERT(test, run_each_class(heap, *held, ReleaseOrder::stack));
    CRUCIBLE_TEST_ASSERT(test, is_empty_after_flush(heap));
    CRUCIBLE_TEST_ASSERT(test, run_each_class(heap, *held, ReleaseOrder::allocation));
    CRUCIBLE_TEST_ASSERT(test, is_empty_after_flush(heap));
    pass(test, "  each_class_in_both_orders: PASSED\n");
}

void sequence_is_the_same_on_two_heaps(TestRunnerCtx const& test) {
    auto const run_on_fresh_heap = [] noexcept {
        core::Box<Held> held = core::mint_box<Held>(::foundation::effects::testing::test().alloc);
        bool const was_sentinel = alloc::is_sentinel(alloc::current_heap());
        Trace trace = run_sequence(alloc::claim_thread_heap(), *held);
        trace.is_sound = trace.is_sound && was_sentinel;
        return trace;
    };
    Trace const first = on_other_thread(run_on_fresh_heap);
    Trace const second = on_other_thread(run_on_fresh_heap);
    CRUCIBLE_TEST_ASSERT(test, first.is_sound && second.is_sound);
    CRUCIBLE_TEST_ASSERT(test, first.allocations > kSteps / 2 && first.allocations == second.allocations);
    CRUCIBLE_TEST_ASSERT(test, first.hash.value == second.hash.value);
    pass(test, "  sequence_is_the_same_on_two_heaps: PASSED\n");
}

void first_allocation_claims_through_the_sentinel(TestRunnerCtx const& test) {
    bool const is_claimed = on_other_thread([] noexcept {
        bool const was_sentinel = alloc::is_sentinel(alloc::current_heap());
        alloc::AddressWord const block = alloc::current_heap().allocate(class_of(3));
        alloc::HeapState& heap = alloc::current_heap();
        bool const has_heap = !alloc::is_sentinel(heap) && heap.chunk_map().owner_of(block) == heap.self();
        heap.release(class_of(3), block);
        return was_sentinel && has_heap && is_census_sound(heap, core::ElementCount{std::size_t{0}});
    });
    CRUCIBLE_TEST_ASSERT(test, is_claimed);
    pass(test, "  first_allocation_claims_through_the_sentinel: PASSED\n");
}

// The blocks of the class 8 in two slabs of 512 slots: the test takes 1024
// blocks, so the first 512 lie in one slab and the next 512 in the next one.
constexpr std::size_t kTwoSlabBlocks = 1024;
constexpr std::size_t kSlotsOfClassEight = 512;

// Takes kTwoSlabBlocks blocks of the class 8 into HELD, and then gives the
// blocks of the bins back to their slabs, so each bin is empty.
void take_two_slabs(alloc::HeapState& heap, Held& held) noexcept {
    for (std::size_t position = 0; position < kTwoSlabBlocks; ++position) {
        alloc::AddressWord const block = heap.allocate(class_of(8));
        held.blocks.with_element(held_at(position), [block](alloc::AddressWord& slot) noexcept { slot = block; });
    }
    heap.flush_bins();
}

// True when two blocks lie in two units of the window.
[[nodiscard]] bool are_in_two_units(alloc::AddressWord first, alloc::AddressWord second) noexcept {
    return (static_cast<std::uint64_t>(first) >> alloc::kUnitShift)
        != (static_cast<std::uint64_t>(second) >> alloc::kUnitShift);
}

// A flush of a run whose blocks of two slabs alternate, and a flush of a run
// whose blocks of one slab alternate between two words of the bitmap.  The
// census after each release agrees with the blocks, and each bitmap agrees
// with its free count and its summary word.
void flush_takes_each_slab_and_each_word(TestRunnerCtx const& test) {
    bool const is_sound = on_other_thread([] noexcept {
        core::Box<Held> held = core::mint_box<Held>(::foundation::effects::testing::test().alloc);
        alloc::HeapState& heap = alloc::claim_thread_heap();
        take_two_slabs(heap, *held);
        bool sound = are_in_two_units(held->blocks.at(held_at(0)), held->blocks.at(held_at(kSlotsOfClassEight)));
        std::size_t live = kTwoSlabBlocks;
        for (std::size_t step = 0; step < kSlotsOfClassEight; ++step) {
            heap.release(class_of(8), held->blocks.at(held_at(step)));
            heap.release(class_of(8), held->blocks.at(held_at(kSlotsOfClassEight + step)));
            live -= 2;
            sound = sound && is_census_sound(heap, core::ElementCount{live});
        }
        sound = sound && is_empty_after_flush(heap);
        // The class 1 has 64 slots in each word of its bitmap, so the blocks
        // 0 and 64 of a fresh slab lie in two words.
        for (std::size_t position = 0; position < 128; ++position) {
            alloc::AddressWord const block = heap.allocate(class_of(1));
            held->blocks.with_element(held_at(position), [block](alloc::AddressWord& slot) noexcept { slot = block; });
        }
        heap.flush_bins();
        live = 128;
        for (std::size_t step = 0; step < 64; ++step) {
            heap.release(class_of(1), held->blocks.at(held_at(step)));
            heap.release(class_of(1), held->blocks.at(held_at(64 + step)));
            live -= 2;
            sound = sound && is_census_sound(heap, core::ElementCount{live});
        }
        return sound && is_empty_after_flush(heap);
    });
    CRUCIBLE_TEST_ASSERT(test, is_sound);
    pass(test, "  flush_takes_each_slab_and_each_word: PASSED\n");
}

// The releases after a bad release into an empty bin: the blocks of the
// class at the positions FIRST to the capacity.  The bad word and the
// capacity - 1 words after it fill the bin, and the next release forces the
// flush, with the bad word as the oldest word of its older half.
void fill_and_flush(alloc::HeapState& heap, alloc::SizeClass size_class, Held const& held, HeldIndex first) noexcept {
    std::size_t const capacity = alloc::bin_capacity(size_class);
    for (std::size_t position = first.raw(); position <= capacity; ++position) {
        heap.release(size_class, held.blocks.at(held_at(position)));
    }
}

// Takes capacity + 1 blocks of the class into HELD from position 0, and then
// gives the blocks of the bins back to their slabs, so each bin is empty.
void take_blocks(alloc::HeapState& heap, alloc::SizeClass size_class, Held& held) noexcept {
    std::size_t const capacity = alloc::bin_capacity(size_class);
    for (std::size_t position = 0; position <= capacity; ++position) {
        alloc::AddressWord const block = heap.allocate(size_class);
        held.blocks.with_element(held_at(position), [block](alloc::AddressWord& slot) noexcept { slot = block; });
    }
    heap.flush_bins();
}

void bad_releases_die_at_the_flush(TestRunnerCtx const& test) {
    // A second release of one block: the two words of the block are the two
    // oldest words of the full bin.
    expect_dies(
        test,
        [] noexcept {
            core::Box<Held> held = core::mint_box<Held>(::foundation::effects::testing::test().alloc);
            alloc::HeapState& heap = alloc::claim_thread_heap();
            alloc::SizeClass const size_class = class_of(1);
            take_blocks(heap, size_class, *held);
            heap.release(size_class, held->blocks.at(held_at(0)));
            fill_and_flush(heap, size_class, *held, held_at(0));
        },
        "a double free");
    // A release of an address 16 bytes inside a block of 32 bytes.
    expect_dies(
        test,
        [] noexcept {
            core::Box<Held> held = core::mint_box<Held>(::foundation::effects::testing::test().alloc);
            alloc::HeapState& heap = alloc::claim_thread_heap();
            alloc::SizeClass const size_class = class_of(2);
            take_blocks(heap, size_class, *held);
            auto const inside = static_cast<std::uint64_t>(held->blocks.at(held_at(0))) + 16;
            heap.release(size_class, alloc::AddressWord{inside});
            fill_and_flush(heap, size_class, *held, held_at(1));
        },
        "which is not the first byte of a slot");
    // A release of a block of the class 1 into the bin of the class 2.
    expect_dies(
        test,
        [] noexcept {
            core::Box<Held> held = core::mint_box<Held>(::foundation::effects::testing::test().alloc);
            alloc::HeapState& heap = alloc::claim_thread_heap();
            alloc::SizeClass const size_class = class_of(2);
            take_blocks(heap, size_class, *held);
            alloc::AddressWord const other = heap.allocate(class_of(1));
            heap.release(size_class, other);
            fill_and_flush(heap, size_class, *held, held_at(1));
        },
        "and its slab holds the class 1");
    // A second release of one block, with a block of a second slab between
    // the two releases.  The lead block of the run lies in the second slab, so
    // its pass keeps the two copies for the pass of their own slab, and that
    // pass finds the second one.
    expect_dies(
        test,
        [] noexcept {
            core::Box<Held> held = core::mint_box<Held>(::foundation::effects::testing::test().alloc);
            alloc::HeapState& heap = alloc::claim_thread_heap();
            take_two_slabs(heap, *held);
            heap.release(class_of(8), held->blocks.at(held_at(0)));
            heap.release(class_of(8), held->blocks.at(held_at(kSlotsOfClassEight)));
            heap.release(class_of(8), held->blocks.at(held_at(0)));
            std::size_t const capacity = alloc::bin_capacity(class_of(8));
            for (std::size_t position = 1; position <= capacity - 2; ++position) {
                heap.release(class_of(8), held->blocks.at(held_at(kSlotsOfClassEight + position)));
            }
        },
        "a double free");
    // A release of a block of the heap of a second thread: its chunk has
    // another owner.
    expect_dies(
        test,
        [] noexcept {
            core::Box<Held> held = core::mint_box<Held>(::foundation::effects::testing::test().alloc);
            alloc::HeapState& heap = alloc::claim_thread_heap();
            alloc::SizeClass const size_class = class_of(2);
            take_blocks(heap, size_class, *held);
            alloc::AddressWord const foreign =
                on_other_thread([] noexcept { return alloc::claim_thread_heap().allocate(class_of(2)); });
            heap.release(size_class, foreign);
            fill_and_flush(heap, size_class, *held, held_at(1));
        },
        "and its chunk has the owner");
    // A release of an address outside the window of the heap.
    expect_dies(
        test,
        [] noexcept {
            core::Box<Held> held = core::mint_box<Held>(::foundation::effects::testing::test().alloc);
            alloc::HeapState& heap = alloc::claim_thread_heap();
            alloc::SizeClass const size_class = class_of(2);
            take_blocks(heap, size_class, *held);
            heap.release(size_class, alloc::AddressWord{std::uint64_t{4096}});
            fill_and_flush(heap, size_class, *held, held_at(1));
        },
        "and the address is outside the window");
    pass(test, "  bad_releases_die_at_the_flush: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    each_class_in_both_orders(test);
    sequence_is_the_same_on_two_heaps(test);
    first_allocation_claims_through_the_sentinel(test);
    flush_takes_each_slab_and_each_word(test);
    bad_releases_die_at_the_flush(test);
    pass(test, "test_alloc_heap_state: PASSED\n");
    return 0;
}
