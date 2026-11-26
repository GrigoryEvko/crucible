// Tests of foundation/core/Array.h: HeapArray and its five mints.

#include <foundation/core/Array.h>

#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::foundation::core::HeapArray;
using ::foundation::core::mint_heap_array;
using ::foundation::core::mint_heap_array_copy;
using ::foundation::core::mint_heap_array_with;
using ::foundation::core::mint_heap_array_written;
using ::foundation::core::mint_heap_array_zeroed;
using ::foundation::core::WriteLease;

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
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
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
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    HeapArray<std::uint32_t> numbers = mint_heap_array(alloc, 1000, std::uint32_t{7});
    CRUCIBLE_FATAL_INVARIANT(numbers.size() == 1000);
    for (std::uint32_t const element : numbers) {
        CRUCIBLE_FATAL_INVARIANT(element == 7);
    }
    numbers = ::foundation::core::with_mut(
        std::move(numbers), [](auto run) noexcept { ::foundation::core::fill(std::move(run), std::uint32_t{3}); });
    std::size_t total = 0;
    for (std::uint32_t const element : numbers) {
        total += element;
    }
    CRUCIBLE_FATAL_INVARIANT(total == 3000);

    // get gives the element below the count, and no value at the count.
    CRUCIBLE_FATAL_INVARIANT(*numbers.get(999).expect("the position is below the count") == 3);
    CRUCIBLE_FATAL_INVARIANT(!numbers.get(1000).is_some());
    numbers.with_element(
        10, [](std::uint32_t& element) noexcept { element = 11; },
        [] noexcept { ::foundation::core::fatal("the position is below the count"); });
    HeapArray<std::uint32_t> const& fixed = numbers;
    CRUCIBLE_FATAL_INVARIANT(*fixed.get(10).expect("the position is below the count") == 11);

    // The reverse loop reads the last element first.
    for (std::uint32_t const element : numbers.reverse()) {
        CRUCIBLE_FATAL_INVARIANT(element == 3);
        break;
    }
}

// The element door writes one element in place below the count and gives the
// result of on_some.  At the count and past it, on_none runs, and no element
// changes.  The door of a const array reads, and an empty array runs on_none
// for each position.
void test_element_door_writes_in_place() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    HeapArray<std::uint64_t> words = mint_heap_array(alloc, 4, std::uint64_t{1});
    int const steps = words.with_element(
        2,
        [](std::uint64_t& word) noexcept {
            word |= 16;
            return 1;
        },
        [] noexcept { return 0; });
    int const missed = words.with_element(
        4,
        [](std::uint64_t& word) noexcept {
            word = 0;
            return 1;
        },
        [] noexcept { return 0; });
    HeapArray<std::uint64_t> const& read = words;
    std::uint64_t const third = read.with_element(
        2, [](std::uint64_t const& word) noexcept { return word; }, [] noexcept { return std::uint64_t{0}; });
    CRUCIBLE_FATAL_INVARIANT(steps == 1 && missed == 0 && third == 17);
    std::uint64_t total = 0;
    for (std::uint64_t const word : words) {
        total += word;
    }
    CRUCIBLE_FATAL_INVARIANT(total == 1 + 1 + 17 + 1);

    HeapArray<std::uint64_t> empty{};
    bool const is_none =
        empty.with_element(0, [](std::uint64_t&) noexcept { return false; }, [] noexcept { return true; });
    CRUCIBLE_FATAL_INVARIANT(is_none);
}

template <std::size_t Alignment>
void test_alignment() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    for (std::size_t count = 1; count < 70; count += 23) {
        HeapArray<std::uint8_t, Alignment> bytes = mint_heap_array<std::uint8_t, Alignment>(alloc, count, 0);
        auto const first = bytes.get(0).expect("the array holds an element");
        CRUCIBLE_FATAL_INVARIANT(::foundation::core::address_of(first).template is_aligned<Alignment>());
        CRUCIBLE_FATAL_INVARIANT(bytes.size() == count);
    }
}

void test_each_alignment() {
    test_alignment<1>();
    test_alignment<16>();
    test_alignment<64>();
    test_alignment<4096>();
    // The alignment of a huge page: each element takes a whole aligned huge
    // page of storage.
    test_alignment<std::size_t{2} << 20>();
}

// A value that refuses a start over bytes, the shape of a provenance tag:
// only its own constructor builds it.  The fill mint and the mint that
// builds in place give it, and each element reads the value of its
// constructor.
struct[[= ::foundation::lifetime::no_start_over_bytes{}]] Stamped {
    std::uint64_t origin = 7;
};

void test_sealed_element() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    HeapArray<Stamped, 64> const filled = mint_heap_array<Stamped, 64>(alloc, 3, Stamped{});
    HeapArray<Stamped> const built =
        mint_heap_array_with<Stamped>(alloc, 3, [](std::size_t) noexcept { return Stamped{}; });
    for (Stamped const& element : filled) {
        CRUCIBLE_FATAL_INVARIANT(element.origin == 7);
    }
    for (Stamped const& element : built) {
        CRUCIBLE_FATAL_INVARIANT(element.origin == 7);
    }
}

// Moves the source into the target through two names, as an alias of two
// references does in a program.  The kind self-move of the rule must-consume
// refuses a self-move that one function names.
[[gnu::noinline]] void move_into(HeapArray<CountedCopy>& target, HeapArray<CountedCopy>& source) noexcept {
    target = std::move(source);
}

void test_destruction_counts() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
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

        move_into(third, third);
        CRUCIBLE_FATAL_INVARIANT(live_copies == 6);
        CRUCIBLE_FATAL_INVARIANT(third.size() == 5);
    }
    CRUCIBLE_FATAL_INVARIANT(live_copies == 0);
}

void test_equality() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    HeapArray<int> const left = mint_heap_array(alloc, 4, 2);
    HeapArray<int> right = mint_heap_array(alloc, 4, 2);
    HeapArray<int> const shorter = mint_heap_array(alloc, 3, 2);
    CRUCIBLE_FATAL_INVARIANT(left == right);
    CRUCIBLE_FATAL_INVARIANT(!(left == shorter));
    right.with_element(
        3, [](int& element) noexcept { element = 5; },
        [] noexcept { ::foundation::core::fatal("the position is below the count"); });
    CRUCIBLE_FATAL_INVARIANT(!(left == right));
}

// Two arrays order as the first pair of elements that differ, and a prefix
// orders first.  A double element orders by the totalOrder of IEEE 754, so
// -0.0 orders before +0.0, and the two arrays are not equal.
void test_order() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    HeapArray<int> const prefix = mint_heap_array(alloc, 3, 2);
    HeapArray<int> longer = mint_heap_array(alloc, 4, 2);
    CRUCIBLE_FATAL_INVARIANT(prefix < longer);
    CRUCIBLE_FATAL_INVARIANT(longer > prefix);
    longer.with_element(
        0, [](int& element) noexcept { element = 1; },
        [] noexcept { ::foundation::core::fatal("the position is below the count"); });
    CRUCIBLE_FATAL_INVARIANT(longer < prefix);
    HeapArray<double> const negative_zero = mint_heap_array(alloc, 2, -0.0);
    HeapArray<double> const positive_zero = mint_heap_array(alloc, 2, 0.0);
    CRUCIBLE_FATAL_INVARIANT(negative_zero < positive_zero);
    CRUCIBLE_FATAL_INVARIANT(!(negative_zero == positive_zero));
    // The canonical NaN equals itself, by the float rule of equal.
    HeapArray<double> const first_nan = mint_heap_array(alloc, 2, ::foundation::core::quiet_nan_of<double>);
    HeapArray<double> const second_nan = mint_heap_array(alloc, 2, ::foundation::core::quiet_nan_of<double>);
    CRUCIBLE_FATAL_INVARIANT(first_nan == second_nan);
}

// A count whose View would be longer than the largest View ends the process
// before the allocation.  The mint runs in a child through expect_dies of the
// Check family.
void test_too_long_aborts(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            HeapArray<std::uint64_t> const huge =
                mint_heap_array(::foundation::effects::testing::test().alloc, ~std::size_t{0} / 4, std::uint64_t{0});
            static_cast<void>(huge);
        },
        ::foundation::core::TextView{
            "foundation: fatal: a HeapArray of 4611686018427387903 elements of 8 bytes is longer than the "
            "largest View"});
}

// The property: an array of a random count and a random fill holds the fill
// at each position, get agrees with the count, and a write through get
// shows in the loop.  A plain count and a plain sum are the model.
void test_agrees_with_plain_model() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::crucible::test::PhiloxStream stream{0x5EEDA77A00000001u};
    for (int trial = 0; trial < 256; ++trial) {
        std::size_t const count = stream.below(300);
        std::uint32_t const fill = static_cast<std::uint32_t>(stream.next_wide());
        HeapArray<std::uint32_t> array = mint_heap_array(alloc, count, fill);
        CRUCIBLE_FATAL_INVARIANT(array.size() == count);
        std::uint64_t model_sum = static_cast<std::uint64_t>(fill) * count;
        if (count != 0) {
            std::size_t const position = stream.below(count);
            array.with_element(
                position, [fill](std::uint32_t& element) noexcept { element = fill + 1; },
                [] noexcept { ::foundation::core::fatal("the position is below the count"); });
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

// An element whose address is its identity.  It keeps the address of its
// construction, and it does not copy or move.
class Anchored {
public:
    explicit Anchored(std::size_t position) noexcept
        : home_{::foundation::core::address_of(::foundation::core::to(*this))}, position_{position} {}
    Anchored(Anchored const&) = delete;
    Anchored(Anchored&&) = delete;
    Anchored& operator=(Anchored const&) = delete;
    Anchored& operator=(Anchored&&) = delete;

    [[nodiscard]] bool is_at_home() const noexcept {
        return ::foundation::core::address_of(::foundation::core::to(*this)) == home_;
    }
    [[nodiscard]] std::size_t position() const noexcept { return position_; }

private:
    ::foundation::core::Address home_;
    std::size_t position_ = 0;
};

// mint_heap_array_with builds each element at its home, from the result of
// init at its position, in the order of the positions.
void test_with_builds_in_place() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    std::size_t calls = 0;
    HeapArray<Anchored> const anchored = mint_heap_array_with<Anchored>(alloc, 37, [&](std::size_t position) noexcept {
        CRUCIBLE_FATAL_INVARIANT(position == calls);
        ++calls;
        return Anchored{position};
    });
    CRUCIBLE_FATAL_INVARIANT(calls == 37 && anchored.size() == 37);
    std::size_t expected = 0;
    for (Anchored const& element : anchored) {
        CRUCIBLE_FATAL_INVARIANT(element.is_at_home());
        CRUCIBLE_FATAL_INVARIANT(element.position() == expected);
        ++expected;
    }

    // An Atomic is pinned, and each cell builds in place at the alignment of
    // a cache line.
    using Cell = ::foundation::core::Atomic<std::uint64_t>;
    HeapArray<Cell, 64> const cells =
        mint_heap_array_with<Cell, 64>(alloc, 5, [](std::size_t position) noexcept { return Cell{position * 3}; });
    std::uint64_t total = 0;
    for (Cell const& cell : cells) {
        total += cell.load_acquire();
    }
    CRUCIBLE_FATAL_INVARIANT(total == 3 * (0 + 1 + 2 + 3 + 4));
    CRUCIBLE_FATAL_INVARIANT(
        ::foundation::core::address_of(cells.get(0).expect("the array holds a cell")).is_aligned<64>());

    // A count of zero calls init no time and allocates nothing.
    HeapArray<Anchored> const empty = mint_heap_array_with<Anchored>(alloc, 0, [&](std::size_t position) noexcept {
        ++calls;
        return Anchored{position};
    });
    CRUCIBLE_FATAL_INVARIANT(empty.size() == 0 && calls == 37);

    // The array ends each element that init built.
    {
        CountedCopy const label{9};
        HeapArray<CountedCopy> const counted = mint_heap_array_with<CountedCopy>(
            alloc, 4, [](std::size_t position) noexcept { return CountedCopy{static_cast<int>(position)}; });
        CRUCIBLE_FATAL_INVARIANT(live_copies == 5);
        CRUCIBLE_FATAL_INVARIANT(counted.get(3).expect("the position is below the count")->label() == 3);
    }
    CRUCIBLE_FATAL_INVARIANT(live_copies == 0);
}

// mint_heap_array_copy copies each element of a View, and the copy owns its
// own storage.
void test_copy_of_view() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    HeapArray<std::uint32_t> source = mint_heap_array(alloc, 9, std::uint32_t{4});
    source.with_element(
        8, [](std::uint32_t& element) noexcept { element = 11; },
        [] noexcept { ::foundation::core::fatal("the position is below the count"); });
    HeapArray<std::uint32_t> copied = mint_heap_array_copy<std::uint32_t>(alloc, source.view());
    CRUCIBLE_FATAL_INVARIANT(copied == source);
    copied.with_element(
        0, [](std::uint32_t& element) noexcept { element = 2; },
        [] noexcept { ::foundation::core::fatal("the position is below the count"); });
    CRUCIBLE_FATAL_INVARIANT(*source.get(0).expect("the position is below the count") == 4);

    HeapArray<std::uint32_t, 64> const window =
        mint_heap_array_copy<std::uint32_t, 64>(alloc, source.view().window(7, 2).expect("two elements fit"));
    CRUCIBLE_FATAL_INVARIANT(window.size() == 2);
    CRUCIBLE_FATAL_INVARIANT(*window.get(1).expect("the position is below the count") == 11);

    HeapArray<std::uint32_t> const nothing =
        mint_heap_array_copy<std::uint32_t>(alloc, ::foundation::core::View<std::uint32_t const>{});
    CRUCIBLE_FATAL_INVARIANT(nothing.size() == 0);
}

// mint_heap_array_zeroed gives zero in each element.  The bytes of an array
// that the test frees before the mint hold a pattern, so a block that the
// allocator gives again with that pattern fails the test.
void test_zeroed() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        HeapArray<std::uint64_t, 64> const poison = mint_heap_array<std::uint64_t, 64>(alloc, 512, 0xABABABABABABABABu);
        CRUCIBLE_FATAL_INVARIANT(poison.size() == 512);
    }
    HeapArray<std::uint64_t, 64> const zeros = mint_heap_array_zeroed<std::uint64_t, 64>(alloc, 512);
    CRUCIBLE_FATAL_INVARIANT(zeros.size() == 512);
    for (std::uint64_t const element : zeros) {
        CRUCIBLE_FATAL_INVARIANT(element == 0);
    }

    // The mint writes bytes and runs no constructor, so a member initializer
    // gives no value to an element.
    struct Sample {
        std::uint32_t count = 7;
        double weight = 1.5;
    };
    HeapArray<Sample> const samples = mint_heap_array_zeroed<Sample>(alloc, 3);
    for (Sample const& sample : samples) {
        CRUCIBLE_FATAL_INVARIANT(sample.count == 0);
        CRUCIBLE_FATAL_INVARIANT(::foundation::core::bit_cast<std::uint64_t>(sample.weight) == 0);
    }
    CRUCIBLE_FATAL_INVARIANT(mint_heap_array_zeroed<std::uint8_t>(alloc, 0).size() == 0);
}

// mint_heap_array_written gives the elements that the writer put through
// its lease, in order, one value at a time or one run at a time.
void test_written() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    HeapArray<std::uint32_t> const run = mint_heap_array(alloc, 3, std::uint32_t{40});
    HeapArray<std::uint32_t, 64> const written =
        mint_heap_array_written<std::uint32_t, 64>(alloc, 6, [&](WriteLease<std::uint32_t>& lease) noexcept {
            CRUCIBLE_FATAL_INVARIANT(lease.remaining() == 6);
            lease.put(1);
            lease.put(2);
            lease.put_run(run.view());
            CRUCIBLE_FATAL_INVARIANT(lease.remaining() == 1);
            lease.put(3);
        });
    CRUCIBLE_FATAL_INVARIANT(written.size() == 6);
    std::uint32_t total = 0;
    for (std::uint32_t const element : written) {
        total += element;
    }
    CRUCIBLE_FATAL_INVARIANT(total == 1 + 2 + 40 + 40 + 40 + 3);
    CRUCIBLE_FATAL_INVARIANT(*written.get(5).expect("the position is below the count") == 3);

    // A count of zero gives the writer a lease of no element.
    bool is_called = false;
    HeapArray<std::uint32_t> const empty =
        mint_heap_array_written<std::uint32_t>(alloc, 0, [&](WriteLease<std::uint32_t>& lease) noexcept {
            is_called = true;
            lease.put_run(::foundation::core::View<std::uint32_t const>{});
        });
    CRUCIBLE_FATAL_INVARIANT(is_called && empty.size() == 0);
}

// A writer that leaves an element with no write, and a write past the last
// element, end the process before the array exists.  The mint gives the
// storage back first.  Each refused mint runs in a child through expect_dies
// of the Check family, and the exact writer is the control in the test
// process.
void test_written_aborts(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            HeapArray<std::uint32_t> const partial =
                mint_heap_array_written<std::uint32_t>(::foundation::effects::testing::test().alloc, 4,
                                                       [](WriteLease<std::uint32_t>& lease) noexcept { lease.put(1); });
            static_cast<void>(partial);
        },
        ::foundation::core::TextView{
            "foundation: fatal: the writer of mint_heap_array_written left 3 of 4 elements with no write, "
            "and gave 0 elements past the last one"});

    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            HeapArray<std::uint32_t> const longer = mint_heap_array_written<std::uint32_t>(
                ::foundation::effects::testing::test().alloc, 1, [](WriteLease<std::uint32_t>& lease) noexcept {
                    lease.put(1);
                    lease.put(2);
                    CRUCIBLE_FATAL_INVARIANT(lease.remaining() == 0 && lease.refused() == 1);
                });
            static_cast<void>(longer);
        },
        ::foundation::core::TextView{
            "foundation: fatal: the writer of mint_heap_array_written left 0 of 1 elements with no write, "
            "and gave 1 elements past the last one"});

    HeapArray<std::uint32_t> const run =
        mint_heap_array(::foundation::effects::testing::test().alloc, 3, std::uint32_t{5});
    ::fixy::check::expect_dies(
        test,
        [&run] noexcept {
            HeapArray<std::uint32_t> const shorter = mint_heap_array_written<std::uint32_t>(
                ::foundation::effects::testing::test().alloc, 2, [&run](WriteLease<std::uint32_t>& lease) noexcept {
                    lease.put_run(run.view());
                    CRUCIBLE_FATAL_INVARIANT(lease.remaining() == 2 && lease.refused() == 3);
                });
            static_cast<void>(shorter);
        },
        ::foundation::core::TextView{
            "foundation: fatal: the writer of mint_heap_array_written left 2 of 2 elements with no write, "
            "and gave 3 elements past the last one"});

    HeapArray<std::uint32_t> const exact = mint_heap_array_written<std::uint32_t>(
        ::foundation::effects::testing::test().alloc, 2, [](WriteLease<std::uint32_t>& lease) noexcept {
            lease.put(1);
            lease.put(2);
        });
    CRUCIBLE_FATAL_INVARIANT(exact.size() == 2);
}

// An element that only moves, as the memory of a region of the store of the
// background thread does.  The move leaves the source with label zero.
class MoveOnly {
public:
    explicit MoveOnly(int label) noexcept : label_{label} {}
    MoveOnly(MoveOnly&& other) noexcept : label_{other.label_} { other.label_ = 0; }
    MoveOnly& operator=(MoveOnly&& other) noexcept {
        label_ = other.label_;
        other.label_ = 0;
        return *this;
    }
    MoveOnly(MoveOnly const&) = delete("an element of the test only moves");
    MoveOnly& operator=(MoveOnly const&) = delete("an element of the test only moves");

    [[nodiscard]] int label() const noexcept { return label_; }

private:
    int label_ = 0;
};

// exchange moves a value into the element at its position and gives back the
// element that the position held.  The other elements stay, and the count of
// the array stays.
void test_exchange_moves_in_and_gives_back() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    HeapArray<MoveOnly> array = mint_heap_array_with<MoveOnly>(
        alloc, 3, [](std::size_t position) noexcept { return MoveOnly{static_cast<int>(position) + 1}; });
    MoveOnly const held = array.exchange(1, MoveOnly{7});
    CRUCIBLE_FATAL_INVARIANT(held.label() == 2);
    CRUCIBLE_FATAL_INVARIANT(array.size() == 3);
    CRUCIBLE_FATAL_INVARIANT(array.view().get(0).expect("the array holds three elements")->label() == 1);
    CRUCIBLE_FATAL_INVARIANT(array.view().get(1).expect("the array holds three elements")->label() == 7);
    CRUCIBLE_FATAL_INVARIANT(array.view().get(2).expect("the array holds three elements")->label() == 3);
}

// An exchange at or past the count ends the process.  The exchange runs in a
// child through expect_dies of the Check family.
void test_exchange_past_the_count_aborts(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            HeapArray<int> array = mint_heap_array(::foundation::effects::testing::test().alloc, 2, 0);
            static_cast<void>(array.exchange(2, 5));
        },
        ::foundation::core::TextView{"foundation: fatal: an exchange at the position 2 of a HeapArray of 2 elements"});
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_empty_array();
    test_fill_and_reads();
    test_element_door_writes_in_place();
    test_each_alignment();
    test_sealed_element();
    test_destruction_counts();
    test_equality();
    test_order();
    test_too_long_aborts(test);
    test_agrees_with_plain_model();
    test_with_builds_in_place();
    test_copy_of_view();
    test_zeroed();
    test_written();
    test_written_aborts(test);
    test_exchange_moves_in_and_gives_back();
    test_exchange_past_the_count_aborts(test);
    return 0;
}
