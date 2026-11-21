// Tests of the SmallList of foundation/core/SmallList.h: the appends in the
// inline slots with no allocation, the spill to the heap and its doublings,
// Full at the maximum, take_last and clear, the frame doors, the moves, the
// lend count, and a property test against a model of a FixedArray and a
// count.
//
// The target links with the linker option --wrap for malloc, aligned_alloc,
// calloc and free (test/foundation/CMakeLists.txt).  Each call of the
// allocation door of foundation/core/Heap.h then goes through a wrapper of
// this file, which counts it.  The shared libraries of the process, such as
// libstdc++, call the allocator with no wrapper, so the counts hold only the
// calls of the code that this link holds.

#include <foundation/core/SmallList.h>

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>
#include <foundation/core/Id.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The calls of the allocator of the C library that this link holds.  The
// test runs on one thread, and a death test counts in its child only.
std::size_t allocator_takes = 0;
std::size_t allocator_gives = 0;

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the linker wrap of the allocator of the C library counts the calls of the door")
extern "C" {
void* __real_malloc(std::size_t bytes);
void* __real_aligned_alloc(std::size_t alignment, std::size_t bytes);
void* __real_calloc(std::size_t count, std::size_t bytes);
void __real_free(void* storage);
void* __wrap_malloc(std::size_t bytes);
void* __wrap_aligned_alloc(std::size_t alignment, std::size_t bytes);
void* __wrap_calloc(std::size_t count, std::size_t bytes);
void __wrap_free(void* storage);

void* __wrap_malloc(std::size_t bytes) {
    ++allocator_takes;
    return __real_malloc(bytes);
}
void* __wrap_aligned_alloc(std::size_t alignment, std::size_t bytes) {
    ++allocator_takes;
    return __real_aligned_alloc(alignment, bytes);
}
void* __wrap_calloc(std::size_t count, std::size_t bytes) {
    ++allocator_takes;
    return __real_calloc(count, bytes);
}
void __wrap_free(void* storage) {
    ++allocator_gives;
    __real_free(storage);
}
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::Index;
using ::foundation::core::mint_small_list;
using ::foundation::core::Quantity;
using ::foundation::core::SmallList;
using ::foundation::core::View;
namespace unit = ::foundation::core::unit;

struct OpTag final {};

using Op = ::foundation::core::Id<OpTag, std::uint32_t>;
using Maximum = Quantity<unit::Element, std::size_t>;

[[nodiscard]] ::foundation::core::Alloc allocation() noexcept { return ::foundation::effects::testing::test().alloc; }

// The sum of the elements of a list, through its frame door.
template <std::size_t Inline>
[[nodiscard]] std::uint64_t sum_of(SmallList<std::uint64_t, Inline> const& list) noexcept {
    return list.with_view([](View<std::uint64_t const> run) noexcept {
        std::uint64_t sum = 0;
        for (std::uint64_t const element : run) {
            sum += element;
        }
        return sum;
    });
}

// The appends up to Inline take no storage.  The append past them takes one
// allocation for the heap, and each growth after it takes one more and gives
// one back.  The end of the list gives the last storage back.
void test_inline_appends_allocate_nothing() {
    std::size_t const takes_before = allocator_takes;
    std::size_t const gives_before = allocator_gives;
    {
        SmallList<std::uint64_t, 4> list = mint_small_list<std::uint64_t, 4>(Maximum{64});
        for (std::uint64_t value = 1; value <= 4; ++value) {
            list.append(allocation(), value).expect("the list is below its maximum");
        }
        CRUCIBLE_FATAL_INVARIANT(allocator_takes == takes_before && allocator_gives == gives_before);
        CRUCIBLE_FATAL_INVARIANT(list.size() == 4 && sum_of(list) == 10);
        list.append(allocation(), 5).expect("the list is below its maximum");
        CRUCIBLE_FATAL_INVARIANT(allocator_takes == takes_before + 1 && allocator_gives == gives_before);
        for (std::uint64_t value = 6; value <= 8; ++value) {
            list.append(allocation(), value).expect("the list is below its maximum");
        }
        CRUCIBLE_FATAL_INVARIANT(allocator_takes == takes_before + 1 && sum_of(list) == 36);
        list.append(allocation(), 9).expect("the list is below its maximum");
        CRUCIBLE_FATAL_INVARIANT(allocator_takes == takes_before + 2 && allocator_gives == gives_before + 1);
        CRUCIBLE_FATAL_INVARIANT(list.size() == 9 && sum_of(list) == 45);
    }
    CRUCIBLE_FATAL_INVARIANT(allocator_takes == takes_before + 2 && allocator_gives == gives_before + 2);
}

// An element with no default constructor fills the inline slots, and a move
// of an inline list moves its elements with no allocation.
void test_inline_moves() {
    std::size_t const takes_before = allocator_takes;
    SmallList<Op, 3> list = mint_small_list<Op, 3>(Maximum{3});
    list.append(allocation(), Op{7}).expect("the list is below its maximum");
    list.append(allocation(), Op{8}).expect("the list is below its maximum");
    SmallList<Op, 3> moved{std::move(list)};
    SmallList<Op, 3> target = mint_small_list<Op, 3>(Maximum{3});
    target = std::move(moved);
    CRUCIBLE_FATAL_INVARIANT(target.size() == 2 && allocator_takes == takes_before);
    std::uint32_t const total = target.with_view([](View<Op const> run) noexcept {
        std::uint32_t sum = 0;
        for (Op const op : run) {
            sum += op.raw();
        }
        return sum;
    });
    CRUCIBLE_FATAL_INVARIANT(total == 15);
    target.append(allocation(), Op{9}).expect("the list is below its maximum");
    CRUCIBLE_FATAL_INVARIANT(target.append(allocation(), Op{10}).is_err() && target.size() == 3);
}

// The list that a call gives back by reference.  The rule use-after-move does
// not read through it, so a test reaches a moved-from list.
template <std::size_t Inline>
[[nodiscard]] SmallList<std::uint64_t, Inline>& through(SmallList<std::uint64_t, Inline>& list) noexcept {
    return list;
}

// A list on the heap moves its storage, and the moved-from list is empty with
// its maximum.
void test_heap_moves() {
    SmallList<std::uint64_t, 2> list = mint_small_list<std::uint64_t, 2>(Maximum{16});
    for (std::uint64_t value = 1; value <= 5; ++value) {
        list.append(allocation(), value).expect("the list is below its maximum");
    }
    std::size_t const takes_before = allocator_takes;
    SmallList<std::uint64_t, 2> moved{std::move(through(list))};
    CRUCIBLE_FATAL_INVARIANT(moved.size() == 5 && sum_of(moved) == 15 && allocator_takes == takes_before);
    CRUCIBLE_FATAL_INVARIANT(through(list).is_empty() && sum_of(through(list)) == 0);
    through(list).append(allocation(), 100).expect("a moved-from list keeps its maximum");
    CRUCIBLE_FATAL_INVARIANT(through(list).size() == 1 && sum_of(through(list)) == 100);
}

void test_take_last_and_clear() {
    SmallList<std::uint64_t, 2> list = mint_small_list<std::uint64_t, 2>(Maximum{8});
    CRUCIBLE_FATAL_INVARIANT(list.take_last().is_none());
    for (std::uint64_t value = 1; value <= 3; ++value) {
        list.append(allocation(), value).expect("the list is below its maximum");
    }
    CRUCIBLE_FATAL_INVARIANT(list.take_last().expect("the list holds three elements") == 3);
    CRUCIBLE_FATAL_INVARIANT(list.size() == 2 && sum_of(list) == 3);
    std::size_t const takes_before = allocator_takes;
    list.clear();
    CRUCIBLE_FATAL_INVARIANT(list.is_empty() && list.take_last().is_none());
    for (std::uint64_t value = 1; value <= 4; ++value) {
        list.append(allocation(), value).expect("a cleared list keeps its storage");
    }
    CRUCIBLE_FATAL_INVARIANT(allocator_takes == takes_before && sum_of(list) == 10);
}

// The View of the frame door gives get and the iteration verbs, and with_mut
// writes each element and gives the list back.
void test_frame_doors() {
    SmallList<std::uint64_t, 4> list = mint_small_list<std::uint64_t, 4>(Maximum{8});
    for (std::uint64_t value = 1; value <= 6; ++value) {
        list.append(allocation(), value).expect("the list is below its maximum");
    }
    std::uint64_t const read = list.with_view([](View<std::uint64_t> run) noexcept {
        std::uint64_t const third = *run.get(2).expect("2 is below the count");
        std::uint64_t backward = 0;
        for (std::uint64_t const element : run.reverse()) {
            backward = backward * 10 + element;
        }
        std::size_t windows = 0;
        for (View<std::uint64_t, 2> const pair : run.windows<2>()) {
            windows +=
                pair.at(::foundation::core::index<1>()) == pair.at(::foundation::core::index<0>()) + 1 ? 1uz : 0uz;
        }
        return third * 1000000 + backward + (run.get(6).is_none() ? 0 : 1) + windows * 10000000;
    });
    CRUCIBLE_FATAL_INVARIANT(read == 3000000 + 654321 + 50000000);
    SmallList<std::uint64_t, 4> doubled = ::foundation::core::with_mut(std::move(list), [](auto run) noexcept {
        for (std::uint64_t& element : run) {
            element *= 2;
        }
    });
    CRUCIBLE_FATAL_INVARIANT(sum_of(doubled) == 42 && doubled.size() == 6);
}

void test_full_at_the_maximum() {
    SmallList<std::uint64_t, 4> list = mint_small_list<std::uint64_t, 4>(Maximum{5});
    for (std::uint64_t value = 1; value <= 5; ++value) {
        list.append(allocation(), value).expect("the list is below its maximum");
    }
    CRUCIBLE_FATAL_INVARIANT(list.append(allocation(), 6).is_err() && list.size() == 5 && sum_of(list) == 15);
    SmallList<std::uint64_t, 4> none = mint_small_list<std::uint64_t, 4>(Maximum{0});
    CRUCIBLE_FATAL_INVARIANT(none.append(allocation(), 1).is_err() && none.is_empty());
}

// The appends of a list whose maximum is below its inline slots.
[[nodiscard]] bool takes_four_of_eight(SmallList<std::uint64_t, 8>& list) noexcept {
    for (std::uint64_t value = 1; value <= 4; ++value) {
        if (list.append(allocation(), value).is_err()) {
            return false;
        }
    }
    return list.append(allocation(), 5).is_err() && list.size() == 4 && sum_of(list) == 10;
}

// A maximum below the inline slots holds: the fifth append of a list of 8
// inline slots and a maximum of 4 gives Full, also after a clear, and also on
// the list that a move leaves.
void test_full_below_the_inline_slots() {
    std::size_t const takes_before = allocator_takes;
    SmallList<std::uint64_t, 8> list = mint_small_list<std::uint64_t, 8>(Maximum{4});
    CRUCIBLE_FATAL_INVARIANT(takes_four_of_eight(list));
    list.clear();
    CRUCIBLE_FATAL_INVARIANT(takes_four_of_eight(list));
    SmallList<std::uint64_t, 8> moved{std::move(through(list))};
    CRUCIBLE_FATAL_INVARIANT(moved.size() == 4 && sum_of(moved) == 10);
    SmallList<std::uint64_t, 8>& left = through(list);
    CRUCIBLE_FATAL_INVARIANT(left.is_empty() && takes_four_of_eight(left));
    CRUCIBLE_FATAL_INVARIANT(allocator_takes == takes_before);
}

// ── The lend count ─────────────────────────────────────────────────────────

constexpr ::foundation::core::TextView kLentChange{
    "foundation: fatal: an append, a clear, a move, a freeze or the end of a growable owner while a frame door lends "
    "its storage"};

// The calls from a function that the body of a frame calls.  The rule
// borrow-match refuses each such call in the body itself.
[[gnu::noipa]] void append_to(SmallList<std::uint64_t, 4>& list) noexcept {
    list.append(allocation(), 9).expect("the list is below its maximum");
}

[[gnu::noipa]] void take_from(SmallList<std::uint64_t, 4>& list) noexcept { static_cast<void>(list.take_last()); }

[[gnu::noipa]] void move_from(SmallList<std::uint64_t, 4>& list) noexcept {
    SmallList<std::uint64_t, 4> const taken{std::move(list)};
    static_cast<void>(taken.size());
}

[[nodiscard]] SmallList<std::uint64_t, 4> list_of_two() noexcept {
    SmallList<std::uint64_t, 4> list = mint_small_list<std::uint64_t, 4>(Maximum{8});
    list.append(allocation(), 1).expect("the list is below its maximum");
    list.append(allocation(), 2).expect("the list is below its maximum");
    return list;
}

// An append, a take and a move while a frame door lends the elements end the
// process, also when the inline slots have room.
void test_lent_list_refuses_changes(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            SmallList<std::uint64_t, 4> list = list_of_two();
            static_cast<void>(list.with_view([&list](View<std::uint64_t> run) noexcept {
                append_to(list);
                return run.size();
            }));
        },
        kLentChange);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            SmallList<std::uint64_t, 4> list = list_of_two();
            static_cast<void>(list.with_view([&list](View<std::uint64_t> run) noexcept {
                take_from(list);
                return run.size();
            }));
        },
        kLentChange);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            SmallList<std::uint64_t, 4> list = list_of_two();
            static_cast<void>(list.with_view([&list](View<std::uint64_t> run) noexcept {
                move_from(list);
                return run.size();
            }));
        },
        kLentChange);
    SmallList<std::uint64_t, 4> control = list_of_two();
    append_to(control);
    CRUCIBLE_FATAL_INVARIANT(sum_of(control) == 12);
}

// ── The property test ──────────────────────────────────────────────────────

// The property: random appends, takes and clears keep the list equal to a
// model of a FixedArray and a count, across the inline slots and the heap, an
// append gives Full exactly at the maximum, and the allocations agree with
// the growths of the model: one at the spill, and one at each doubling after
// it.  A clear keeps the storage, so the capacity of the model only grows.
void test_list_agrees_with_the_model() {
    constexpr std::size_t maximum = 40;
    constexpr std::size_t inline_count = 3;
    ::crucible::test::PhiloxStream stream{0x5A110003u};
    std::size_t const takes_before = allocator_takes;
    std::size_t expected_takes = 0;
    {
        SmallList<std::uint64_t, inline_count> list = mint_small_list<std::uint64_t, inline_count>(Maximum{maximum});
        FixedArray<std::uint64_t, maximum> model{};
        std::size_t count = 0;
        std::size_t capacity = inline_count;
        for (int step = 0; step < 8192; ++step) {
            std::uint64_t const choice = stream.below(16);
            if (choice < 10) {
                std::uint64_t const value = stream.next_wide();
                bool const is_appended = list.append(allocation(), value).is_ok();
                CRUCIBLE_FATAL_INVARIANT(is_appended == (count < maximum));
                if (is_appended) {
                    if (count == capacity) {
                        capacity = capacity * 2 < maximum ? capacity * 2 : maximum;
                        ++expected_takes;
                    }
                    model.with_element(Index<maximum>::check(count).expect("the count is below the maximum"),
                                       [value](std::uint64_t& held) noexcept { held = value; });
                    ++count;
                }
            } else if (choice < 15) {
                auto taken = list.take_last();
                CRUCIBLE_FATAL_INVARIANT(taken.is_some() == (count != 0));
                if (count != 0) {
                    --count;
                    std::uint64_t const expected = model.at(Index<maximum>::check(count).expect("below"));
                    CRUCIBLE_FATAL_INVARIANT(std::move(taken).expect("the list holds an element") == expected);
                }
            } else if (stream.below(4) == 0) {
                list.clear();
                count = 0;
            }
            View<std::uint64_t const> const expected = model.view().window(0, count).expect("the count fits");
            CRUCIBLE_FATAL_INVARIANT(list.size() == count);
            CRUCIBLE_FATAL_INVARIANT(list.with_view([&expected](View<std::uint64_t const> run) noexcept {
                return ::foundation::core::equal(run, expected);
            }));
            CRUCIBLE_FATAL_INVARIANT(allocator_takes - takes_before == expected_takes);
        }
    }
    CRUCIBLE_FATAL_INVARIANT(expected_takes != 0);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_inline_appends_allocate_nothing();
    test_inline_moves();
    test_heap_moves();
    test_take_last_and_clear();
    test_frame_doors();
    test_full_at_the_maximum();
    test_full_below_the_inline_slots();
    test_lent_list_refuses_changes(test);
    test_list_agrees_with_the_model();
    return 0;
}
