// Tests of foundation/core/Array.h: HeapArray and mint_heap_array.

#include <foundation/core/Array.h>

#include <foundation/Platform.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include "abort_probe.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::foundation::core::HeapArray;
using ::foundation::core::mint_heap_array;

// The allocation tag passes the gate of the mint, and a different tag does
// not.
static_assert(::foundation::core::CanMintHeapArray<int, alignof(int), ::foundation::effects::Alloc>);
static_assert(!::foundation::core::CanMintHeapArray<int, alignof(int), ::foundation::effects::IO>);

// The count of live CountedCopy objects.  An array must end each element
// one time and leak none.
int live_copies = 0;

class CountedCopy {
public:
    explicit CountedCopy(int label) noexcept : label_{label} { ++live_copies; }
    CountedCopy(CountedCopy const& other) noexcept : label_{other.label_} { ++live_copies; }
    CountedCopy& operator=(CountedCopy const&) = delete;
    ~CountedCopy() { --live_copies; }

    [[nodiscard]] int label() const noexcept { return label_; }

private:
    int label_ = 0;
};

void test_empty_array() {
    ::foundation::effects::Alloc const alloc{};
    HeapArray<std::uint32_t> const empty = mint_heap_array(alloc, 0, std::uint32_t{5});
    CRUCIBLE_FATAL_INVARIANT(empty.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(empty.view().size() == 0);
    CRUCIBLE_FATAL_INVARIANT(!empty.get(0).is_some());
    int turns = 0;
    for (std::uint32_t const element : empty) {
        static_cast<void>(element);
        ++turns;
    }
    CRUCIBLE_FATAL_INVARIANT(turns == 0);

    HeapArray<std::uint32_t> const unminted{};
    CRUCIBLE_FATAL_INVARIANT(unminted.size() == 0);
}

void test_fill_and_reads() {
    ::foundation::effects::Alloc const alloc{};
    HeapArray<std::uint32_t> numbers = mint_heap_array(alloc, 1000, std::uint32_t{7});
    CRUCIBLE_FATAL_INVARIANT(numbers.size() == 1000);
    for (std::uint32_t const element : numbers) {
        CRUCIBLE_FATAL_INVARIANT(element == 7);
    }
    ::foundation::core::fill(numbers.view(), std::uint32_t{3});
    std::size_t total = 0;
    for (std::uint32_t const element : numbers) {
        total += element;
    }
    CRUCIBLE_FATAL_INVARIANT(total == 3000);

    // get gives the element below the count, and no value at the count.
    CRUCIBLE_FATAL_INVARIANT(*numbers.get(999).expect("the position is below the count") == 3);
    CRUCIBLE_FATAL_INVARIANT(!numbers.get(1000).is_some());
    *numbers.get(10).expect("the position is below the count") = 11;
    HeapArray<std::uint32_t> const& fixed = numbers;
    CRUCIBLE_FATAL_INVARIANT(*fixed.get(10).expect("the position is below the count") == 11);

    // The reverse loop reads the last element first.
    for (std::uint32_t const element : numbers.reverse()) {
        CRUCIBLE_FATAL_INVARIANT(element == 3);
        break;
    }
}

template <std::size_t Alignment>
void test_alignment() {
    ::foundation::effects::Alloc const alloc{};
    for (std::size_t count = 1; count < 70; count += 23) {
        HeapArray<std::uint8_t, Alignment> bytes = mint_heap_array<std::uint8_t, Alignment>(alloc, count, 0);
        ::foundation::core::Ref<std::uint8_t> const first = bytes.get(0).expect("the array holds an element");
        CRUCIBLE_FATAL_INVARIANT(::foundation::core::address_of(first).template is_aligned<Alignment>());
        CRUCIBLE_FATAL_INVARIANT(bytes.size() == count);
    }
}

void test_each_alignment() {
    test_alignment<1>();
    test_alignment<16>();
    test_alignment<64>();
    test_alignment<4096>();
}

void test_destruction_counts() {
    ::foundation::effects::Alloc const alloc{};
    {
        CountedCopy const fill{4};
        HeapArray<CountedCopy> first = mint_heap_array(alloc, 5, fill);
        CRUCIBLE_FATAL_INVARIANT(live_copies == 6);
        HeapArray<CountedCopy> second{std::move(first)};
        CRUCIBLE_FATAL_INVARIANT(live_copies == 6);
        CRUCIBLE_FATAL_INVARIANT(second.size() == 5);

        HeapArray<CountedCopy> third = mint_heap_array(alloc, 2, fill);
        CRUCIBLE_FATAL_INVARIANT(live_copies == 8);
        // The assignment ends each element that the target held.
        third = std::move(second);
        CRUCIBLE_FATAL_INVARIANT(live_copies == 6);
        CRUCIBLE_FATAL_INVARIANT(third.size() == 5);
        for (CountedCopy const& element : third) {
            CRUCIBLE_FATAL_INVARIANT(element.label() == 4);
        }

        HeapArray<CountedCopy>& alias = third;
        third = std::move(alias);
        CRUCIBLE_FATAL_INVARIANT(live_copies == 6);
        CRUCIBLE_FATAL_INVARIANT(third.size() == 5);
    }
    CRUCIBLE_FATAL_INVARIANT(live_copies == 0);
}

void test_equality() {
    ::foundation::effects::Alloc const alloc{};
    HeapArray<int> const left = mint_heap_array(alloc, 4, 2);
    HeapArray<int> right = mint_heap_array(alloc, 4, 2);
    HeapArray<int> const shorter = mint_heap_array(alloc, 3, 2);
    CRUCIBLE_FATAL_INVARIANT(left == right);
    CRUCIBLE_FATAL_INVARIANT(!(left == shorter));
    *right.get(3).expect("the position is below the count") = 5;
    CRUCIBLE_FATAL_INVARIANT(!(left == right));
}

// A count whose View would be longer than the largest View ends the process
// before the allocation.
void test_too_long_aborts() {
    bool const too_long_aborts = ::foundation::test::aborts([] {
        HeapArray<std::uint64_t> const huge =
            mint_heap_array(::foundation::effects::Alloc{}, ~std::size_t{0} / 4, std::uint64_t{0});
        static_cast<void>(huge);
    });
    CRUCIBLE_FATAL_INVARIANT(too_long_aborts);
}

// The property: an array of a random count and a random fill holds the fill
// at each position, get agrees with the count, and a write through get
// shows in the loop.  A plain count and a plain sum are the model.
void test_agrees_with_plain_model() {
    ::foundation::effects::Alloc const alloc{};
    ::foundation::test::PhiloxStream stream{0x5EEDA77A00000001u};
    for (int trial = 0; trial < 256; ++trial) {
        std::size_t const count = stream.below(300);
        std::uint32_t const fill = static_cast<std::uint32_t>(stream.next_wide());
        HeapArray<std::uint32_t> array = mint_heap_array(alloc, count, fill);
        CRUCIBLE_FATAL_INVARIANT(array.size() == count);
        std::uint64_t model_sum = static_cast<std::uint64_t>(fill) * count;
        if (count != 0) {
            std::size_t const position = stream.below(count);
            *array.get(position).expect("the position is below the count") = fill + 1;
            model_sum += 1;
        }
        std::uint64_t sum = 0;
        for (std::uint32_t const element : array) {
            sum += element;
        }
        CRUCIBLE_FATAL_INVARIANT(sum == model_sum);
        CRUCIBLE_FATAL_INVARIANT(!array.get(count).is_some());
    }
}

}  // namespace

int main() {
    test_empty_array();
    test_fill_and_reads();
    test_each_alignment();
    test_destruction_counts();
    test_equality();
    test_too_long_aborts();
    test_agrees_with_plain_model();
    return 0;
}
