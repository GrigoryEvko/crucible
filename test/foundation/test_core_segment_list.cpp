// Tests of the SegmentList of the Region family: appends across each segment
// edge against an oracle, the runs of the segments, a Ref that lives across
// many appends and across a move, the windows of a walk, pinned and linear
// elements, the end of each element, the drain, two threads that read one list,
// and the deaths of a cursor, of a walk and of element code in an append.

#include <foundation/core/SegmentList.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/List.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
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

using ::foundation::core::Alloc;
using ::foundation::core::Box;
using ::foundation::core::ElementCount;
using ::foundation::core::FixedArray;
using ::foundation::core::FixedList;
using ::foundation::core::SegmentList;
using ::foundation::core::TextView;
using ::foundation::core::View;

constexpr Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The list that a call gives back by reference.  The rules use-after-move and
// borrow-match do not read through it, so a test reaches a moved-from list
// and a list in its own append.
template <class List>
[[nodiscard]] List& through(List& list) noexcept {
    return list;
}

// Elements of four sizes: segment 0 has 256, 32, 4 and 1 slots.  A large
// element holds its value in its first word and the complement in its last
// one, and a byte holds the low bits of its value.
template <std::size_t Between>
struct Wide final {
    std::uint64_t value = 0;
    FixedArray<std::uint64_t, Between> between{};
    std::uint64_t complement = ~std::uint64_t{0};
};
using Event = Wide<7>;
using Page = Wide<38>;
static_assert(sizeof(Event) == 72 && sizeof(Page) == 320);

template <class T>
[[nodiscard]] T element_of(std::uint64_t value) noexcept {
    if constexpr (std::is_class_v<T>) {
        return T{.value = value, .complement = ~value};
    } else {
        return static_cast<T>(value);
    }
}

template <class T>
[[nodiscard]] bool holds(T const& element, std::uint64_t value) noexcept {
    if constexpr (std::is_class_v<T>) {
        return element.value == value && element.complement == ~value;
    } else {
        return element == static_cast<T>(value);
    }
}

// The step of a sum, for a fold over the elements of a walk.
constexpr auto kAdd = [](std::uint64_t acc, std::uint64_t const& value) noexcept { return acc + value; };

constexpr std::size_t kOracleSlots = 1024;

// The values that a list must hold: a FixedArray and a count.
struct Oracle final {
    FixedArray<std::uint64_t, kOracleSlots> values{};
    std::size_t count = 0;

    void append(std::uint64_t value) noexcept {
        values.set(::foundation::core::Index<kOracleSlots>::check(count).expect("the oracle has room"), value);
        ++count;
    }

    [[nodiscard]] std::uint64_t at(std::size_t position) const noexcept {
        CRUCIBLE_FATAL_INVARIANT(position < count);
        return *values.get(position).expect("the position is below the count");
    }
};

// Each read of the list gives the elements of the oracle in order, and no
// element past them: get, the loop, the walk with each kind of terminal, and
// the walk of the segments.  Complexity: O(the count of the oracle).
template <class T>
void require_agreement(SegmentList<T> const& list, Oracle const& oracle) {
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == oracle.count);
    for (std::size_t position = 0; position != oracle.count; ++position) {
        T const& element = *list.get(position).expect("the position is below the count");
        CRUCIBLE_TEST_ASSERT(kTest, holds(element, oracle.at(position)));
    }
    for (std::size_t past = 0; past != 3; ++past) {
        CRUCIBLE_TEST_ASSERT(kTest, list.get(oracle.count + past).is_none());
        CRUCIBLE_TEST_ASSERT(kTest, list.get(~past).is_none());
    }
    std::size_t position = 0;
    for (T const& element : list) {
        CRUCIBLE_TEST_ASSERT(kTest, holds(element, oracle.at(position)));
        ++position;
    }
    CRUCIBLE_TEST_ASSERT(kTest, position == oracle.count);
    std::size_t const folded = list.walk().fold(std::size_t{0}, [&oracle](std::size_t at, T const& element) noexcept {
        return at < oracle.count && holds(element, oracle.at(at)) ? at + 1 : oracle.count + 1;
    });
    CRUCIBLE_TEST_ASSERT(kTest, folded == oracle.count && list.walk().size() == oracle.count);
    // Each run starts where the run before it ended, and no run is empty.
    std::size_t walked = 0;
    for (View<T const> run : list.segments()) {
        CRUCIBLE_TEST_ASSERT(kTest, run.size() != 0);
        for (T const& element : run) {
            CRUCIBLE_TEST_ASSERT(kTest, holds(element, oracle.at(walked)));
            ++walked;
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, walked == oracle.count);
    if (oracle.count == 0) {
        CRUCIBLE_TEST_ASSERT(kTest, list.last().is_none());
    } else {
        T const& last = *list.last().expect("the list holds an element");
        CRUCIBLE_TEST_ASSERT(kTest, holds(last, oracle.at(oracle.count - 1)));
    }
}

// True when a count is one before, at, or one after the end of a segment, for
// segment 0 of first slots.
[[nodiscard]] bool is_near_segment_edge(std::size_t count, std::size_t first) noexcept {
    std::size_t end = 0;
    for (std::size_t slots = first; end <= count + 1; slots *= 2) {
        if (count + 1 == end || count == end || count == end + 1) return true;
        end += slots;
    }
    return count + 1 == end || count == end || count == end + 1;
}

// The property: after each append near a segment edge, each read of the list
// agrees with the oracle.  The last count is past the sixth segment of the
// largest element and past the second one of the smallest.
template <class T>
void test_agrees_with_the_oracle(std::size_t first_slots, std::uint64_t seed) {
    ::crucible::test::PhiloxStream stream{seed};
    SegmentList<T> list;
    Oracle oracle{};
    require_agreement(list, oracle);
    while (oracle.count != kOracleSlots) {
        std::uint64_t const value = stream.next_wide();
        list.append(kAlloc, element_of<T>(value));
        oracle.append(value);
        if (is_near_segment_edge(oracle.count, first_slots)) {
            require_agreement(list, oracle);
        }
    }
    require_agreement(list, oracle);
}

// The run of the segment at position holds size elements.
template <class T>
void require_run(SegmentList<T> const& list, std::size_t position, std::size_t size) {
    for (View<T const> const run : list.segments().skip(ElementCount{position}).limit(ElementCount{1})) {
        CRUCIBLE_TEST_ASSERT(kTest, run.size() == size);
    }
}

// Each segment holds two times the slots of the segment before it, and the
// last one holds the rest: a list of count elements has a run of each size.
template <class T, class... Sizes>
void require_runs(std::size_t count, Sizes... sizes) {
    SegmentList<T> list;
    for (std::size_t value = 0; value != count; ++value) {
        list.append(kAlloc, element_of<T>(value));
    }
    CRUCIBLE_TEST_ASSERT(kTest, list.segments().size() == sizeof...(Sizes));
    [[maybe_unused]] std::size_t position = 0;
    (require_run(list, position++, std::size_t{sizes}), ...);
}

void test_segment_sizes() {
    require_runs<std::uint64_t>(0);
    require_runs<std::uint64_t>(1, 1u);
    require_runs<std::uint64_t>(32, 32u);
    require_runs<std::uint64_t>(33, 32u, 1u);
    require_runs<std::uint64_t>(96, 32u, 64u);
    require_runs<std::uint64_t>(100, 32u, 64u, 4u);
    require_runs<Event>(13, 4u, 8u, 1u);
    require_runs<Page>(8, 1u, 2u, 4u, 1u);
    require_runs<unsigned char>(800, 256u, 512u, 32u);
}

// A Ref of an element and a View of a segment stay correct across 1000
// appends and across a move of the list, because no element moves.  Under
// AddressSanitizer a read of storage that the list gave back ends the test.
void test_borrows_live_across_appends_and_a_move() {
    SegmentList<Event> list;
    list.append(kAlloc, element_of<Event>(71));
    list.append(kAlloc, element_of<Event>(72));
    auto const first = list.get(0).expect("the list holds two elements");
    auto const second = list.last().expect("the list holds two elements");
    View<Event const> const run = *list.segments().begin();
    for (std::uint64_t value = 0; value != 1000; ++value) {
        list.append(kAlloc, element_of<Event>(value));
        CRUCIBLE_TEST_ASSERT(kTest, holds(*first, 71) && holds(*second, 72));
    }
    CRUCIBLE_TEST_ASSERT(kTest, run.size() == 2 && holds(*run.get(1).expect("the run holds two elements"), 72));
    SegmentList<Event> const moved{std::move(list)};
    CRUCIBLE_TEST_ASSERT(kTest, moved.size() == 1002 && holds(*first, 71) && holds(*second, 72));
    CRUCIBLE_TEST_ASSERT(kTest, holds(*run.get(0).expect("the run holds two elements"), 71));
}

// A Ref of an owner that is an element lives across the appends too.  The rule
// view-escape reads no end in an append, because the element holds no borrow.
void test_a_ref_of_an_owner_lives_across_appends() {
    SegmentList<Box<std::uint64_t>> boxes;
    boxes.append(kAlloc, ::foundation::core::mint_box<std::uint64_t>(kAlloc, std::uint64_t{5}));
    auto const first = boxes.get(0).expect("the list holds one element");
    for (std::uint64_t value = 0; value != 100; ++value) {
        boxes.append(kAlloc, ::foundation::core::mint_box<std::uint64_t>(kAlloc, value));
    }
    std::uint64_t const held =
        ::foundation::core::with_view(*first, [](std::uint64_t const& value) noexcept { return value; });
    CRUCIBLE_TEST_ASSERT(kTest, held == 5 && boxes.size() == 101);
}

// A moved-from list is empty, and it takes new elements.  A move assignment
// ends the elements of its target.
void test_moves() {
    SegmentList<std::uint64_t> source;
    for (std::uint64_t value = 0; value != 50; ++value) {
        source.append(kAlloc, value);
    }
    SegmentList<std::uint64_t> target{std::move(through(source))};
    CRUCIBLE_TEST_ASSERT(kTest, target.size() == 50 && source.size() == 0 && source.get(0).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, source.last().is_none() && source.segments().size() == 0);
    source.append(kAlloc, 9);
    CRUCIBLE_TEST_ASSERT(kTest, source.size() == 1 && *source.get(0).expect("the list holds one element") == 9);
    target = std::move(source);
    CRUCIBLE_TEST_ASSERT(kTest, target.size() == 1 && *target.last().expect("the list holds one element") == 9);
}

// Each window of a walk gives the elements that the oracle has there, for
// random windows over several segments: a skip, a limit, and the two in each
// order.
void test_windows_of_a_walk(std::uint64_t seed) {
    ::crucible::test::PhiloxStream stream{seed};
    SegmentList<std::uint64_t> list;
    for (std::uint64_t value = 0; value != 500; ++value) {
        list.append(kAlloc, value * 7);
    }
    for (int turn = 0; turn != 400; ++turn) {
        std::uint64_t const skipped = stream.below(540);
        std::uint64_t const limited = stream.below(540);
        std::uint64_t const left = skipped < 500 ? 500 - skipped : 0;
        std::uint64_t const count = limited < left ? limited : left;
        std::uint64_t expected = skipped;
        std::uint64_t seen = 0;
        for (std::uint64_t const& element : list.walk().skip(ElementCount{skipped}).limit(ElementCount{limited})) {
            CRUCIBLE_TEST_ASSERT(kTest, element == expected * 7);
            ++expected;
            ++seen;
        }
        CRUCIBLE_TEST_ASSERT(kTest, seen == count);
        std::uint64_t const total =
            list.walk().skip(ElementCount{skipped}).limit(ElementCount{limited}).fold(std::uint64_t{0}, kAdd);
        CRUCIBLE_TEST_ASSERT(kTest, total == 7 * count * (2 * skipped + count - (count == 0 ? 0 : 1)) / 2);
        std::uint64_t const kept = limited < 500 ? limited : 500;
        std::uint64_t const rest = skipped < kept ? kept - skipped : 0;
        CRUCIBLE_TEST_ASSERT(kTest,
                             list.walk().limit(ElementCount{limited}).skip(ElementCount{skipped}).size() == rest);
    }
}

// The terminals that stop and the pairs of two walks read across the edges of
// the segments: any_of stops at the last element of the list, all_of reads
// each one, and zip pairs each element with the run of a FixedArray.
void test_terminals_and_pairs() {
    SegmentList<std::uint64_t> list;
    FixedArray<std::uint64_t, 150> squares{};
    for (std::uint64_t value = 0; value != 150; ++value) {
        list.append(kAlloc, value);
        squares.set(::foundation::core::Index<150>::check(value).expect("the index is below the count"), value * value);
    }
    CRUCIBLE_TEST_ASSERT(kTest, list.walk().any_of([](std::uint64_t const& value) noexcept { return value == 149; }));
    CRUCIBLE_TEST_ASSERT(kTest, !list.walk().any_of([](std::uint64_t const& value) noexcept { return value == 150; }));
    CRUCIBLE_TEST_ASSERT(kTest, list.walk().all_of([](std::uint64_t const& value) noexcept { return value < 150; }));
    CRUCIBLE_TEST_ASSERT(kTest, !list.walk().all_of([](std::uint64_t const& value) noexcept { return value != 33; }));
    std::uint64_t const pairs = list.walk()
                                    .try_zip(squares.view().walk())
                                    .expect("the two walks have 150 elements")
                                    .fold(std::uint64_t{0}, [](std::uint64_t acc, auto pair) noexcept {
                                        auto [value, square] = pair;
                                        return acc + (*square == *value * *value ? 1 : 0);
                                    });
    CRUCIBLE_TEST_ASSERT(kTest, pairs == 150);
    std::uint64_t const later_pairs = list.walk()
                                          .skip(ElementCount{40})
                                          .try_zip(squares.view().walk().skip(ElementCount{40}))
                                          .expect("the two walks have 110 elements")
                                          .fold(std::uint64_t{0}, [](std::uint64_t acc, auto pair) noexcept {
                                              auto [value, square] = pair;
                                              return acc + (*square == *value * *value && *value >= 40 ? 1 : 0);
                                          });
    CRUCIBLE_TEST_ASSERT(kTest, later_pairs == 110);
    std::uint64_t const even = list.walk()
                                   .filter([](std::uint64_t const& value) noexcept { return value % 2 == 0; })
                                   .map([](std::uint64_t const& value) noexcept { return value * 2; })
                                   .fold(std::uint64_t{0}, kAdd);
    CRUCIBLE_TEST_ASSERT(kTest, even == 2 * 2 * (74 * 75 / 2));
    std::uint64_t const longest = list.segments()
                                      .map([](View<std::uint64_t const> run) noexcept { return run.size(); })
                                      .fold(std::uint64_t{0}, [](std::uint64_t acc, std::size_t size) noexcept {
                                          return acc < size ? std::uint64_t{size} : acc;
                                      });
    CRUCIBLE_TEST_ASSERT(kTest, longest == 64);

    // The walk of the segments has the same terminals: a stop, the windows, and
    // the pairs of the segments after the first one with their sizes.
    FixedArray<std::size_t, 2> const sizes{std::size_t{64}, std::size_t{54}};
    std::uint64_t const sized = list.segments()
                                    .skip(ElementCount{1})
                                    .try_zip(sizes.view().walk())
                                    .expect("two segments come after the first one")
                                    .fold(std::uint64_t{0}, [](std::uint64_t acc, auto pair) noexcept {
                                        auto [run, size] = pair;
                                        return acc + (run.size() == *size ? 1 : 0);
                                    });
    CRUCIBLE_TEST_ASSERT(kTest, sized == 2);
    constexpr auto add_size = [](std::uint64_t acc, View<std::uint64_t const> run) noexcept {
        return acc + run.size();
    };
    CRUCIBLE_TEST_ASSERT(kTest, list.segments().skip(ElementCount{1}).fold(std::uint64_t{0}, add_size) == 118);
    std::uint64_t runs_read = 0;
    for (View<std::uint64_t const> const run : list.segments().limit(ElementCount{2})) {
        CRUCIBLE_TEST_ASSERT(kTest, run.size() == 32 * (runs_read + 1));
        ++runs_read;
    }
    CRUCIBLE_TEST_ASSERT(kTest, runs_read == 2 && list.segments().limit(ElementCount{2}).size() == 2);
    CRUCIBLE_TEST_ASSERT(
        kTest, list.segments().any_of([](View<std::uint64_t const> const& run) noexcept { return run.size() == 54; }));
    CRUCIBLE_TEST_ASSERT(
        kTest, !list.segments().any_of([](View<std::uint64_t const> const& run) noexcept { return run.size() == 55; }));
}

// Two cursors are equal only when the same count of elements comes after them:
// the end of one segment can be the address of the first slot of another one,
// and a loop that compares addresses only would then stop too early.
void test_cursors_compare_the_elements_after_them() {
    SegmentList<std::uint64_t> list;
    for (std::uint64_t value = 0; value != 40; ++value) {
        list.append(kAlloc, value);
    }
    auto walk = list.walk().limit(ElementCount{32});
    CRUCIBLE_TEST_ASSERT(kTest, list.begin() == list.begin() && walk.begin() == walk.begin());
    CRUCIBLE_TEST_ASSERT(kTest, *list.begin() == *walk.begin() && !(list.begin() == walk.begin()));
    CRUCIBLE_TEST_ASSERT(kTest, !(list.begin() == list.end()) && list.end() == list.end());
}

// A walk holds the count of its start, so an append during the walk adds no
// step to it.
void test_a_walk_keeps_the_count_of_its_start() {
    SegmentList<std::uint64_t> list;
    for (std::uint64_t value = 0; value != 40; ++value) {
        list.append(kAlloc, value);
    }
    std::uint64_t steps = 0;
    auto walk = list.walk();
    for (std::uint64_t const& element : walk) {
        through(list).append(kAlloc, element + 1000);
        ++steps;
    }
    CRUCIBLE_TEST_ASSERT(kTest, steps == 40 && list.size() == 80);
    CRUCIBLE_TEST_ASSERT(kTest, *list.get(40).expect("the list holds 80 elements") == 1000);
}

// A pinned element keeps its address: append_with builds it in place from its
// position, and no append moves it.
void test_pinned_elements() {
    SegmentList<::foundation::core::Atomic<std::uint64_t>> cells;
    for (int turn = 0; turn != 300; ++turn) {
        cells.append_with(
            kAlloc, [](std::size_t position) noexcept { return ::foundation::core::Atomic<std::uint64_t>{position}; });
    }
    std::uint64_t expected = 0;
    for (auto const& cell : cells) {
        CRUCIBLE_TEST_ASSERT(kTest, cell.load_acquire() == expected);
        ++expected;
    }
    CRUCIBLE_TEST_ASSERT(kTest, expected == 300);
}

// A make that reads the list sees the elements before its own one.
void test_make_reads_the_list() {
    SegmentList<std::uint64_t> list;
    for (int turn = 0; turn != 70; ++turn) {
        list.append_with(kAlloc, [&fixed = std::as_const(list)](std::size_t position) noexcept {
            CRUCIBLE_TEST_ASSERT(kTest, fixed.size() == position && fixed.get(position).is_none());
            CRUCIBLE_TEST_ASSERT(kTest, fixed.last().is_some() == (position != 0));
            return std::uint64_t{fixed.walk().size()} * 3;
        });
    }
    for (std::size_t position = 0; position != 70; ++position) {
        CRUCIBLE_TEST_ASSERT(kTest, *list.get(position).expect("a position below 70") == position * 3);
    }
}

// The order of the ends of some elements: the labels, in order.
struct EndLog final {
    FixedList<int, 400> labels{};
};

// An element that writes its label into a log at its end.  The brand of the
// Ref is a template argument, so no fresh brand leaves its scope.  A
// moved-from element writes nothing.
template <class LogRef>
struct Logged final {
    LogRef log;
    int label = 0;
    bool is_live = true;
    Logged(LogRef to_log, int from) noexcept : log{to_log}, label{from} {}
    Logged(Logged&& other) noexcept : log{other.log}, label{other.label} { other.is_live = false; }
    Logged(Logged const&) = delete;
    Logged& operator=(Logged&&) = delete;
    Logged& operator=(Logged const&) = delete;
    ~Logged() {
        if (is_live) {
            static_cast<void>(log->labels.append(label).expect("the log has room for each label"));
        }
    }
};

// The end of a list ends each element one time, from the last one.  A move of
// the list ends none, and an append across a segment edge ends none.
void test_each_element_ends_once_in_reverse() {
    EndLog log{};
    auto const log_ref = ::foundation::core::to(log);
    using Element = Logged<std::remove_const_t<decltype(log_ref)>>;
    {
        SegmentList<Element> list;
        for (int label = 0; label != 300; ++label) {
            list.append(kAlloc, Element{log_ref, label});
        }
        CRUCIBLE_TEST_ASSERT(kTest, log.labels.size() == 0);
        SegmentList<Element> const moved{std::move(list)};
        CRUCIBLE_TEST_ASSERT(kTest, log.labels.size() == 0 && moved.size() == 300);
    }
    CRUCIBLE_TEST_ASSERT(kTest, log.labels.size() == 300);
    int expected = 299;
    for (int const label : log.labels) {
        CRUCIBLE_TEST_ASSERT(kTest, label == expected);
        --expected;
    }
}

// A move assignment ends the old elements of the target one time, and it keeps
// the elements of the source.
void test_assignment_ends_the_old_elements() {
    EndLog log{};
    auto const log_ref = ::foundation::core::to(log);
    using Element = Logged<std::remove_const_t<decltype(log_ref)>>;
    {
        SegmentList<Element> target;
        SegmentList<Element> source;
        for (int label = 0; label != 40; ++label) {
            target.append(kAlloc, Element{log_ref, label});
            source.append(kAlloc, Element{log_ref, label + 100});
        }
        target = std::move(source);
        CRUCIBLE_TEST_ASSERT(kTest, log.labels.size() == 40 && target.size() == 40);
        CRUCIBLE_TEST_ASSERT(kTest, target.get(0).expect("the list holds 40 elements")->label == 100);
    }
    CRUCIBLE_TEST_ASSERT(kTest, log.labels.size() == 80);
}

// A drain gives each element one time, in the order of the appends, and the
// body owns it.  The list is empty before the first call, so the body can
// append to it.
void test_drain() {
    EndLog log{};
    auto const log_ref = ::foundation::core::to(log);
    using Element = Logged<std::remove_const_t<decltype(log_ref)>>;
    SegmentList<Element> list;
    for (int label = 0; label != 100; ++label) {
        list.append(kAlloc, Element{log_ref, label});
    }
    int calls = 0;
    std::move(through(list)).drain([&calls, &log, &list](Element element) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, element.label == calls && log.labels.size() == static_cast<std::size_t>(calls));
        CRUCIBLE_TEST_ASSERT(kTest, list.size() == (calls < 50 ? 0 : 1));
        ++calls;
        if (calls == 50) {
            through(list).append(kAlloc, Element{element.log, 1000});
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, calls == 100 && log.labels.size() == 100 && list.size() == 1);
    int expected = 0;
    for (int const label : log.labels) {
        CRUCIBLE_TEST_ASSERT(kTest, label == expected);
        ++expected;
    }
    CRUCIBLE_TEST_ASSERT(kTest, list.last().expect("the body appended one element")->label == 1000);
    SegmentList<std::uint64_t>{}.drain([](std::uint64_t) noexcept { ::foundation::core::fatal("no element"); });
}

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

// A list of a linear element is linear.  A drain gives each element to its
// consumer, and expect_empty ends a list that holds none.
void test_linear_elements() {
    SegmentList<Token> list;
    for (int id = 0; id != 90; ++id) {
        list.append(kAlloc, Token{id});
    }
    CRUCIBLE_TEST_ASSERT(kTest, list.get(80).expect("the list holds 90 tokens")->id == 80);
    int expected = 0;
    std::move(list).drain([&expected](Token token) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, token.id == expected);
        ++expected;
        ::foundation::linear::drop(std::move(token));
    });
    CRUCIBLE_TEST_ASSERT(kTest, expected == 90);
    SegmentList<Token> empty;
    std::move(empty).expect_empty("the list never held a token");
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            SegmentList<int> held;
            held.append(kAlloc, 7);
            std::move(held).expect_empty("the list still holds one element");
        },
        TextView{"foundation: fatal: the list still holds one element"});
}

// A list of lists: each inner list keeps its elements at each append of the
// outer one.
void test_nested_lists() {
    SegmentList<SegmentList<int>> pools;
    for (int pool = 0; pool != 30; ++pool) {
        SegmentList<int> members;
        for (int member = 0; member <= pool; ++member) {
            members.append(kAlloc, pool * 100 + member);
        }
        pools.append(kAlloc, std::move(members));
    }
    int pool = 0;
    for (SegmentList<int> const& members : pools) {
        CRUCIBLE_TEST_ASSERT(kTest, members.size() == static_cast<std::size_t>(pool) + 1);
        CRUCIBLE_TEST_ASSERT(kTest, *members.last().expect("each pool holds a member") == pool * 101);
        ++pool;
    }
    CRUCIBLE_TEST_ASSERT(kTest, pool == 30);
}

// Two threads read one list at the same time through the typed thread door:
// the list is Shareable, so a body can hold a reference to a const list.
// ThreadSanitizer sees no race, because no read writes the list.
void test_two_threads_read_one_list() {
    constexpr std::uint64_t kCount = 5000;
    SegmentList<std::uint64_t> list;
    for (std::uint64_t value = 0; value != kCount; ++value) {
        list.append(kAlloc, value);
    }
    auto walk_each = [&fixed = std::as_const(list)] noexcept { return fixed.walk().fold(std::uint64_t{0}, kAdd); };
    auto get_each = [&fixed = std::as_const(list)] noexcept {
        std::uint64_t total = 0;
        for (std::size_t position = 0; position != fixed.size(); ++position) {
            total += *fixed.get(position).expect("the position is below the count");
        }
        return total;
    };
    auto const sums =
        ::fixy::thread::run_joined(::fixy::BgLoadCtx{::foundation::effects::testing::bg()},
                                   ::fixy::thread::ThreadOptions{}, std::move(walk_each), std::move(get_each))
            .expect("the test starts two threads");
    CRUCIBLE_TEST_ASSERT(kTest, sums.at(::fixy::index<0>()) == kCount * (kCount - 1) / 2);
    CRUCIBLE_TEST_ASSERT(kTest, sums.at(::fixy::index<1>()) == kCount * (kCount - 1) / 2);
}

// A read and a step through the end cursor end the process, for the loop over
// the elements and for the loop over the segments, also of an empty list.
void test_cursors_at_their_end_die() {
    constexpr TextView expected{
        "foundation: fatal: a read or a step through the end cursor of a loop over a SegmentList"};
    SegmentList<std::uint64_t> list;
    for (std::uint64_t value = 0; value != 32; ++value) {
        list.append(kAlloc, value);
    }
    SegmentList<std::uint64_t> const empty;
    ::fixy::check::expect_dies(kTest, [&list] noexcept { static_cast<void>(*list.end()); }, expected);
    ::fixy::check::expect_dies(
        kTest,
        [&list] noexcept {
            auto cursor = list.end();
            ++cursor;
        },
        expected);
    ::fixy::check::expect_dies(kTest, [&empty] noexcept { static_cast<void>(*empty.begin()); }, expected);
    ::fixy::check::expect_dies(
        kTest, [&list] noexcept { static_cast<void>(*list.segments().skip(ElementCount{1}).begin()); }, expected);
    ::fixy::check::expect_dies(
        kTest,
        [&empty] noexcept {
            auto cursor = empty.segments().begin();
            ++cursor;
        },
        expected);
}

// A walk of a list that moved away ends the process at its next segment, for
// each form of the walk.
void test_a_walk_of_a_moved_list_dies() {
    constexpr TextView expected{
        "foundation: fatal: a walk reached a segment of a SegmentList that moved away during the walk"};
    SegmentList<std::uint64_t> list;
    for (std::uint64_t value = 0; value != 100; ++value) {
        list.append(kAlloc, value);
    }
    ::fixy::check::expect_dies(
        kTest,
        [&list] noexcept {
            auto walk = through(list).walk();
            SegmentList<std::uint64_t> const moved{std::move(list)};
            static_cast<void>(std::move(walk).fold(std::uint64_t{0}, kAdd));
        },
        expected);
    ::fixy::check::expect_dies(
        kTest,
        [&list] noexcept {
            auto cursor = through(list).begin();
            SegmentList<std::uint64_t> const moved{std::move(list)};
            for (int step = 0; step != 33; ++step) {
                ++cursor;
            }
        },
        expected);
    ::fixy::check::expect_dies(
        kTest,
        [&list] noexcept {
            auto runs = through(list).segments();
            SegmentList<std::uint64_t> const moved{std::move(list)};
            static_cast<void>(*runs.begin());
        },
        expected);
}

// An element whose move runs an action on the list of its home: it stands for
// the code of an element that reaches the list that holds it.  to() of a
// branded object takes the brand of the object, so the element names the type
// of its Ref.
enum class MoveAction : unsigned char {
    none,
    append,
    move_away,
    assign_over,
};

struct ReentrantHome;
struct ReentrantHomeBrand final {};
using HomeRef = ::foundation::core::Ref<ReentrantHome, ReentrantHomeBrand>;

struct Reentrant final {
    HomeRef home;
    MoveAction action = MoveAction::none;

    Reentrant(HomeRef to_home, MoveAction on_move) noexcept : home{to_home}, action{on_move} {}
    Reentrant(Reentrant&& other) noexcept;
    Reentrant(Reentrant const&) = delete("an element of the test has one identity");
    Reentrant& operator=(Reentrant const&) = delete("an element of the test has one identity");
    ~Reentrant() = default;
};

struct ReentrantHome final {
    using brand_type = ReentrantHomeBrand;
    SegmentList<Reentrant> list{};
};

Reentrant::Reentrant(Reentrant&& other) noexcept : home{other.home}, action{MoveAction::none} {
    if (other.action == MoveAction::append) {
        home->list.append(kAlloc, Reentrant{home, MoveAction::none});
    } else if (other.action == MoveAction::move_away) {
        SegmentList<Reentrant> const taken{std::move(home->list)};
        CRUCIBLE_TEST_ASSERT(kTest, taken.size() != 0);
    } else if (other.action == MoveAction::assign_over) {
        home->list = SegmentList<Reentrant>{};
    }
}

// An append, a move, a drain and the end of a list from code of an element in
// an append end the process, before the list gives the slot to a second
// element and before its storage goes.  The first element and a later one take
// the same road.
void test_element_code_in_an_append_dies() {
    constexpr TextView expected{"foundation: fatal: an append, a move, a drain or the end of a SegmentList while an "
                                "append builds an element of it"};
    FixedArray<MoveAction, 3> const actions{MoveAction::append, MoveAction::move_away, MoveAction::assign_over};
    for (int held = 0; held != 2; ++held) {
        auto const fill = [held](SegmentList<std::uint64_t>& list) noexcept {
            for (int value = 0; value != held; ++value) {
                list.append(kAlloc, 1);
            }
        };
        ::fixy::check::expect_dies(
            kTest,
            [&fill] noexcept {
                SegmentList<std::uint64_t> list;
                fill(list);
                list.append_with(kAlloc, [&list](std::size_t) noexcept {
                    through(list).append(kAlloc, 2);
                    return std::uint64_t{1};
                });
            },
            expected);
        ::fixy::check::expect_dies(
            kTest,
            [&fill] noexcept {
                SegmentList<std::uint64_t> list;
                fill(list);
                list.append_with(kAlloc, [&list](std::size_t) noexcept {
                    through(list) = SegmentList<std::uint64_t>{};
                    return std::uint64_t{1};
                });
            },
            expected);
        ::fixy::check::expect_dies(
            kTest,
            [&fill] noexcept {
                SegmentList<std::uint64_t> list;
                fill(list);
                list.append_with(kAlloc, [&list](std::size_t) noexcept {
                    std::move(through(list)).drain([](std::uint64_t) noexcept {});
                    return std::uint64_t{1};
                });
            },
            expected);
        for (MoveAction const action : actions) {
            ::fixy::check::expect_dies(
                kTest,
                [held, action] noexcept {
                    ReentrantHome home{};
                    HomeRef const home_ref = ::foundation::core::to(home);
                    for (int value = 0; value != held; ++value) {
                        home.list.append(kAlloc, Reentrant{home_ref, MoveAction::none});
                    }
                    home.list.append(kAlloc, Reentrant{home_ref, action});
                },
                expected);
        }
    }
    // The control: an element with no action goes into the list.
    ReentrantHome home{};
    HomeRef const home_ref = ::foundation::core::to(home);
    home.list.append(kAlloc, Reentrant{home_ref, MoveAction::none});
    CRUCIBLE_TEST_ASSERT(kTest, home.list.size() == 1);
}

}  // namespace

int main() {
    test_agrees_with_the_oracle<unsigned char>(256, 0x5EA1);
    test_agrees_with_the_oracle<std::uint64_t>(32, 0x5EA2);
    test_agrees_with_the_oracle<Event>(4, 0x5EA3);
    test_agrees_with_the_oracle<Page>(1, 0x5EA4);
    test_segment_sizes();
    test_borrows_live_across_appends_and_a_move();
    test_a_ref_of_an_owner_lives_across_appends();
    test_moves();
    test_windows_of_a_walk(0x5EA5);
    test_terminals_and_pairs();
    test_cursors_compare_the_elements_after_them();
    test_a_walk_keeps_the_count_of_its_start();
    test_pinned_elements();
    test_make_reads_the_list();
    test_each_element_ends_once_in_reverse();
    test_assignment_ends_the_old_elements();
    test_drain();
    test_linear_elements();
    test_nested_lists();
    test_two_threads_read_one_list();
    test_cursors_at_their_end_die();
    test_a_walk_of_a_moved_list_dies();
    test_element_code_in_an_append_dies();
    return 0;
}
