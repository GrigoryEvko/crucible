// Tests of foundation/core/Array.h: MaskedArray and mint_masked_array.

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

// The array that a call gives back by reference.  The rule use-after-move
// does not read through it.
template <class Array>
[[nodiscard]] Array& through(Array& array) noexcept {
    return array;
}

// An element of an empty array ends the process through fatal(), for the
// default array and for a moved-from one.  No read goes through the null
// address, which would end the process with a fault and no abort.  Each
// read runs in a child through expect_dies of the Check family.
void test_element_of_an_empty_array_aborts(::fixy::TestRunnerCtx const& test) {
    MaskedArray<std::uint64_t> empty{};
    MaskedArray<std::uint64_t> const fixed_empty{};
    MaskedArray<std::uint64_t> source =
        mint_masked_array<std::uint64_t>(::foundation::effects::testing::test().alloc, 4);
    MaskedArray<std::uint64_t> const target{std::move(through(source))};
    CRUCIBLE_FATAL_INVARIANT(target.size() == 4);
    ::fixy::check::expect_dies(
        test, [&empty] noexcept { static_cast<void>(empty.at_masked(0)); },
        ::foundation::core::TextView{
            "foundation: fatal: an element of a MaskedArray that holds no storage: the array is default or moved from"});
    ::fixy::check::expect_dies(
        test, [&fixed_empty] noexcept { static_cast<void>(fixed_empty.at_masked(5)); },
        ::foundation::core::TextView{
            "foundation: fatal: an element of a MaskedArray that holds no storage: the array is default or moved from"});
    ::fixy::check::expect_dies(
        test, [&source] noexcept { static_cast<void>(through(source).at_masked(1)); },
        ::foundation::core::TextView{
            "foundation: fatal: an element of a MaskedArray that holds no storage: the array is default or moved from"});
}

// Each count gives that count of zeros, and a key at or past the count reads
// the element of the key modulo the count.
void test_counts_and_wrapping_keys() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
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
            array.with_masked(position, [position](std::uint64_t& element) noexcept { element = position + 1; });
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
        // The element door of a const array reads the element of the masked
        // key, and the door gives the result of the body.
        std::uint64_t const last =
            fixed.with_masked(~std::uint64_t{0}, [](std::uint64_t const& element) noexcept { return element; });
        CRUCIBLE_FATAL_INVARIANT(last == count);
    }
}

// The element door writes one element in place at a masked key, and a key
// with the same low bits names the same element.  The door gives the result of
// the body, and its body holds no reference to the element after the call.
void test_element_door_writes_in_place() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    MaskedArray<std::uint64_t> words = mint_masked_array<std::uint64_t>(alloc, 8);
    int const steps = words.with_masked(2 + 8 * 7, [](std::uint64_t& word) noexcept {
        word |= 16;
        return 1;
    });
    CRUCIBLE_FATAL_INVARIANT(steps == 1);
    CRUCIBLE_FATAL_INVARIANT(words.at_masked(2) == 16 && words.at_masked(10) == 16);
    std::uint64_t total = 0;
    for (std::uint64_t const word : words.view()) {
        total += word;
    }
    CRUCIBLE_FATAL_INVARIANT(total == 16);
}

template <std::size_t Alignment>
void test_alignment() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
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
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    MaskedArray<PinnedSlot, 64> table = mint_masked_array<PinnedSlot, 64>(alloc, 16);
    CRUCIBLE_FATAL_INVARIANT(table.size() == 16);
    for (PinnedSlot const& slot : table.view()) {
        CRUCIBLE_FATAL_INVARIANT(slot.key.load_acquire() == 0);
        CRUCIBLE_FATAL_INVARIANT(slot.value.load_acquire() == 0);
    }
    table.with_masked(5, [](PinnedSlot& slot) noexcept { slot.key.store_release(0xABCD); });
    table.with_masked(5 + 16 * 3, [](PinnedSlot& slot) noexcept { slot.value.store_release(7); });
    CRUCIBLE_FATAL_INVARIANT(table.at_masked(21).key.load_acquire() == 0xABCD);
    CRUCIBLE_FATAL_INVARIANT(table.at_masked(5).value.load_acquire() == 7);
    CRUCIBLE_FATAL_INVARIANT(table.at_masked(6).key.load_acquire() == 0);
}

// Moves the source into the target through two names, as an alias of two
// references does in a program.  The kind self-move of the rule must-consume
// refuses a self-move that one function names.
[[gnu::noinline]] void move_into(MaskedArray<Counted>& target, MaskedArray<Counted>& source) noexcept {
    target = std::move(source);
}

void test_move_and_destruction_counts() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        MaskedArray<Counted> first = mint_masked_array<Counted>(alloc, 8);
        CRUCIBLE_FATAL_INVARIANT(live_elements == 8);
        first.with_masked(3, [](Counted& element) noexcept { element.label = 30; });

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

        move_into(third, third);
        CRUCIBLE_FATAL_INVARIANT(live_elements == 8);
        CRUCIBLE_FATAL_INVARIANT(third.size() == 8);
    }
    CRUCIBLE_FATAL_INVARIANT(live_elements == 0);
}

// A count that is zero, is not a power of two, or is above 2^31 ends the
// process before the allocation.  Each mint runs in a child through
// expect_dies of the Check family.
void test_bad_counts_abort(::fixy::TestRunnerCtx const& test) {
    ::foundation::core::FixedArray<std::size_t, 5> const counts{std::size_t{0}, std::size_t{3}, std::size_t{6},
                                                                std::size_t{1000}, std::size_t{1} << 32};
    for (std::size_t const count : counts) {
        ::fixy::check::expect_dies(
            test,
            [count] noexcept {
                MaskedArray<std::uint64_t> const refused =
                    mint_masked_array<std::uint64_t>(::foundation::effects::testing::test().alloc, count);
                static_cast<void>(refused);
            },
            ::foundation::core::TextView{"foundation: contract violation: detail::is_masked_array_count_(count)"});
    }
}

// The property: for a random power-of-two count and random keys, a write at
// a key reads back at each key with the same low bits.  A plain array that the
// test indexes with key % count is the model.
void test_agrees_with_plain_model() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::crucible::test::PhiloxStream stream{0x5EEDA77A00000002u};
    for (int trial = 0; trial < 128; ++trial) {
        std::size_t const count = std::size_t{1} << stream.below(12);
        MaskedArray<std::uint64_t> array = mint_masked_array<std::uint64_t>(alloc, count);
        ::foundation::core::HeapArray<std::uint64_t> model =
            ::foundation::core::mint_heap_array(alloc, count, std::uint64_t{0});
        model = ::foundation::core::with_mut(std::move(model), [&array, &stream, count](auto cells) noexcept {
            for (int step = 0; step < 64; ++step) {
                std::uint64_t const key = stream.next_wide();
                std::uint64_t const value = stream.next_wide();
                array.with_masked(key, [value](std::uint64_t& element) noexcept { element = value; });
                auto cell = cells.get_mut(key % count).expect("a key modulo the count is below the count");
                *cell = value;
            }
        });
        for (int step = 0; step < 64; ++step) {
            std::uint64_t const key = stream.next_wide();
            CRUCIBLE_FATAL_INVARIANT(array.at_masked(key)
                                     == *model.get(key % count).expect("a key modulo the count is below the count"));
        }
    }
}

// The value doors: set_masked puts a value at the masked key, exchange_masked
// gives the old element back, and update_masked gives the body a copy and puts
// it back.  A key past the count wraps through the mask.
void test_value_doors() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    MaskedArray<std::uint64_t> words = mint_masked_array<std::uint64_t>(alloc, 4);
    std::uint64_t const nine = 9;
    words.set_masked(1, nine);
    words.set_masked(6, std::uint64_t{5});
    CRUCIBLE_FATAL_INVARIANT(words.at_masked(1) == 9);
    CRUCIBLE_FATAL_INVARIANT(words.at_masked(2) == 5);
    std::uint64_t const old = words.exchange_masked(5, 11);
    CRUCIBLE_FATAL_INVARIANT(old == 9);
    CRUCIBLE_FATAL_INVARIANT(words.at_masked(1) == 11);
    std::uint64_t const sum = words.update_masked(2, [](std::uint64_t& word) noexcept {
        word += 3;
        return word;
    });
    CRUCIBLE_FATAL_INVARIANT(sum == 8);
    CRUCIBLE_FATAL_INVARIANT(words.at_masked(2) == 8);
    CRUCIBLE_FATAL_INVARIANT(words.at_masked(0) == 0);
    CRUCIBLE_FATAL_INVARIANT(words.at_masked(3) == 0);

    // An element with an end: set_masked ends the old one, and exchange_masked
    // gives it back whole.
    using Inner = ::foundation::core::HeapArray<int>;
    MaskedArray<Inner> nested = mint_masked_array<Inner>(alloc, 2);
    nested.set_masked(0, ::foundation::core::mint_heap_array(alloc, 3, 1));
    nested.set_masked(0, ::foundation::core::mint_heap_array(alloc, 2, 4));
    Inner const taken = nested.exchange_masked(2, ::foundation::core::mint_heap_array(alloc, 1, 6));
    CRUCIBLE_FATAL_INVARIANT(taken.size() == 2);
    CRUCIBLE_FATAL_INVARIANT(nested.at_masked(0).size() == 1);
}

// The count of the move assignments of AssignCounted.
int assignments = 0;

// An element whose move and end run no code of a caller, and whose move
// assignment is code of its own.  MoveRunsCode reads no assignment.
class AssignCounted {
public:
    AssignCounted() noexcept = default;
    explicit AssignCounted(int label) noexcept : label_{label} {}
    AssignCounted(AssignCounted&&) noexcept = default;
    AssignCounted& operator=(AssignCounted&& other) noexcept {
        ++assignments;
        label_ = other.label_;
        return *this;
    }
    AssignCounted(AssignCounted const&) = delete("an element of the test only moves");
    AssignCounted& operator=(AssignCounted const&) = delete("an element of the test only moves");
    ~AssignCounted() = default;

    [[nodiscard]] int label() const noexcept { return label_; }

private:
    int label_ = 0;
};
static_assert(!::foundation::core::MoveRunsCode<AssignCounted>);

// An element whose move assignment is code.  update_masked puts the copy back
// while the array holds its storage, so it takes only a trivially copyable
// element, and this one has no update_masked.
struct AssignedWithCode {
    int value = 0;
    AssignedWithCode() noexcept = default;
    AssignedWithCode(AssignedWithCode const&) noexcept = default;
    AssignedWithCode(AssignedWithCode&&) noexcept = default;
    AssignedWithCode& operator=(AssignedWithCode const&) noexcept = default;
    AssignedWithCode& operator=(AssignedWithCode&& other) noexcept {
        value = other.value;
        return *this;
    }
    ~AssignedWithCode() = default;
};
static_assert(::foundation::core::UpdatableElement<AssignedWithCode>);
static_assert(!::foundation::core::HeapUpdatableElement<AssignedWithCode>);
template <class Element>
concept HasUpdateMasked = requires(MaskedArray<Element>& array) { array.update_masked(0, [](Element&) noexcept {}); };
static_assert(!HasUpdateMasked<AssignedWithCode> && HasUpdateMasked<std::uint64_t>);

// exchange_masked and set_masked of an element whose move runs no code of a
// caller end the moved-from element in its slot and start the value there.
// They run no assignment of the element, because an assignment can reach the
// array through a second name while the array holds its storage.
void test_exchange_runs_no_assignment() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    MaskedArray<AssignCounted> array = mint_masked_array<AssignCounted>(alloc, 2);
    AssignCounted const held = array.exchange_masked(2, AssignCounted{5});
    array.set_masked(3, AssignCounted{6});
    CRUCIBLE_FATAL_INVARIANT(held.label() == 0 && assignments == 0);
    CRUCIBLE_FATAL_INVARIANT(array.at_masked(0).label() == 5 && array.at_masked(1).label() == 6);
}

// A value door of an empty array ends the process, as at_masked does.
void test_value_door_of_an_empty_array_aborts(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            MaskedArray<int> empty{};
            empty.set_masked(0, 1);
        },
        ::foundation::core::TextView{
            "foundation: fatal: an element of a MaskedArray that holds no storage: the array is default or moved from"});
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            MaskedArray<int> empty{};
            empty.update_masked(0, [](int& element) noexcept { element = 1; });
        },
        ::foundation::core::TextView{
            "foundation: fatal: an element of a MaskedArray that holds no storage: the array is default or moved from"});
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_value_doors();
    test_value_door_of_an_empty_array_aborts(test);
    test_exchange_runs_no_assignment();
    test_empty_array();
    test_element_of_an_empty_array_aborts(test);
    test_counts_and_wrapping_keys();
    test_element_door_writes_in_place();
    test_each_alignment();
    test_pinned_slots();
    test_move_and_destruction_counts();
    test_bad_counts_abort(test);
    test_agrees_with_plain_model();
    return 0;
}
