// Tests of the live list of the Region family: the three appends against an
// oracle across each segment edge, a prefix that keeps its count, a Ref and a
// View that live across the appends, pinned elements and elements that own
// storage, element code in an append, and one writer beside three readers that
// compare each prefix with the oracle at each segment edge.  ThreadSanitizer
// runs the same binary.

#include <foundation/core/LiveList.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Index.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

#include "philox_stream.h"
#include "segment_oracle.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::crucible::test::element_of;
using ::crucible::test::Event;
using ::crucible::test::holds;
using ::crucible::test::is_near_segment_edge;
using ::crucible::test::Oracle;
using ::crucible::test::oracle_slots;
using ::crucible::test::Page;
using ::crucible::test::require_agreement;
using ::crucible::test::require_first_elements;
using ::foundation::core::Alloc;
using ::foundation::core::Atomic;
using ::foundation::core::Box;
using ::foundation::core::CacheLine;
using ::foundation::core::FixedArray;
using ::foundation::core::LiveList;
using ::foundation::core::SegmentPrefix;
using ::foundation::core::TextView;
using ::foundation::core::View;

constexpr Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The one writer of each list of the test: it makes the proof for each append.
// Each case appends from one thread.
class ListWriter final {
    struct AppendRole final {};

public:
    using Proof = ::foundation::SoleWriterProof<ListWriter, AppendRole>;

    template <class T>
    static void append(LiveList<T, Proof>& list, T value) noexcept {
        list.append(Proof{}, kAlloc, std::move(value));
    }

    template <class T, class Make>
    static void append_with(LiveList<T, Proof>& list, Make&& make) noexcept {
        list.append_with(Proof{}, kAlloc, static_cast<Make&&>(make));
    }

    template <class T>
    static void append_each(LiveList<T, Proof>& list, View<T const> run) noexcept {
        list.append_each(Proof{}, kAlloc, run);
    }
};

template <class T>
using Live = LiveList<T, ListWriter::Proof>;

// The list that a call gives back by reference.  The rule borrow-match does
// not read through it, so a test reaches a list in its own append.
template <class List>
[[nodiscard]] List& through(List& list) noexcept {
    return list;
}

// The most elements of one append_each of the test: some batches are larger
// than the first segments, so they cross one edge or two.
constexpr std::size_t kBatch = 24;

// The elements of the values from first, for an append_each.
template <class T>
[[nodiscard]] FixedArray<T, kBatch> batch_of(std::uint64_t first) noexcept {
    FixedArray<T, kBatch> batch{};
    for (std::size_t offset = 0; offset != kBatch; ++offset) {
        batch.set(::foundation::core::Index<kBatch>::check(offset).expect("the offset is below the batch"),
                  element_of<T>(first + offset));
    }
    return batch;
}

// The property: after each append near a segment edge, each read of the prefix
// agrees with the oracle.  The stream selects the append, so each of the three
// meets each edge for some element.
template <class T>
void test_agrees_with_the_oracle(std::size_t first_slots, std::uint64_t seed) {
    ::crucible::test::PhiloxStream stream{seed};
    Live<T> list;
    Oracle oracle{};
    require_agreement(kTest, list.prefix(), oracle);
    while (oracle.count != oracle_slots) {
        std::uint64_t const value = stream.next_wide();
        std::uint64_t const form = stream.below(3);
        std::size_t const before = oracle.count;
        if (form == 0) {
            ListWriter::append(list, element_of<T>(value));
            oracle.append(value);
        } else if (form == 1) {
            ListWriter::append_with(list, [value, before](std::size_t position) noexcept {
                CRUCIBLE_TEST_ASSERT(kTest, position == before);
                return element_of<T>(value);
            });
            oracle.append(value);
        } else {
            std::size_t const room = oracle_slots - before;
            std::size_t const drawn = stream.below(kBatch + 1);
            std::size_t const count = drawn < room ? drawn : room;
            FixedArray<T, kBatch> const batch = batch_of<T>(value);
            ListWriter::append_each(list, batch.view().window(0, count).expect("the count is at most the batch"));
            for (std::size_t offset = 0; offset != count; ++offset) {
                oracle.append(value + offset);
            }
        }
        if (form == 2 || is_near_segment_edge(oracle.count, first_slots)) {
            require_agreement(kTest, list.prefix(), oracle);
        }
    }
    require_agreement(kTest, list.prefix(), oracle);
}

// A prefix keeps the count of its start across the appends of each form, and a
// later prefix has the new elements.
void test_a_prefix_keeps_its_count() {
    Live<Event> list;
    Oracle oracle{};
    for (std::uint64_t value = 0; value != 5; ++value) {
        ListWriter::append(list, element_of<Event>(value));
        oracle.append(value);
    }
    SegmentPrefix<Event const> const early = list.prefix();
    SegmentPrefix<Event const> const copied = early;
    for (std::uint64_t value = 5; value != 300; ++value) {
        ListWriter::append(list, element_of<Event>(value));
        oracle.append(value);
        require_first_elements(kTest, early, oracle, 5);
    }
    require_first_elements(kTest, copied, oracle, 5);
    require_agreement(kTest, list.prefix(), oracle);
    SegmentPrefix<Event const> latest = copied;
    latest = list.prefix();
    require_agreement(kTest, latest, oracle);
}

// A Ref of an element, a View of a segment and a walk stay correct across 1000
// appends, because no element moves and no word of the directory that a prefix
// reads changes.  Under AddressSanitizer a read of storage that the list gave
// back ends the test.
void test_borrows_live_across_appends() {
    Live<Event> list;
    ListWriter::append(list, element_of<Event>(71));
    ListWriter::append(list, element_of<Event>(72));
    SegmentPrefix<Event const> const prefix = list.prefix();
    auto const first = prefix.get(0).expect("the prefix holds two elements");
    auto const second = list.prefix().last().expect("the prefix holds two elements");
    View<Event const> const run = *prefix.segments().begin();
    auto walk = prefix.walk();
    for (std::uint64_t value = 0; value != 1000; ++value) {
        ListWriter::append(list, element_of<Event>(value));
        CRUCIBLE_TEST_ASSERT(kTest, holds(*first, 71) && holds(*second, 72));
    }
    CRUCIBLE_TEST_ASSERT(kTest, run.size() == 2 && holds(*run.get(1).expect("the run holds two elements"), 72));
    std::uint64_t const sum = std::move(walk).fold(
        std::uint64_t{0}, [](std::uint64_t acc, Event const& element) noexcept { return acc + element.value; });
    CRUCIBLE_TEST_ASSERT(kTest, sum == 71 + 72 && prefix.size() == 2 && list.prefix().size() == 1002);
}

// A pinned element comes through append_with, and a reader uses it as a cell.
void test_pinned_elements() {
    Live<Atomic<std::uint64_t>> cells;
    for (int turn = 0; turn != 300; ++turn) {
        ListWriter::append_with(
            cells, [](std::size_t position) noexcept { return Atomic<std::uint64_t>{std::uint64_t{position}}; });
    }
    std::uint64_t expected = 0;
    for (Atomic<std::uint64_t> const& cell : cells.prefix()) {
        CRUCIBLE_TEST_ASSERT(kTest, cell.load_acquire() == expected);
        ++expected;
    }
    CRUCIBLE_TEST_ASSERT(kTest, expected == 300);
}

// An element that owns storage moves into its slot, and the end of the list
// ends it.  AddressSanitizer reports an element that the list ends two times or
// not at all.
void test_elements_that_own_storage() {
    Live<Box<std::uint64_t>> boxes;
    for (std::uint64_t value = 0; value != 100; ++value) {
        ListWriter::append(boxes, ::foundation::core::mint_box<std::uint64_t>(kAlloc, value));
    }
    std::uint64_t expected = 0;
    for (Box<std::uint64_t> const& box : boxes.prefix()) {
        std::uint64_t const held =
            ::foundation::core::with_view(box, [](std::uint64_t const& value) noexcept { return value; });
        CRUCIBLE_TEST_ASSERT(kTest, held == expected);
        ++expected;
    }
    CRUCIBLE_TEST_ASSERT(kTest, expected == 100);
}

// A make that reads the list sees the published elements, and the new one is
// not one of them.
void test_make_reads_the_list() {
    Live<std::uint64_t> list;
    for (int turn = 0; turn != 70; ++turn) {
        ListWriter::append_with(list, [&fixed = std::as_const(list)](std::size_t position) noexcept {
            CRUCIBLE_TEST_ASSERT(kTest, fixed.prefix().size() == position && fixed.prefix().get(position).is_none());
            return std::uint64_t{fixed.prefix().walk().size()} * 3;
        });
    }
    for (std::size_t position = 0; position != 70; ++position) {
        CRUCIBLE_TEST_ASSERT(kTest, *list.prefix().get(position).expect("a position below 70") == position * 3);
    }
}

// An append from code of an element in an append ends the process, before the
// list gives the slot to a second element.  The first element and a later one
// take the same road.
void test_element_code_in_an_append_dies() {
    constexpr TextView expected{"foundation: fatal: an append, a move, a drain or the end of a SegmentList while an "
                                "append builds an element of it"};
    for (std::uint64_t held = 0; held != 2; ++held) {
        ::fixy::check::expect_dies(
            kTest,
            [held] noexcept {
                Live<std::uint64_t> list;
                for (std::uint64_t value = 0; value != held; ++value) {
                    ListWriter::append(list, value);
                }
                ListWriter::append_with(list, [&list](std::size_t) noexcept {
                    ListWriter::append(through(list), std::uint64_t{2});
                    return std::uint64_t{1};
                });
            },
            expected);
        ::fixy::check::expect_dies(
            kTest,
            [held] noexcept {
                Live<std::uint64_t> list;
                FixedArray<std::uint64_t, 2> const run{std::uint64_t{7}, std::uint64_t{8}};
                ListWriter::append_each(list, run.view().window(0, held).expect("held is at most two"));
                ListWriter::append_with(list, [&list, &run](std::size_t) noexcept {
                    ListWriter::append_each(through(list), View<std::uint64_t const>{run.view()});
                    return std::uint64_t{1};
                });
            },
            expected);
    }
}

// The elements of the case of one writer beside three readers, and the count
// of the elements of one append_each in it.
constexpr std::size_t kCount = 3000;
constexpr std::size_t kRun = 7;

// The readers, and the index of a reader.
constexpr std::size_t kReaders = 3;
using Reader = ::foundation::core::Index<kReaders>;

// The count that each reader read last.  Only a reader writes its cell, and the
// writer loads it.  So the cells give an order from a reader to the writer and
// no order from the writer to a reader.  The published count of the list is
// the one such order, and ThreadSanitizer reports each read of an element or of
// a word of the directory that it does not cover.
struct Acknowledged final {
    struct Cell final {
        CacheLine<Atomic<std::size_t>> count{std::size_t{0}};
    };
    FixedArray<Cell, kReaders> cells{};

    void set(Reader reader, std::size_t count) noexcept {
        cells.with_element(reader, [count](Cell& cell) noexcept { cell.count->store_release(count); });
    }

    // Waits until each reader read the first count elements.
    void wait_for(std::size_t count) const noexcept {
        for (Cell const& cell : cells) {
            while (cell.count->load_acquire() < count)
                CRUCIBLE_SPIN_PAUSE;
        }
    }
};

// The end of the segment that holds a position, for segment 0 of first slots.
[[nodiscard]] std::size_t end_of_segment(std::size_t position, std::size_t first) noexcept {
    std::size_t end = first;
    for (std::size_t slots = first * 2; end <= position; slots *= 2) {
        end += slots;
    }
    return end;
}

// One writer appends kCount elements, and each of three readers reads them at
// the same time through the elements of one kind.  The writer waits for each
// reader after the append that fills a segment and after the append that opens
// one.  So each reader reads the last element of each segment, and the first
// one, from a prefix that has no later element.  A reader reads the new
// elements from the newest one.
template <class Kind>
void test_one_writer_beside_three_readers() {
    using T = typename Kind::Element;
    Live<T> list;
    Acknowledged acknowledged;
    auto read = [&list, &acknowledged](::fixy::thread::BesideIndex index) noexcept {
        Reader const reader = Reader::check(index.raw()).expect("the index of a thread is below the readers");
        std::size_t seen = 0;
        while (seen != kCount) {
            SegmentPrefix<T const> const prefix = list.prefix();
            std::size_t const count = prefix.size();
            if (count == seen) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            CRUCIBLE_TEST_ASSERT(kTest, count > seen && count <= kCount);
            for (std::size_t position = count; position != seen; --position) {
                T const& element = *prefix.get(position - 1).expect("the position is below the count");
                CRUCIBLE_TEST_ASSERT(kTest, Kind::holds_position(element, position - 1));
            }
            T const& last = *prefix.last().expect("the prefix holds an element");
            CRUCIBLE_TEST_ASSERT(kTest, Kind::holds_position(last, count - 1));
            seen = count;
            acknowledged.set(reader, seen);
        }
        // The loop, the walk and the segments of a prefix, while the other
        // readers read too.
        SegmentPrefix<T const> const prefix = list.prefix();
        std::size_t position = 0;
        for (T const& element : prefix) {
            CRUCIBLE_TEST_ASSERT(kTest, Kind::holds_position(element, position));
            ++position;
        }
        std::size_t walked = 0;
        for (View<T const> run : prefix.segments()) {
            for (T const& element : run) {
                CRUCIBLE_TEST_ASSERT(kTest, Kind::holds_position(element, walked));
                ++walked;
            }
        }
        CRUCIBLE_TEST_ASSERT(kTest, position == kCount && walked == kCount);
    };
    auto write = [&list, &acknowledged]() noexcept {
        std::size_t segment = 0;
        std::size_t count = 0;
        while (count != kCount) {
            std::size_t const end = end_of_segment(count, Kind::first_slots);
            count += Kind::append_at(list, count, segment % Kind::forms, kCount - count);
            if (count < end) continue;
            // The append filled its segment, or it opened the next one.
            acknowledged.wait_for(count);
            ++segment;
            if (count == end && count != kCount) {
                count += Kind::append_at(list, count, segment % Kind::forms, kCount - count);
                acknowledged.wait_for(count);
            }
        }
        return count;
    };
    auto const readers = ::fixy::thread::BesideCount<kReaders>::check(kReaders).expect("three is at most three");
    std::size_t const written = ::fixy::thread::run_beside_each(::fixy::BgLoadCtx{::foundation::effects::testing::bg()},
                                                                ::fixy::thread::ThreadOptions{}, readers, read, write)
                                    .expect("the test starts three threads");
    CRUCIBLE_TEST_ASSERT(kTest, written == kCount && list.prefix().size() == kCount);
}

// Elements of 72 bytes, through each of the three appends.  The form of the
// append changes with the segment, so each form fills a segment and opens one.
struct EventKind final {
    using Element = Event;
    static constexpr std::size_t first_slots = 4;
    static constexpr std::size_t forms = 3;

    [[nodiscard]] static bool holds_position(Event const& element, std::size_t position) noexcept {
        return holds(element, std::uint64_t{position} * 5);
    }

    // Appends some elements from position, at most room, and gives their count.
    [[nodiscard]] static std::size_t append_at(Live<Event>& list, std::size_t position, std::size_t form,
                                               std::size_t room) noexcept {
        if (form == 0) {
            ListWriter::append(list, element_of<Event>(std::uint64_t{position} * 5));
            return 1;
        }
        if (form == 1) {
            ListWriter::append_with(list,
                                    [](std::size_t at) noexcept { return element_of<Event>(std::uint64_t{at} * 5); });
            return 1;
        }
        FixedArray<Event, kRun> run{};
        for (std::size_t offset = 0; offset != kRun; ++offset) {
            run.set(::foundation::core::Index<kRun>::check(offset).expect("the offset is below the run"),
                    element_of<Event>(std::uint64_t{position + offset} * 5));
        }
        std::size_t const count = kRun < room ? kRun : room;
        ListWriter::append_each(list, run.view().window(0, count).expect("the count is at most the run"));
        return count;
    }
};

// Elements that own storage, so a reader reads an object that the writer built
// before it published the element.
struct BoxKind final {
    using Element = Box<std::uint64_t>;
    static constexpr std::size_t first_slots = 32;
    static constexpr std::size_t forms = 2;

    [[nodiscard]] static bool holds_position(Box<std::uint64_t> const& element, std::size_t position) noexcept {
        return ::foundation::core::with_view(
            element, [position](std::uint64_t const& value) noexcept { return value == std::uint64_t{position} * 3; });
    }

    [[nodiscard]] static std::size_t append_at(Live<Box<std::uint64_t>>& list, std::size_t position, std::size_t form,
                                               std::size_t /*room*/) noexcept {
        if (form == 0) {
            ListWriter::append(list, ::foundation::core::mint_box<std::uint64_t>(kAlloc, std::uint64_t{position} * 3));
        } else {
            ListWriter::append_with(list, [](std::size_t at) noexcept {
                return ::foundation::core::mint_box<std::uint64_t>(kAlloc, std::uint64_t{at} * 3);
            });
        }
        return 1;
    }
};
static_assert(sizeof(Box<std::uint64_t>) == 8);

}  // namespace

int main() {
    test_agrees_with_the_oracle<unsigned char>(256, 0x11F1);
    test_agrees_with_the_oracle<std::uint64_t>(32, 0x11F2);
    test_agrees_with_the_oracle<Event>(4, 0x11F3);
    test_agrees_with_the_oracle<Page>(1, 0x11F4);
    test_a_prefix_keeps_its_count();
    test_borrows_live_across_appends();
    test_pinned_elements();
    test_elements_that_own_storage();
    test_make_reads_the_list();
    test_element_code_in_an_append_dies();
    test_one_writer_beside_three_readers<EventKind>();
    test_one_writer_beside_three_readers<BoxKind>();
    return 0;
}
