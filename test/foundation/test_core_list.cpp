// Tests of the FixedList of the Region family: append, take_last, clear, get,
// the value doors insert, set, exchange, update, remove and drain, the copy and
// the move, the end of each element, and the Full of a linear element.  Each
// property compares a list with a model: a FixedArray and a count.

#include <foundation/core/List.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/ByteSeal.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::FixedList;
using ::foundation::core::Index;
using ::foundation::core::View;

// An element that counts its ends through a Ref to a counter.  The brand of
// the Ref is a template argument, so no fresh brand leaves its scope.  A
// moved-from element is not live, so only a live element counts.
template <class CounterRef>
struct Counted final {
    CounterRef ends;
    bool is_live = true;
    explicit Counted(CounterRef counter) noexcept : ends{counter} {}
    Counted(Counted&& other) noexcept : ends{other.ends} { other.is_live = false; }
    Counted(Counted const& other) noexcept : ends{other.ends}, is_live{other.is_live} {}
    Counted& operator=(Counted&&) = delete;
    Counted& operator=(Counted const&) = delete;
    ~Counted() {
        if (is_live) {
            ++*ends;
        }
    }
};

// A linear element.  The seal makes its assignment user-provided, so it is
// not trivially copyable, and it refuses a lifetime start over bytes.
struct[[= ::foundation::linear::linear{}]] Token final {
    int id = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    explicit Token(int from) noexcept : id{from} {}
    Token(Token&& other) noexcept : id{other.id} {}
    Token& operator=(Token&& other) noexcept {
        id = other.id;
        return *this;
    }
    Token(Token const&) = delete;
};

void test_append_until_full() {
    FixedList<int, 4> list;
    for (int value = 1; value <= 4; ++value) {
        Index<4> const index = list.append(value).expect("the list has room");
        CRUCIBLE_TEST_ASSERT(kTest, *list.get(index.raw()).expect("the list holds the element") == value);
        list.update(index, [](int& element) noexcept { element *= 10; });
    }
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 4);
    CRUCIBLE_FATAL_INVARIANT(list.append(5).is_err());
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 4);
    int expected = 10;
    for (int const element : list) {
        CRUCIBLE_TEST_ASSERT(kTest, element == expected);
        expected += 10;
    }
    CRUCIBLE_TEST_ASSERT(kTest, list.get(3).is_some() && list.get(4).is_none());
}

void test_take_last_and_clear() {
    FixedList<int, 3> list;
    CRUCIBLE_TEST_ASSERT(kTest, list.take_last().is_none());
    static_cast<void>(list.append(1).expect("the list has room"));
    static_cast<void>(list.append(2).expect("the list has room"));
    static_cast<void>(list.append(3).expect("the list has room"));
    CRUCIBLE_TEST_ASSERT(kTest, list.take_last().expect("the list holds three elements") == 3);
    CRUCIBLE_TEST_ASSERT(kTest, list.take_last().expect("the list holds two elements") == 2);
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 1);
    list.clear();
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 0 && list.view().size() == 0 && list.take_last().is_none());
    static_cast<void>(list.append(7).expect("a cleared list has room"));
    CRUCIBLE_TEST_ASSERT(kTest, *list.get(0).expect("0 is below the count") == 7);
}

// expect_empty ends an empty list, and a list that still holds an element
// ends the process with the reason.
void test_expect_empty() {
    FixedList<int, 2> empty;
    std::move(empty).expect_empty("the list holds no element");
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            FixedList<int, 2> held;
            static_cast<void>(held.append(5).expect("the list has room"));
            std::move(held).expect_empty("the list still holds one element");
        },
        ::foundation::core::TextView{"foundation: fatal: the list still holds one element"});
}

// Each element ends exactly one time: at a take, at a clear, or at the end of
// the list.  A copy of the list copies each element.
void test_each_element_ends_once() {
    int ends = 0;
    auto const counter = ::foundation::core::to(ends);
    using CounterRef = std::remove_const_t<decltype(counter)>;
    using Element = Counted<CounterRef>;
    {
        FixedList<Element, 4> list;
        for (int turn = 0; turn < 3; ++turn) {
            static_cast<void>(list.append(Element{counter}).expect("the list has room"));
        }
        CRUCIBLE_TEST_ASSERT(kTest, ends == 0);
        {
            Element const last = list.take_last().expect("the list holds three elements");
            CRUCIBLE_TEST_ASSERT(kTest, last.is_live && ::foundation::core::same_object(last.ends, counter));
        }
        CRUCIBLE_TEST_ASSERT(kTest, ends == 1);
        FixedList<Element, 4> const copied{list};
        CRUCIBLE_TEST_ASSERT(kTest, copied.size() == 2 && ends == 1);
        FixedList<Element, 4> moved{std::move(list)};
        CRUCIBLE_TEST_ASSERT(kTest, moved.size() == 2 && ends == 1);
        moved.clear();
        CRUCIBLE_TEST_ASSERT(kTest, ends == 3);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ends == 5);
}

// An append into a full list of a linear element gives the value back in its
// Full, and the caller consumes each value.
void test_full_gives_a_linear_value_back() {
    FixedList<Token, 1> list;
    static_cast<void>(list.append(Token{1}).expect("the list has room"));
    auto refused = list.append(Token{2});
    CRUCIBLE_TEST_ASSERT(kTest, refused.is_err());
    Token back = std::move(refused).err().expect("the list is full").take();
    CRUCIBLE_TEST_ASSERT(kTest, back.id == 2);
    ::foundation::linear::drop(std::move(back));
    Token first = list.take_last().expect("the list holds the first token");
    CRUCIBLE_TEST_ASSERT(kTest, first.id == 1);
    ::foundation::linear::drop(std::move(first));
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 0);
    std::move(list).expect_empty("the test took each token out of the list");
}

// The property: for random appends and takes, the list holds the elements of
// the model, in order, and refuses an append exactly when the model is full.
void test_list_agrees_with_the_model() {
    constexpr std::size_t capacity = 16;
    ::crucible::test::PhiloxStream stream{0x0F1E2D3C4B5A6978u};
    FixedList<std::uint64_t, capacity> list;
    FixedArray<std::uint64_t, capacity> model{};
    std::size_t count = 0;
    for (int step = 0; step < 4096; ++step) {
        if (stream.below(3) != 0) {
            std::uint64_t const value = stream.next_wide();
            bool const is_appended = list.append(value).is_ok();
            CRUCIBLE_FATAL_INVARIANT(is_appended == (count < capacity));
            if (is_appended) {
                model.set(Index<capacity>::check(count).expect("the count is below the capacity"), value);
                ++count;
            }
        } else {
            bool const is_taken = list.take_last().is_some();
            CRUCIBLE_TEST_ASSERT(kTest, is_taken == (count != 0));
            if (is_taken) {
                --count;
            }
        }
        View<std::uint64_t const> const expected = model.view().window(0, count).expect("the count fits");
        CRUCIBLE_TEST_ASSERT(kTest, list.size() == count);
        CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::equal(list.view(), expected));
    }
}

// The property of the value doors: for random inserts, removes, sets,
// exchanges and updates, the list holds the elements of the model, in order,
// and a full list refuses an insert.
void test_value_doors_agree_with_the_model() {
    constexpr std::size_t capacity = 8;
    using Slot = Index<capacity>;
    ::crucible::test::PhiloxStream stream{0x1F2E3D4C5B6A7988u};
    FixedList<std::uint64_t, capacity> list;
    FixedArray<std::uint64_t, capacity> model{};
    std::size_t count = 0;
    auto const slot_of = [](std::size_t position) noexcept {
        return Slot::check(position).expect("the position is below the capacity");
    };
    for (int step = 0; step < 8192; ++step) {
        std::uint64_t const value = stream.next_wide();
        if (stream.below(2) == 0) {
            if (count == capacity) {
                CRUCIBLE_FATAL_INVARIANT(list.insert(slot_of(0), value).is_err());
                continue;
            }
            std::size_t const position = stream.below(static_cast<std::uint32_t>(count + 1));
            list.insert(slot_of(position), value).expect("the list has room");
            for (std::size_t later = count; later != position; --later)
                model.set(slot_of(later), model.at(slot_of(later - 1)));
            model.set(slot_of(position), value);
            ++count;
        } else if (count != 0) {
            Slot const slot = slot_of(stream.below(static_cast<std::uint32_t>(count)));
            switch (stream.below(4)) {
                case 0:
                    list.set(slot, value);
                    model.set(slot, value);
                    break;
                case 1:
                    CRUCIBLE_FATAL_INVARIANT(list.exchange(slot, value) == model.exchange(slot, value));
                    break;
                case 2: {
                    auto const add = [value](std::uint64_t& element) noexcept {
                        element += value;
                        return element;
                    };
                    CRUCIBLE_FATAL_INVARIANT(list.update(slot, add) == model.update(slot, add));
                    break;
                }
                default:
                    CRUCIBLE_FATAL_INVARIANT(list.remove(slot).expect("the list holds the element") == model.at(slot));
                    --count;
                    for (std::size_t later = slot.raw(); later != count; ++later)
                        model.set(slot_of(later), model.at(slot_of(later + 1)));
                    break;
            }
        }
        View<std::uint64_t const> const expected = model.view().window(0, count).expect("the count fits");
        CRUCIBLE_FATAL_INVARIANT(list.size() == count && ::foundation::core::equal(list.view(), expected));
    }
}

// The value doors end each element one time, also an element with no
// assignment, and drain gives each element in order and ends the list.
void test_value_doors_end_each_element_once() {
    int ends = 0;
    auto const counter = ::foundation::core::to(ends);
    using Element = Counted<std::remove_const_t<decltype(counter)>>;
    FixedList<Element, 4> list;
    for (int turn = 0; turn < 3; ++turn) {
        list.insert(::foundation::core::index<0>(), Element{counter}).expect("the list has room");
    }
    CRUCIBLE_TEST_ASSERT(kTest, ends == 0);
    list.set(::foundation::core::index<1>(), Element{counter});
    CRUCIBLE_TEST_ASSERT(kTest, ends == 1);
    {
        Element const old = list.exchange(::foundation::core::index<2>(), Element{counter});
        CRUCIBLE_TEST_ASSERT(kTest, old.is_live && ends == 1);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ends == 2);
    static_cast<void>(list.remove(::foundation::core::index<0>()).expect("the list holds three elements"));
    CRUCIBLE_TEST_ASSERT(kTest, ends == 3 && list.size() == 2);
    int given = 0;
    std::move(list).drain([&given](Element element) noexcept { given += element.is_live ? 1 : 0; });
    CRUCIBLE_TEST_ASSERT(kTest, given == 2 && ends == 5);
}

// A linear element goes through exchange, remove and insert, and a full list
// gives the value of an insert back.
void test_value_doors_of_a_linear_list() {
    FixedList<Token, 2> list;
    list.insert(::foundation::core::index<0>(), Token{2}).expect("the list has room");
    list.insert(::foundation::core::index<0>(), Token{1}).expect("the list has room");
    Token refused = list.insert(::foundation::core::index<0>(), Token{3}).err().expect("the list is full").take();
    CRUCIBLE_TEST_ASSERT(kTest, refused.id == 3);
    Token second = list.exchange(::foundation::core::index<1>(), std::move(refused));
    CRUCIBLE_TEST_ASSERT(kTest, second.id == 2);
    ::foundation::linear::drop(std::move(second));
    Token first = list.remove(::foundation::core::index<0>()).expect("the list holds two tokens");
    Token last = list.remove(::foundation::core::index<0>()).expect("the list holds one token");
    CRUCIBLE_TEST_ASSERT(kTest, first.id == 1 && last.id == 3);
    list.remove(::foundation::core::index<0>()).expect_none("an empty list has no element to remove");
    ::foundation::linear::drop(std::move(first));
    ::foundation::linear::drop(std::move(last));
    std::move(list).expect_empty("the test took each token out of the list");
}

// The identities that ended, and what a destructor found in the list.
constexpr std::size_t kIdentities = 64;
constinit FixedArray<bool, kIdentities> ended_identities{};
constinit std::size_t next_identity = 0;
constinit bool is_ended_element_seen = false;
constinit std::size_t reads_of_the_list = 0;

// The identity of an element.  Its end marks the identity as ended.  A moved
// identity has none.
class Identity final {
public:
    Identity() noexcept : place_{next_identity} { ++next_identity; }
    Identity(Identity&& other) noexcept : place_{other.place_} { other.place_ = kIdentities; }
    Identity& operator=(Identity&& other) noexcept {
        end_();
        place_ = other.place_;
        other.place_ = kIdentities;
        return *this;
    }
    Identity(Identity const&) = delete("an identity ends one time");
    Identity& operator=(Identity const&) = delete("an identity ends one time");
    ~Identity() { end_(); }

    [[nodiscard]] bool is_ended() const noexcept {
        return Index<kIdentities>::check(place_).is_some_and(
            [](Index<kIdentities> const& place) noexcept { return ended_identities.at(place); });
    }

private:
    void end_() noexcept {
        Index<kIdentities>::check(place_).match(
            [](Index<kIdentities> place) noexcept { ended_identities.set(place, true); }, []() noexcept {});
    }

    std::size_t place_ = kIdentities;
};

template <bool IsAssignable>
struct Watched;

template <bool IsAssignable>
using WatchedList = FixedList<Watched<IsAssignable>, 4>;

// The list that the destructor of an element reads, and whether it reads it.
template <bool IsAssignable>
constinit WatchedList<IsAssignable> watched_list{};
constinit bool is_watching = false;

// Reads each element of the watched list.
template <bool IsAssignable>
void read_watched_list() noexcept {
    if (!is_watching) return;
    ++reads_of_the_list;
    for (Watched<IsAssignable> const& element : watched_list<IsAssignable>) {
        is_ended_element_seen = is_ended_element_seen || element.identity.is_ended();
    }
}

// Reads the list at its end.
template <bool IsAssignable>
struct ReaderAtEnd final {
    ReaderAtEnd() noexcept = default;
    ReaderAtEnd(ReaderAtEnd&&) noexcept = default;
    ReaderAtEnd& operator=(ReaderAtEnd&&) noexcept = default;
    ReaderAtEnd(ReaderAtEnd const&) = delete("a reader ends one time");
    ReaderAtEnd& operator=(ReaderAtEnd const&) = delete("a reader ends one time");
    ~ReaderAtEnd() { read_watched_list<IsAssignable>(); }
};

// An element whose identity ends before the reader that looks at the list.
template <bool IsAssignable>
struct Watched final {
    ReaderAtEnd<IsAssignable> reader{};
    Identity identity{};

    Watched() noexcept = default;
    Watched(Watched&&) noexcept = default;
    Watched& operator=(Watched&&) noexcept
        requires IsAssignable
    = default;
    Watched(Watched const&) = delete("a watched element does not copy");
};

// Runs door on the watched list of three elements, and tests that no
// destructor found an element that ended while the list counted it.
template <bool IsAssignable, class Door>
void check_list_is_whole(Door const& door) {
    WatchedList<IsAssignable>& list = watched_list<IsAssignable>;
    list.clear();
    ended_identities = {};
    next_identity = 0;
    is_ended_element_seen = false;
    reads_of_the_list = 0;
    for (int turn = 0; turn < 3; ++turn) {
        static_cast<void>(list.append(Watched<IsAssignable>{}).expect("the list has room"));
    }
    is_watching = true;
    door(list);
    is_watching = false;
    list.clear();
    CRUCIBLE_TEST_ASSERT(kTest, reads_of_the_list != 0 && !is_ended_element_seen);
}

// A destructor of an element that reaches the list finds an element in each
// slot that the list counts, in each door that ends an element.
template <bool IsAssignable>
void test_a_destructor_finds_a_whole_list() {
    using Element = Watched<IsAssignable>;
    using List = WatchedList<IsAssignable>;
    constexpr Index<4> first = ::foundation::core::index<0>();
    constexpr Index<4> middle = ::foundation::core::index<1>();
    check_list_is_whole<IsAssignable>([](List& list) noexcept { list.set(middle, Element{}); });
    check_list_is_whole<IsAssignable>([](List& list) noexcept { static_cast<void>(list.exchange(middle, Element{})); });
    check_list_is_whole<IsAssignable>([](List& list) noexcept {
        Element const kept = list.exchange(middle, Element{});
        static_cast<void>(kept);
    });
    check_list_is_whole<IsAssignable>(
        [](List& list) noexcept { list.insert(first, Element{}).expect("the list has room"); });
    check_list_is_whole<IsAssignable>([](List& list) noexcept { static_cast<void>(list.remove(first).is_some()); });
    check_list_is_whole<IsAssignable>([](List& list) noexcept { static_cast<void>(list.take_last().is_some()); });
    check_list_is_whole<IsAssignable>([](List& list) noexcept { list.clear(); });
    check_list_is_whole<IsAssignable>([](List& list) noexcept { list = List{}; });
}

// An element with no assignment.  Its destructor clears the list that the
// door writes.
struct Clearing final {
    Clearing() noexcept = default;
    Clearing(Clearing&&) noexcept {}
    Clearing& operator=(Clearing&&) = delete("the test needs an element with no assignment");
    Clearing(Clearing const&) = delete("the test needs an element that moves only");
    ~Clearing();
};

constinit FixedList<Clearing, 4> cleared_list{};
constinit bool is_clear_at_end = false;

Clearing::~Clearing() {
    if (!is_clear_at_end) return;
    is_clear_at_end = false;
    cleared_list.clear();
}

constexpr ::foundation::core::TextView list_changed_report{
    "foundation: fatal: the code of an element changed a FixedList while a door of the list moved or ended its "
    "elements"};

// A list whose element has no assignment ends the process when the code of an
// element changes it in a door that frees a slot.
void test_a_destructor_that_changes_the_list_ends_the_process() {
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            static_cast<void>(cleared_list.append(Clearing{}).expect("the list has room"));
            static_cast<void>(cleared_list.append(Clearing{}).expect("the list has room"));
            is_clear_at_end = true;
            cleared_list.set(::foundation::core::index<1>(), Clearing{});
        },
        list_changed_report);
}

// An element whose destructor appends to the list that ends it.  The slot that
// it would take is the slot whose destructor still runs.
struct Growing final {
    Growing() noexcept = default;
    Growing(Growing&&) noexcept {}
    Growing& operator=(Growing&&) noexcept { return *this; }
    Growing(Growing const&) = delete("the test needs an element that moves only");
    Growing& operator=(Growing const&) = delete("the test needs an element that moves only");
    ~Growing();
};

constinit FixedList<Growing, 4> growing_list{};
constinit bool is_append_at_end = false;

Growing::~Growing() {
    if (!is_append_at_end) return;
    is_append_at_end = false;
    static_cast<void>(growing_list.append(Growing{}).is_ok());
}

// Puts two elements into the growing list, and lets the next destructor append.
void arm_growing_list() noexcept {
    static_cast<void>(growing_list.append(Growing{}).expect("the list has room"));
    static_cast<void>(growing_list.append(Growing{}).expect("the list has room"));
    is_append_at_end = true;
}

// A destructor that appends to the list while a door of the list ends the
// element ends the process.  The doors that end an element are clear,
// take_last, remove and the assignment.
void test_an_append_from_a_destructor_ends_the_process() {
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            arm_growing_list();
            growing_list.clear();
        },
        list_changed_report);
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            arm_growing_list();
            static_cast<void>(growing_list.take_last().is_some());
        },
        list_changed_report);
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            arm_growing_list();
            static_cast<void>(growing_list.remove(::foundation::core::index<0>()).is_some());
        },
        list_changed_report);
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            arm_growing_list();
            growing_list = FixedList<Growing, 4>{};
        },
        list_changed_report);
}

constinit FixedList<int, 4> updated_list{};

// Clears the updated list.  It is a second name of the list for the body.
void clear_updated_list() noexcept { updated_list.clear(); }

// update names the list and no slot, so a body that ends the element through a
// second name ends the process at the index.
void test_a_body_of_update_that_ends_the_element_ends_the_process() {
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            static_cast<void>(updated_list.append(1).expect("the list has room"));
            updated_list.update(::foundation::core::index<0>(), [](int&) noexcept { clear_updated_list(); });
        },
        ::foundation::core::TextView{"foundation: fatal: a write at the index 0 of a FixedList of 0 elements"});
}

// An index with no element ends the process at each door that writes it.
void test_an_index_past_the_count_ends_the_process() {
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            FixedList<int, 4> list;
            static_cast<void>(list.append(1).expect("the list has room"));
            list.set(::foundation::core::index<1>(), 2);
        },
        ::foundation::core::TextView{"foundation: fatal: a write at the index 1 of a FixedList of 1 elements"});
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            FixedList<int, 4> list;
            static_cast<void>(list.exchange(::foundation::core::index<0>(), 2));
        },
        ::foundation::core::TextView{"foundation: fatal: a write at the index 0 of a FixedList of 0 elements"});
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            FixedList<int, 4> list;
            list.update(::foundation::core::index<3>(), [](int& element) noexcept { element = 1; });
        },
        ::foundation::core::TextView{"foundation: fatal: a write at the index 3 of a FixedList of 0 elements"});
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            FixedList<int, 4> list;
            list.insert(::foundation::core::index<1>(), 2).expect("the list has room");
        },
        ::foundation::core::TextView{"foundation: fatal: a write at the index 1 of a FixedList of 0 elements"});
}

void test_iteration_verbs_of_a_list() {
    FixedList<int, 5> list;
    for (int value = 1; value <= 5; ++value) {
        static_cast<void>(list.append(value).expect("the list has room"));
    }
    int expected = 5;
    for (int const element : list.reverse()) {
        CRUCIBLE_TEST_ASSERT(kTest, element == expected);
        --expected;
    }
    int windows = 0;
    for (View<int const, 2> const run : list.windows<2>()) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             run.at(::foundation::core::index<1>()) == run.at(::foundation::core::index<0>()) + 1);
        ++windows;
    }
    CRUCIBLE_TEST_ASSERT(kTest, windows == 4);
    CRUCIBLE_TEST_ASSERT(kTest, list.chunks<2>().rest().size() == 1);
}

// A list of the first count numbers from 1, for a constant evaluation.
constexpr FixedList<int, 4> list_of(int count) noexcept {
    FixedList<int, 4> list;
    for (int value = 1; value <= count; ++value) {
        static_cast<void>(list.append(value).expect("the list has room"));
    }
    return list;
}

constexpr int sum_of(FixedList<int, 4> const& list) noexcept {
    int sum = 0;
    for (int const element : list) {
        sum += element;
    }
    return sum;
}

// The View of a FixedList comes from a reference to the array of its slots,
// with no compare with a null pointer.  The build sets
// -fno-delete-null-pointer-checks, and with it GCC 16 does not fold the
// compare of the address of a block-scope static object with a null pointer
// in a constant evaluation.  A loop over a static FixedList is then a
// constant expression only when the View has no such compare.
void test_view_of_a_static_list_is_constant() {
    static constexpr FixedList<int, 4> three = list_of(3);
    static constexpr FixedList<int, 4> none = list_of(0);
    static constexpr int sum = sum_of(three);
    static constexpr int empty_sum = sum_of(none);
    static_assert(sum == 6 && empty_sum == 0);
    CRUCIBLE_TEST_ASSERT(kTest, sum_of(three) == 6 && sum_of(none) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, none.view().size() == 0);
}

}  // namespace

int main() {
    test_append_until_full();
    test_take_last_and_clear();
    test_expect_empty();
    test_each_element_ends_once();
    test_full_gives_a_linear_value_back();
    test_list_agrees_with_the_model();
    test_value_doors_agree_with_the_model();
    test_value_doors_end_each_element_once();
    test_value_doors_of_a_linear_list();
    test_a_destructor_finds_a_whole_list<true>();
    test_a_destructor_finds_a_whole_list<false>();
    test_a_destructor_that_changes_the_list_ends_the_process();
    test_an_append_from_a_destructor_ends_the_process();
    test_a_body_of_update_that_ends_the_element_ends_the_process();
    test_an_index_past_the_count_ends_the_process();
    test_iteration_verbs_of_a_list();
    test_view_of_a_static_list_is_constant();
    ::fixy::check::pass(kTest, "test_core_list: all tests passed\n");
    return 0;
}
