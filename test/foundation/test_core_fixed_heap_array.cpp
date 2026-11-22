// Tests of foundation/core/FixedHeapArray.h: an owner of exactly N elements on
// the heap, and its mint.

#include <foundation/core/FixedHeapArray.h>

#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <fixy/check/Check.h>

#include <cstddef>
#include <cstdint>

namespace {

using ::foundation::core::FixedHeapArray;
using ::foundation::core::index;
using ::foundation::core::mint_fixed_heap_array;

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

// The mint gives N copies of the fill, and the reads give them through the
// View of a fixed extent, the loop and at().
void test_fill_and_reads() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    FixedHeapArray<std::uint32_t, 1000> const numbers = mint_fixed_heap_array<std::uint32_t, 1000>(alloc, 7u);
    static_assert(decltype(numbers)::size() == 1000);
    static_assert(decltype(numbers.view())::extent == 1000);
    std::uint64_t total = 0;
    for (std::uint32_t const element : numbers) {
        total += element;
    }
    CRUCIBLE_FATAL_INVARIANT(total == 7000);
    CRUCIBLE_FATAL_INVARIANT(numbers.at(index<999>()) == 7);
    CRUCIBLE_FATAL_INVARIANT(numbers.view().size() == 1000);
}

// The element door writes one element in place, and the frame door writes
// the run in place: the run of the body lies at the elements of the array.
// The door of a const array reads.
void test_doors_write_in_place() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    FixedHeapArray<std::uint64_t, 4> words = mint_fixed_heap_array<std::uint64_t, 4>(alloc, std::uint64_t{1});
    int const steps = words.with_element(index<2>(), [](std::uint64_t& word) noexcept {
        word |= 16;
        return 1;
    });
    FixedHeapArray<std::uint64_t, 4> const& read = words;
    std::uint64_t const third = read.with_element(index<2>(), [](std::uint64_t const& word) noexcept { return word; });
    CRUCIBLE_FATAL_INVARIANT(steps == 1 && third == 17);

    ::foundation::core::View<std::uint64_t const, 4> const elements = words.view();
    bool const is_in_place = words.with_mut([elements](auto run) noexcept {
        static_assert(decltype(run)::extent == 4);
        std::uint64_t next = 10;
        for (std::uint64_t& word : run) {
            word = next++;
        }
        return ::foundation::core::offset_of(elements, ::foundation::core::View<std::uint64_t const>{run.view()})
            .is_some();
    });
    CRUCIBLE_FATAL_INVARIANT(is_in_place);
    CRUCIBLE_FATAL_INVARIANT(words.at(index<0>()) == 10 && words.at(index<3>()) == 13);
}

// The storage of the array has the alignment Align.
void test_alignment() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    FixedHeapArray<std::uint8_t, 3, 4096> const bytes = mint_fixed_heap_array<std::uint8_t, 3, 4096>(alloc, 0);
    CRUCIBLE_FATAL_INVARIANT(
        ::foundation::core::address_of(::foundation::core::to(bytes.at(index<0>()))).is_aligned<4096>());
}

// The end of the array ends each element one time.
void test_destruction_counts() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        CountedCopy const fill{4};
        FixedHeapArray<CountedCopy, 5> const copies = mint_fixed_heap_array<CountedCopy, 5>(alloc, fill);
        CRUCIBLE_FATAL_INVARIANT(live_copies == 6);
        CRUCIBLE_FATAL_INVARIANT(copies.at(index<4>()).label() == 4);
    }
    CRUCIBLE_FATAL_INVARIANT(live_copies == 0);
}

// A ring of a million records, the shape of the metadata log: a write of a
// masked slot through the frame door, and a read of it through at().
void test_large_ring() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    constexpr std::size_t kCount = std::size_t{1} << 20;
    FixedHeapArray<std::uint64_t, kCount> ring = mint_fixed_heap_array<std::uint64_t, kCount>(alloc, std::uint64_t{0});
    std::uint64_t const slot = (kCount + 5) & (kCount - 1);
    ring.with_mut([slot](auto run) noexcept {
        run.view().window(slot, 1).match(
            [](::foundation::core::View<std::uint64_t> one) noexcept {
                for (std::uint64_t& word : one) {
                    word = 9;
                }
            },
            [] noexcept { ::foundation::core::fatal("a masked slot is below the count"); });
    });
    CRUCIBLE_FATAL_INVARIANT(ring.at(::foundation::core::Index<kCount>::check(5).expect("5 is below the count")) == 9);
}

}  // namespace

int main() {
    test_fill_and_reads();
    test_doors_write_in_place();
    test_alignment();
    test_destruction_counts();
    test_large_ring();
    return 0;
}
