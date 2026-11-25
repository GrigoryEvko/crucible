// The slab heap of foundation/alloc/SlabHeap.h over the slab records of a
// window of 1 GiB, against an oracle of the slabs that the heap holds.
//
//   - A Philox sequence of kSteps steps inserts, pops and removes the slabs of
//     kChunks chunks, the slab 0 among them.  After each step the lowest slab
//     of the heap is the lowest slab of the oracle.  Every kWalkEvery steps a
//     walk of the tree checks each link: each child has a higher number than
//     its parent, the last link of each slab names its parent or its last
//     sibling, the root has no sibling, the tree holds each slab of the
//     oracle one time, and the count of the heap is the count of the oracle.
//   - At the end, a pop of each slab gives the slabs of the oracle in
//     ascending order, and the heap ends empty.
//   - A removal of the root, of a first child and of a later sibling, each
//     with the order of the rest after it.

#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/alloc/SlabHeap.h>
#include <foundation/alloc/Window.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Philox.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/PageSource.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace alloc = ::foundation::alloc;
namespace core = ::foundation::core;
namespace os = ::foundation::os;

using ::fixy::TestRunnerCtx;
using ::fixy::check::pass;

// The chunks whose slabs the test uses, and their slabs.
constexpr std::size_t kChunks = 16;
constexpr std::size_t kSlabs = kChunks * alloc::kUnitsPerChunk;
// The steps of the sequence, and the steps between two walks of the tree.
constexpr std::size_t kSteps = 20000;
constexpr std::size_t kWalkEvery = 500;
// The key of the Philox stream of the sequence.
constexpr std::uint64_t kSequenceKey = 0x534c41425f484541ULL;
// The window of the test: the smallest size of the ladder.
constexpr std::size_t kWindowBytes = std::size_t{1} << 30;

using SlabSlot = core::Index<kSlabs>;

// The word of draw INDEX of the stream of the sequence.
[[nodiscard]] std::uint32_t draw_of(std::size_t index) noexcept {
    core::Philox4x32::Counter const words = core::Philox4x32::generate(std::uint64_t{index / 4}, kSequenceKey);
    return words.at(core::Index<4>::check(index % 4).expect("a word of a block lies below 4"));
}

[[nodiscard]] SlabSlot slot_of(std::size_t raw) noexcept {
    return SlabSlot::check(raw).expect("a slab of the test lies below kSlabs");
}

[[nodiscard]] alloc::SlabLink link_at(std::size_t raw) noexcept {
    return alloc::link_of(alloc::SlabNumber{static_cast<std::uint32_t>(raw)});
}

[[nodiscard]] std::size_t raw_of(alloc::SlabLink link) noexcept { return static_cast<std::uint32_t>(link) - 1; }

// The chunk source of the test with each record of its first kChunks chunks
// started, and the records of its slabs.
struct Records {
    alloc::ChunkSource source;
    alloc::SlabRecords records{};

    explicit Records(alloc::Window&& window) noexcept : source{std::move(window)} {
        for (std::uint32_t chunk = 0; chunk < kChunks; ++chunk) {
            for (alloc::UnitIndex const unit : core::IndexRange<alloc::kUnitsPerChunk>{}) {
                static_cast<void>(source.start_slab_meta(alloc::ChunkIndex{chunk}, unit)
                                      .expect("the kernel commits the records of the test"));
            }
        }
        records = alloc::SlabRecords::of(source.slab_records_at());
    }
};

[[nodiscard]] Records start_records() noexcept {
    alloc::WindowDiagnostic diagnostic{};
    core::Result<alloc::Window, alloc::WindowFull> reserved =
        alloc::reserve_window(os::ReserveCap{kWindowBytes}, core::to(diagnostic));
    if (reserved.is_err()) fixy::fatal("{}", diagnostic.text());
    return Records{std::move(reserved).expect("a window that the ladder gave")};
}

// The state of a slab that a search of the oracle wants.
enum class Holding : std::uint8_t {
    held,
    not_held,
};

// The slabs that the heap holds, by the oracle.
struct Oracle {
    core::FixedArray<bool, kSlabs> held{};
    std::size_t count = 0;

    // Sets the slab at SLOT to the state STATE.
    void mark(SlabSlot slot, Holding state) noexcept { held.set(slot, state == Holding::held); }

    // The lowest held slab, or none.
    [[nodiscard]] alloc::SlabLink lowest() const noexcept {
        for (SlabSlot const slot : core::IndexRange<kSlabs>{}) {
            if (held.at(slot)) return link_at(slot.raw());
        }
        return alloc::SlabLink::none;
    }

    // The first slab at or after START, in a cycle, in the state WANT.  The
    // caller knows that one exists.
    [[nodiscard]] std::size_t find_from(SlabSlot start, Holding want) const noexcept {
        bool const is_held_wanted = want == Holding::held;
        for (std::size_t step = 0; step < kSlabs; ++step) {
            std::size_t const raw = (start.raw() + step) % kSlabs;
            if (held.at(slot_of(raw)) == is_held_wanted) return raw;
        }
        fixy::fatal("the oracle holds no slab of the wanted state");
    }
};

// True when each link of the tree is consistent and the tree holds each slab
// of the oracle one time (the walk of the header).  Complexity: linear in the
// slabs of the heap.
[[nodiscard]] bool is_tree_sound(alloc::SlabHeap const& heap, alloc::SlabRecords records,
                                 Oracle const& oracle) noexcept {
    alloc::SlabLink const root = heap.min();
    if (root == alloc::SlabLink::none) return oracle.count == 0;
    if (records.next_of(root) != alloc::SlabLink::none || records.prev_of(root) != alloc::SlabLink::none) return false;
    core::FixedArray<alloc::SlabLink, kSlabs> pending{};
    core::FixedArray<bool, kSlabs> seen{};
    std::size_t depth = 0;
    std::size_t visited = 0;
    pending.set(slot_of(depth++), root);
    while (depth != 0) {
        alloc::SlabLink const node = pending.at(slot_of(--depth));
        SlabSlot const node_slot = slot_of(raw_of(node));
        if (!oracle.held.at(node_slot) || seen.at(node_slot)) return false;
        seen.set(node_slot, true);
        ++visited;
        alloc::SlabLink expected_prev = node;
        for (alloc::SlabLink child = records.child_of(node); child != alloc::SlabLink::none;
             child = records.next_of(child)) {
            if (!(node < child) || records.prev_of(child) != expected_prev) return false;
            if (depth == kSlabs) return false;
            pending.set(slot_of(depth++), child);
            expected_prev = child;
        }
    }
    return visited == oracle.count;
}

void sequence_follows_the_oracle(TestRunnerCtx const& test, Records const& state) {
    alloc::SlabHeap heap{};
    Oracle oracle{};
    bool is_min_right = true;
    bool is_sound = true;
    for (std::size_t step = 0; step < kSteps; ++step) {
        std::uint32_t const word = draw_of(step);
        std::size_t const pick = (word >> 4) % kSlabs;
        std::uint32_t const kind = word & 7;
        if (kind < 4 && oracle.count < kSlabs) {
            std::size_t const raw = oracle.find_from(slot_of(pick), Holding::not_held);
            heap.insert(state.records, link_at(raw));
            oracle.mark(slot_of(raw), Holding::held);
            ++oracle.count;
        } else if (kind < 6) {
            alloc::SlabLink const expected = oracle.lowest();
            alloc::SlabLink const popped = heap.pop_min(state.records);
            is_min_right = is_min_right && popped == expected;
            if (popped != alloc::SlabLink::none) {
                oracle.mark(slot_of(raw_of(popped)), Holding::not_held);
                --oracle.count;
            }
        } else if (oracle.count != 0) {
            std::size_t const raw = oracle.find_from(slot_of(pick), Holding::held);
            heap.remove(state.records, link_at(raw));
            oracle.mark(slot_of(raw), Holding::not_held);
            --oracle.count;
        }
        is_min_right = is_min_right && heap.min() == oracle.lowest();
        if (step % kWalkEvery == 0) {
            is_sound = is_sound && is_tree_sound(heap, state.records, oracle)
                    && heap.count(state.records).raw() == oracle.count;
        }
    }
    CRUCIBLE_TEST_ASSERT(test, is_min_right);
    CRUCIBLE_TEST_ASSERT(test, is_sound && is_tree_sound(heap, state.records, oracle));
    // The drain gives the held slabs in ascending order.
    bool is_ascending = true;
    std::size_t drained = 0;
    for (std::size_t raw = 0; raw < kSlabs; ++raw) {
        if (!oracle.held.at(slot_of(raw))) continue;
        alloc::SlabLink const popped = heap.pop_min(state.records);
        is_ascending = is_ascending && popped == link_at(raw);
        ++drained;
    }
    CRUCIBLE_TEST_ASSERT(test, is_ascending && drained == oracle.count);
    CRUCIBLE_TEST_ASSERT(test, heap.is_empty() && heap.pop_min(state.records) == alloc::SlabLink::none);
    pass(test, "  sequence_follows_the_oracle: PASSED\n");
}

// The removal of the root, of a first child and of a later sibling.  Six
// inserts in ascending order make the root 0 with the children 5, 4, 3, 2
// and 1: each insert becomes the first child.  So 5 is the first child and 3
// a later sibling.
void removal_of_each_place(TestRunnerCtx const& test, Records const& state) {
    alloc::SlabHeap heap{};
    Oracle oracle{};
    for (std::size_t raw = 0; raw < 6; ++raw) {
        heap.insert(state.records, link_at(raw));
        oracle.mark(slot_of(raw), Holding::held);
        ++oracle.count;
    }
    CRUCIBLE_TEST_ASSERT(test, state.records.child_of(link_at(0)) == link_at(5));
    CRUCIBLE_TEST_ASSERT(test, is_tree_sound(heap, state.records, oracle));
    heap.remove(state.records, link_at(5));
    heap.remove(state.records, link_at(3));
    heap.remove(state.records, link_at(0));
    oracle.mark(slot_of(5), Holding::not_held);
    oracle.mark(slot_of(3), Holding::not_held);
    oracle.mark(slot_of(0), Holding::not_held);
    oracle.count -= 3;
    CRUCIBLE_TEST_ASSERT(test, is_tree_sound(heap, state.records, oracle));
    CRUCIBLE_TEST_ASSERT(test, heap.count(state.records).raw() == 3);
    alloc::SlabLink const first = heap.pop_min(state.records);
    alloc::SlabLink const second = heap.pop_min(state.records);
    alloc::SlabLink const third = heap.pop_min(state.records);
    CRUCIBLE_TEST_ASSERT(test, first == link_at(1) && second == link_at(2) && third == link_at(4));
    CRUCIBLE_TEST_ASSERT(test, heap.is_empty());
    // Each slab leaves the heap with no link.
    bool is_unlinked = true;
    for (std::size_t raw = 0; raw < 6; ++raw) {
        alloc::SlabLink const slab = link_at(raw);
        is_unlinked = is_unlinked && state.records.child_of(slab) == alloc::SlabLink::none
                   && state.records.next_of(slab) == alloc::SlabLink::none
                   && state.records.prev_of(slab) == alloc::SlabLink::none;
    }
    CRUCIBLE_TEST_ASSERT(test, is_unlinked);
    pass(test, "  removal_of_each_place: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    Records const state = start_records();
    removal_of_each_place(test, state);
    sequence_follows_the_oracle(test, state);
    pass(test, "test_alloc_slab_heap: PASSED\n");
    return 0;
}
