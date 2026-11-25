// Tests of foundation/core/FixedHeapArray.h: an owner of exactly N elements on
// the heap, and its mint.

#include <foundation/core/FixedHeapArray.h>

#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <fixy/check/Check.h>

#include <cstddef>
#include <cstdint>
#include <utility>

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

// The element door of a keyed array writes one element in place, and the frame
// door writes the run in place: the run of the body lies at the elements of
// the array.  The door of a const array reads.
void test_doors_write_in_place() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto [words, key] = ::foundation::core::mint_keyed_owner(
        ::foundation::core::fixed_heap_array_owner<std::uint64_t, 4>, alloc, std::uint64_t{1});
    int const steps = words.with_element(key, index<2>(), [](std::uint64_t& word) noexcept {
        word |= 16;
        return 1;
    });
    auto const& read = words;
    std::uint64_t const third = read.with_element(index<2>(), [](std::uint64_t const& word) noexcept { return word; });
    CRUCIBLE_FATAL_INVARIANT(steps == 1 && third == 17);

    ::foundation::core::View<std::uint64_t const, 4> const elements = words.view();
    bool const is_in_place = words.with_mut(key, [elements](auto run) noexcept {
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

// The element door of an array with no key gives an element that a body reads,
// and an element that two threads can write at the same time.
void test_element_door_with_no_key() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    FixedHeapArray<std::uint64_t, 4> words = mint_fixed_heap_array<std::uint64_t, 4>(alloc, std::uint64_t{7});
    std::uint64_t const third = words.with_element(index<2>(), [](std::uint64_t const& word) noexcept { return word; });
    CRUCIBLE_FATAL_INVARIANT(third == 7);
}

// The value doors write one element and the run in place.  A copy from a View
// of the same array gives the values that the View held before the copy, and a
// window that does not fit writes nothing.
void test_value_doors() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    FixedHeapArray<std::uint64_t, 6> words = mint_fixed_heap_array<std::uint64_t, 6>(alloc, std::uint64_t{1});
    words.set(index<1>(), 4);
    std::uint64_t const held = words.exchange(index<2>(), 9);
    CRUCIBLE_FATAL_INVARIANT(held == 1 && words.at(index<1>()) == 4 && words.at(index<2>()) == 9);
    ::foundation::core::FixedArray<std::uint64_t, 6> const source{1, 2, 3, 4, 5, 6};
    words.copy_from(source.view());
    CRUCIBLE_FATAL_INVARIANT(::foundation::core::equal(words.view(), source.view()));
    CRUCIBLE_FATAL_INVARIANT(words.copy_window(2, words.view().window(0, 4).expect("four elements fit")).is_ok());
    ::foundation::core::FixedArray<std::uint64_t, 6> const moved{1, 2, 1, 2, 3, 4};
    CRUCIBLE_FATAL_INVARIANT(::foundation::core::equal(words.view(), moved.view()));
    CRUCIBLE_FATAL_INVARIANT(words.copy_window(3, words.view().window(0, 4).expect("four elements fit")).is_err());
    CRUCIBLE_FATAL_INVARIANT(::foundation::core::equal(words.view(), moved.view()));
    words.fill(8);
    CRUCIBLE_FATAL_INVARIANT(words.at(index<0>()) == 8 && words.at(index<5>()) == 8);
    std::uint64_t const doubled = words.update(index<5>(), [](std::uint64_t& word) noexcept {
        word |= 16;
        return word * 2;
    });
    CRUCIBLE_FATAL_INVARIANT(doubled == 48 && words.at(index<5>()) == 24 && words.at(index<4>()) == 8);
}

// The maker of a keyed heap array gives an array of the copies of a value and
// the key of the brand of the mint.
void test_keyed_maker() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto [words, key] = ::foundation::core::mint_keyed_owner(
        ::foundation::core::fixed_heap_array_owner<std::uint64_t, 5>, alloc, std::uint64_t{3});
    std::uint64_t const total = words.with_mut(key, [](auto run) noexcept {
        std::uint64_t sum = 0;
        for (std::uint64_t& word : run) {
            sum += word;
            word = sum;
        }
        return sum;
    });
    CRUCIBLE_FATAL_INVARIANT(total == 15 && words.at(index<0>()) == 3 && words.at(index<4>()) == 15);
}

// A keyed array opens its frames with the key of its brand: by move, and by a
// borrow of the key.  The key comes from the mint of a keyed FixedArray, which
// gives a key of the brand of its call site, and the heap array takes that
// brand.  The writes are in place, and the key comes back from each door.
void test_keyed_doors() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto [paired, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 1>);
    using Brand = typename decltype(key)::brand_type;
    FixedHeapArray<std::uint64_t, 4, alignof(std::uint64_t), Brand> words =
        mint_fixed_heap_array<std::uint64_t, 4, alignof(std::uint64_t), Brand>(alloc, std::uint64_t{1});
    key = words.with_mut(std::move(key), [](auto run) noexcept {
        for (std::uint64_t& word : run) {
            word = 5;
        }
    });
    key = words.with_element(std::move(key), index<1>(), [](std::uint64_t& word) noexcept { word = 6; });
    std::uint64_t const second = words.with_element(key, index<1>(), [](std::uint64_t& word) noexcept { return word; });
    std::uint64_t const total = words.with_mut(key, [](auto run) noexcept {
        std::uint64_t sum = 0;
        for (std::uint64_t const& word : run) {
            sum += word;
        }
        return sum;
    });
    CRUCIBLE_FATAL_INVARIANT(second == 6 && total == 21);
    CRUCIBLE_FATAL_INVARIANT(words.at(index<0>()) == 5 && paired.at(index<0>()) == 0);
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
    auto [ring, key] = ::foundation::core::mint_keyed_owner(
        ::foundation::core::fixed_heap_array_owner<std::uint64_t, kCount>, alloc, std::uint64_t{0});
    std::uint64_t const slot = (kCount + 5) & (kCount - 1);
    ring.with_mut(key, [slot](auto run) noexcept {
        take_window(std::move(run), slot, 1)
            .match(
                [](auto one) noexcept {
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
    test_element_door_with_no_key();
    test_value_doors();
    test_keyed_maker();
    test_keyed_doors();
    test_alignment();
    test_destruction_counts();
    test_large_ring();
    return 0;
}
