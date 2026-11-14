// Tests of foundation/core/Array.h: MaskedArray and mint_masked_array.

#include <foundation/core/Array.h>

#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include "abort_probe.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::core::MaskedArray;
using ::foundation::core::mint_masked_array;

// A slot of three atomic cells, as an open-addressing table holds it.  It
// does not copy and does not move.
struct PinnedSlot {
    ::foundation::core::Atomic<std::uint64_t> key{};
    ::foundation::core::Atomic<std::uint64_t> row{};
    ::foundation::core::Atomic<std::uint64_t> value{};
};

// The allocation tag passes the gate of the mint, and a different tag does
// not.  A pinned slot passes the gate of the masked array, and not the gate
// of the heap array, which copies a fill.
static_assert(::foundation::core::CanMintMaskedArray<std::uint64_t, 8, ::foundation::effects::Alloc>);
static_assert(!::foundation::core::CanMintMaskedArray<std::uint64_t, 8, ::foundation::effects::IO>);
static_assert(!std::is_copy_constructible_v<PinnedSlot> && !std::is_move_constructible_v<PinnedSlot>);
static_assert(::foundation::core::CanMintMaskedArray<PinnedSlot, 64, ::foundation::effects::Alloc>);
static_assert(!::foundation::core::CanMintHeapArray<PinnedSlot, 64, ::foundation::effects::Alloc>);

// The count of live Counted objects.  An array must end each element one
// time and leak none.
int live_elements = 0;

class Counted {
public:
    Counted() noexcept { ++live_elements; }
    Counted(Counted const&) = delete;
    Counted& operator=(Counted const&) = delete;
    ~Counted() { --live_elements; }

    int label = 0;
};

void test_empty_array() {
    MaskedArray<std::uint64_t> const empty{};
    CRUCIBLE_FATAL_INVARIANT(empty.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(empty.view().size() == 0);
}

// Each count gives that count of zeros, and a key at or past the count reads
// the element of the key modulo the count.
void test_counts_and_wrapping_keys() {
    ::foundation::effects::Alloc const alloc{};
    ::foundation::core::FixedArray<std::size_t, 5> const counts{std::size_t{1}, std::size_t{2}, std::size_t{4},
                                                                std::size_t{64}, std::size_t{1} << 16};
    for (std::size_t const count : counts) {
        MaskedArray<std::uint64_t> array = mint_masked_array<std::uint64_t>(alloc, count);
        CRUCIBLE_FATAL_INVARIANT(array.size() == count);
        CRUCIBLE_FATAL_INVARIANT(array.view().size() == count);
        for (std::uint64_t const element : array.view()) {
            CRUCIBLE_FATAL_INVARIANT(element == 0);
        }
        for (std::uint64_t position = 0; position < count; ++position) {
            array.at_masked(position) = position + 1;
        }
        for (std::uint64_t position = 0; position < count; ++position) {
            CRUCIBLE_FATAL_INVARIANT(array.at_masked(position + count) == position + 1);
            CRUCIBLE_FATAL_INVARIANT(array.at_masked(position + 5 * count) == position + 1);
            CRUCIBLE_FATAL_INVARIANT(array.at_masked(position | (std::uint64_t{1} << 63)) == position + 1);
        }
        // The largest key masks to the last element.
        CRUCIBLE_FATAL_INVARIANT(array.at_masked(~std::uint64_t{0}) == count);
        MaskedArray<std::uint64_t> const& fixed = array;
        CRUCIBLE_FATAL_INVARIANT(fixed.at_masked(count + count - 1) == count);
    }
}

template <std::size_t Alignment>
void test_alignment() {
    ::foundation::effects::Alloc const alloc{};
    for (std::size_t count = 1; count <= 256; count *= 4) {
        MaskedArray<std::uint8_t, Alignment> bytes = mint_masked_array<std::uint8_t, Alignment>(alloc, count);
        auto const first = bytes.view().get(0).expect("the array holds an element");
        CRUCIBLE_FATAL_INVARIANT(::foundation::core::address_of(first).template is_aligned<Alignment>());
        CRUCIBLE_FATAL_INVARIANT(bytes.size() == count);
    }
}

void test_each_alignment() {
    test_alignment<1>();
    test_alignment<64>();
    test_alignment<4096>();
}

// A table of pinned slots: a write at one key shows at each key with the same
// low bits.
void test_pinned_slots() {
    ::foundation::effects::Alloc const alloc{};
    MaskedArray<PinnedSlot, 64> table = mint_masked_array<PinnedSlot, 64>(alloc, 16);
    CRUCIBLE_FATAL_INVARIANT(table.size() == 16);
    for (PinnedSlot const& slot : table.view()) {
        CRUCIBLE_FATAL_INVARIANT(slot.key.load_acquire() == 0);
        CRUCIBLE_FATAL_INVARIANT(slot.value.load_acquire() == 0);
    }
    table.at_masked(5).key.store_release(0xABCD);
    table.at_masked(5 + 16 * 3).value.store_release(7);
    CRUCIBLE_FATAL_INVARIANT(table.at_masked(21).key.load_acquire() == 0xABCD);
    CRUCIBLE_FATAL_INVARIANT(table.at_masked(5).value.load_acquire() == 7);
    CRUCIBLE_FATAL_INVARIANT(table.at_masked(6).key.load_acquire() == 0);
}

void test_move_and_destruction_counts() {
    ::foundation::effects::Alloc const alloc{};
    {
        MaskedArray<Counted> first = mint_masked_array<Counted>(alloc, 8);
        CRUCIBLE_FATAL_INVARIANT(live_elements == 8);
        first.at_masked(3).label = 30;

        // A move gives the storage to the target and leaves the source empty.
        MaskedArray<Counted> second{std::move(first)};
        CRUCIBLE_FATAL_INVARIANT(live_elements == 8);
        CRUCIBLE_FATAL_INVARIANT(second.size() == 8);
        CRUCIBLE_FATAL_INVARIANT(second.at_masked(11).label == 30);

        // The assignment ends each element that the target held.
        MaskedArray<Counted> third = mint_masked_array<Counted>(alloc, 2);
        CRUCIBLE_FATAL_INVARIANT(live_elements == 10);
        third = std::move(second);
        CRUCIBLE_FATAL_INVARIANT(live_elements == 8);
        CRUCIBLE_FATAL_INVARIANT(third.size() == 8);
        CRUCIBLE_FATAL_INVARIANT(third.at_masked(3).label == 30);

        MaskedArray<Counted>& alias = third;
        third = std::move(alias);
        CRUCIBLE_FATAL_INVARIANT(live_elements == 8);
        CRUCIBLE_FATAL_INVARIANT(third.size() == 8);
    }
    CRUCIBLE_FATAL_INVARIANT(live_elements == 0);
}

// A count that is zero, is not a power of two, or is above 2^31 ends the
// process before the allocation.
void test_bad_counts_abort() {
    ::foundation::core::FixedArray<std::size_t, 5> const counts{std::size_t{0}, std::size_t{3}, std::size_t{6},
                                                                std::size_t{1000}, std::size_t{1} << 32};
    for (std::size_t const count : counts) {
        bool const bad_count_aborts = ::crucible::test::aborts([count] {
            MaskedArray<std::uint64_t> const refused =
                mint_masked_array<std::uint64_t>(::foundation::effects::Alloc{}, count);
            static_cast<void>(refused);
        });
        CRUCIBLE_FATAL_INVARIANT(bad_count_aborts);
    }
}

// The property: for a random power-of-two count and random keys, a write at
// a key reads back at each key with the same low bits.  A plain array that the
// test indexes with key % count is the model.
void test_agrees_with_plain_model() {
    ::foundation::effects::Alloc const alloc{};
    ::crucible::test::PhiloxStream stream{0x5EEDA77A00000002u};
    for (int trial = 0; trial < 128; ++trial) {
        std::size_t const count = std::size_t{1} << stream.below(12);
        MaskedArray<std::uint64_t> array = mint_masked_array<std::uint64_t>(alloc, count);
        ::foundation::core::HeapArray<std::uint64_t> model =
            ::foundation::core::mint_heap_array(alloc, count, std::uint64_t{0});
        for (int step = 0; step < 64; ++step) {
            std::uint64_t const key = stream.next_wide();
            std::uint64_t const value = stream.next_wide();
            array.at_masked(key) = value;
            *model.get(key % count).expect("a key modulo the count is below the count") = value;
        }
        for (int step = 0; step < 64; ++step) {
            std::uint64_t const key = stream.next_wide();
            CRUCIBLE_FATAL_INVARIANT(array.at_masked(key)
                                     == *model.get(key % count).expect("a key modulo the count is below the count"));
        }
    }
}

}  // namespace

int main() {
    test_empty_array();
    test_counts_and_wrapping_keys();
    test_each_alignment();
    test_pinned_slots();
    test_move_and_destruction_counts();
    test_bad_counts_abort();
    test_agrees_with_plain_model();
    return 0;
}
