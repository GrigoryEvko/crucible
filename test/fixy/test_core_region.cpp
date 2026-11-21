// Tests of foundation/core/Region.h and of OwnedMmap::view: the View of a
// mapping, its windows, copy and fill.
//
// The storage of each View is an anonymous mapping, because a mapping is
// the owner that gives a View in this tree.  map_region takes the mapping
// out of the Result of its mint.

#include <fixy/Core.h>
#include <fixy/os/Mmap.h>
#include <foundation/Platform.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "../foundation/philox_stream.h"

#include <bit>
#include <cstdint>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {

using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

struct ScratchRegion final {
    using permission_row = eff::Row<>;
};

using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
struct EmptyRegionBrand {};
using EmptyMapping =
    fixy::OwnedMmap<ScratchRegion, fixy::mmap::prot::WriteCopy, fixy::mmap::share::Anonymous, EmptyRegionBrand>;

constexpr std::size_t page_bytes = 4096;

// A record of the shape of a metadata record: words and narrower fields,
// with no padding.
struct Record {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
    std::uint64_t extra = 0;
};

enum class Shade : std::uint8_t {
    dark,
    light,
    bright
};

// A record of one byte.
struct Tint {
    std::uint8_t level = 0;

    [[nodiscard]] friend bool operator==(Tint, Tint) noexcept = default;
};

template <class Owner>
[[nodiscard]] auto map_region(IoBlockCtx const& ctx, Owner const& owner, std::size_t bytes) {
    return fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, owner, bytes)
        .expect("an anonymous mapping of the test region succeeds")
        .consume();
}

// Each element of first equals the element at the same position of second.
template <class T>
[[nodiscard]] bool hold_same_values(fixy::View<T const> first, fixy::View<T const> second) {
    if (first.size() != second.size()) return false;
    auto other = second.begin();
    for (T const& value : first) {
        if (!(value == *other)) return false;
        ++other;
    }
    return true;
}

[[nodiscard]] std::uintptr_t address_of_first(fixy::View<std::uint64_t const> view) {
    return std::bit_cast<std::uintptr_t>(&*view.begin());
}

void test_view_of_a_mapping() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
    auto region = map_region(ctx, owner, page_bytes);

    fixy::View<std::uint64_t> words = region.view<std::uint64_t>().expect("a page holds whole words");
    CRUCIBLE_FATAL_INVARIANT(words.size() == page_bytes / sizeof(std::uint64_t));
    std::uint64_t zero_count = 0;
    for (std::uint64_t const word : words) {
        if (word == 0) ++zero_count;
    }
    CRUCIBLE_FATAL_INVARIANT(zero_count == words.size());

    fixy::fill(words, std::uint64_t{0xABCD});
    fixy::View<std::uint64_t const> read_back = region.view<std::uint64_t const>().expect("a page holds whole words");
    for (std::uint64_t const word : read_back) {
        CRUCIBLE_FATAL_INVARIANT(word == 0xABCD);
    }

    // A const mapping gives a View of const elements.
    auto const& fixed_region = region;
    fixy::View<std::uint64_t const> fixed_words =
        fixed_region.view<std::uint64_t const>().expect("a page holds whole words");
    CRUCIBLE_FATAL_INVARIANT(fixed_words.size() == words.size());
    CRUCIBLE_FATAL_INVARIANT(address_of_first(fixed_words) == address_of_first(words));

    // Three pages hold a whole number of records of 24 bytes.
    auto record_region = map_region(ctx, owner, 3 * page_bytes);
    fixy::View<Record> records = record_region.view<Record>().expect("three pages hold whole records");
    CRUCIBLE_FATAL_INVARIANT(records.size() == 3 * page_bytes / sizeof(Record));
}

// One page does not hold a whole number of records of 24 bytes, so the
// mapping gives no View of records.
void test_view_of_a_partial_element_is_none() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
    auto region = map_region(ctx, owner, page_bytes);
    CRUCIBLE_FATAL_INVARIANT(region.view<Record>().is_none());
    CRUCIBLE_FATAL_INVARIANT(region.view<std::uint64_t>().is_some());
}

void test_view_of_an_empty_mapping() {
    EmptyMapping empty_region{};
    fixy::View<std::uint64_t> words = empty_region.view<std::uint64_t>().expect("an empty mapping gives an empty View");
    CRUCIBLE_FATAL_INVARIANT(words.size() == 0);
    int turns = 0;
    for ([[maybe_unused]] std::uint64_t const word : words) {
        ++turns;
    }
    CRUCIBLE_FATAL_INVARIANT(turns == 0);
}

// offset_of gives the position of a window inside its View, for each window
// that lies inside it, and no value for a View of a different mapping and
// for a View that runs past the end.  The case maps two pages under the
// permission of test_windows.
template <class Owner>
void test_offset_of(IoBlockCtx const& ctx, Owner const& owner) {
    auto region = map_region(ctx, owner, page_bytes);
    auto other_region = map_region(ctx, owner, page_bytes);
    fixy::View<std::uint64_t> words = region.template view<std::uint64_t>().expect("a page holds whole words");
    fixy::View<std::uint64_t const> other =
        other_region.template view<std::uint64_t const>().expect("a page holds words");

    for (std::size_t position = 0; position <= words.size(); position += 37) {
        fixy::View<std::uint64_t const> part = words.window(position, 0).expect("an empty window fits");
        CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(words, part).value_or(fixy::dynamic_extent) == position);
    }
    fixy::View<std::uint64_t, 8> fixed = words.window<8>(100).expect("the fixed window fits");
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(words, fixed).value_or(fixy::dynamic_extent) == 100);
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(words, words).value_or(fixy::dynamic_extent) == 0);
    fixy::View<std::uint64_t> tail = words.window(words.size() - 4, 4).expect("the last four words fit");
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(words, tail).value_or(fixy::dynamic_extent) == words.size() - 4);

    // A View of a different mapping, and a View that covers the whole and
    // more, do not lie inside the window.
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(words, other).is_none());
    fixy::View<std::uint64_t> head = words.window(0, 16).expect("sixteen words fit");
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(head, words).is_none());
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(head, tail).is_none());

    // A View of bytes that starts in the middle of a word lies inside the
    // bytes of the page, and the position counts bytes.
    fixy::View<std::uint8_t> bytes = region.template view<std::uint8_t>().expect("a page holds whole bytes");
    fixy::View<std::uint8_t> odd = bytes.window(3, 5).expect("five bytes fit");
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(bytes, odd).value_or(fixy::dynamic_extent) == 3);

    // Two empty Views with no address lie one inside the other.
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(fixy::View<int>{}, fixy::View<int const>{}).value_or(1) == 0);
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(words, fixy::View<std::uint64_t>{}).is_none());

    // The records at byte 8 of the page start between record 0 and record 1
    // of the records at byte 0.  So neither the run nor its first record has
    // a position there.  The case reads the addresses of the records only.
    fixy::View<Record> records = region.template view_at<Record>(0, 100).expect("a page holds 100 records");
    fixy::View<Record> shifted = region.template view_at<Record>(8, 10).expect("ten records fit at byte 8");
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(records, shifted).is_none());
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(records, *shifted.begin()).is_none());
    fixy::View<Record> aligned = region.template view_at<Record>(sizeof(Record), 10).expect("ten records fit");
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(records, aligned) == fixy::some(std::size_t{1}));
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(records, *aligned.begin()) == fixy::some(std::size_t{1}));
}

void test_windows() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
    test_offset_of(ctx, owner);
    auto region = map_region(ctx, owner, page_bytes);
    fixy::View<std::uint64_t> words = region.view<std::uint64_t>().expect("a page holds whole words");
    std::uint64_t position = 0;
    for (std::uint64_t& word : words) {
        word = position++;
    }

    fixy::View<std::uint64_t> middle = words.window(10, 5).expect("the window fits in the page");
    CRUCIBLE_FATAL_INVARIANT(middle.size() == 5);
    std::uint64_t expected = 10;
    for (std::uint64_t const word : middle) {
        CRUCIBLE_FATAL_INVARIANT(word == expected++);
    }
    CRUCIBLE_FATAL_INVARIANT(words.window(0, words.size()).is_some());
    CRUCIBLE_FATAL_INVARIANT(words.window(words.size(), 0).is_some());
    CRUCIBLE_FATAL_INVARIANT(words.window(words.size(), 1).is_none());
    CRUCIBLE_FATAL_INVARIANT(words.window(words.size() + 1, 0).is_none());
    CRUCIBLE_FATAL_INVARIANT(words.window(1, words.size()).is_none());
    CRUCIBLE_FATAL_INVARIANT(words.window(2, fixy::dynamic_extent).is_none());

    fixy::View<std::uint64_t, 8> fixed = words.window<8>(100).expect("the fixed window fits in the page");
    static_assert(fixy::View<std::uint64_t, 8>::extent == 8);
    std::uint64_t fixed_sum = 0;
    for (std::uint64_t const word : fixed) {
        fixed_sum += word;
    }
    CRUCIBLE_FATAL_INVARIANT(fixed_sum == 100 + 101 + 102 + 103 + 104 + 105 + 106 + 107);
    CRUCIBLE_FATAL_INVARIANT(words.window<8>(words.size() - 7).is_none());

    // A fixed window erases to a dynamic View, and a mutable View converts
    // to a View of const elements.
    fixy::View<std::uint64_t const> erased = fixed;
    CRUCIBLE_FATAL_INVARIANT(erased.size() == 8);
}

// The copy of a fixed extent runs in a function that the optimizer does not
// see through, so the copy takes the form of the build and folds no value.
template <class T, std::size_t Count>
[[gnu::noipa]] void copy_fixed_opaque(fixy::View<T, Count> target, fixy::View<T const, Count> source) {
    fixy::copy(target, source);
}

// The byte at a position of the pattern of a run.  Two adjacent positions
// hold different bytes, so a copy with a wrong offset changes a byte.
[[nodiscard]] std::uint8_t pattern_byte(std::size_t position) { return static_cast<std::uint8_t>(position * 7 + 3); }

// A copy of Count bytes between two windows of one run that start shift
// bytes apart.  The source window comes first or the target window comes
// first.  After the copy, the target window holds the bytes that the source
// window held before it, and each other byte keeps its value.
template <std::size_t Count>
void expect_overlapping_copy(fixy::View<std::uint8_t> run, std::size_t shift, bool is_source_first) {
    std::size_t position = 0;
    for (std::uint8_t& byte : run) {
        byte = pattern_byte(position);
        ++position;
    }
    std::size_t const source_at = is_source_first ? 0 : shift;
    std::size_t const target_at = is_source_first ? shift : 0;
    copy_fixed_opaque<std::uint8_t, Count>(run.window<Count>(target_at).expect("the target fits in the run"),
                                           run.window<Count>(source_at).expect("the source fits in the run"));
    position = 0;
    for (std::uint8_t const byte : run) {
        bool const is_in_target = position >= target_at && position < target_at + Count;
        std::size_t const origin = is_in_target ? position - target_at + source_at : position;
        CRUCIBLE_FATAL_INVARIANT(byte == pattern_byte(origin));
        ++position;
    }
}

// The four overlaps of a copy of Count bytes: a shift of one byte and a
// shift of Count - 1 bytes, each with the source first and with the target
// first.
template <std::size_t Count>
void expect_overlapping_copies(fixy::View<std::uint8_t> run) {
    expect_overlapping_copy<Count>(run, 1, true);
    expect_overlapping_copy<Count>(run, 1, false);
    expect_overlapping_copy<Count>(run, Count - 1, true);
    expect_overlapping_copy<Count>(run, Count - 1, false);
}

// A copy of a fixed extent between two windows of one run moves the bytes,
// for each size class of the copy: one word, a first word and a tail word,
// two or more words of the widest width with a tail word, and the memmove
// past 64 bytes.  A record of 24 bytes takes the same check with a shift of
// one record.
template <class Owner>
void expect_overlapping_fixed_copies(IoBlockCtx const& ctx, Owner const& owner) {
    auto byte_region = map_region(ctx, owner, page_bytes);
    fixy::View<std::uint8_t> run = byte_region.template view<std::uint8_t>()
                                       .expect("a page holds whole bytes")
                                       .window(0, 256)
                                       .expect("a page holds 256 bytes");
    expect_overlapping_copies<1>(run);
    expect_overlapping_copies<3>(run);
    expect_overlapping_copies<7>(run);
    expect_overlapping_copies<8>(run);
    expect_overlapping_copies<16>(run);
    expect_overlapping_copies<20>(run);
    expect_overlapping_copies<31>(run);
    expect_overlapping_copies<32>(run);
    expect_overlapping_copies<33>(run);
    expect_overlapping_copies<48>(run);
    expect_overlapping_copies<63>(run);
    expect_overlapping_copies<64>(run);
    expect_overlapping_copies<65>(run);
    expect_overlapping_copies<100>(run);

    auto record_region = map_region(ctx, owner, 3 * page_bytes);
    fixy::View<Record> records = record_region.template view<Record>().expect("three pages hold whole records");
    std::uint64_t key = 0;
    for (Record& record : records.window(0, 3).expect("three records fit")) {
        record = Record{.key = key, .count = static_cast<std::uint32_t>(key + 1), .flags = 7, .extra = key * 5};
        ++key;
    }
    copy_fixed_opaque<Record, 2>(records.window<2>(1).expect("two records fit"),
                                 records.window<2>(0).expect("two records fit"));
    // The records 0 and 1 move to the positions 1 and 2, and the record at
    // position 0 keeps its value.
    std::uint64_t position = 0;
    for (Record const& record : records.window(0, 3).expect("three records fit")) {
        std::uint64_t const expected_key = position == 0 ? 0 : position - 1;
        CRUCIBLE_FATAL_INVARIANT(record.key == expected_key && record.count == expected_key + 1
                                 && record.extra == expected_key * 5);
        ++position;
    }
}

void test_copy() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const source_owner = perm::mint_permission_root<ScratchRegion>();
    auto source_region = map_region(ctx, source_owner, page_bytes);
    auto target_region = map_region(ctx, source_owner, page_bytes);
    fixy::View<std::uint64_t> source = source_region.view<std::uint64_t>().expect("a page holds whole words");
    fixy::View<std::uint64_t> target = target_region.view<std::uint64_t>().expect("a page holds whole words");
    std::uint64_t position = 0;
    for (std::uint64_t& word : source) {
        word = position * 3 + 1;
        ++position;
    }

    CRUCIBLE_FATAL_INVARIANT(fixy::copy(target, source).is_ok());
    CRUCIBLE_FATAL_INVARIANT(hold_same_values<std::uint64_t>(target, source));

    // Two windows of one run can overlap: the copy moves the elements.
    fixy::View<std::uint64_t> head = source.window(0, 100).expect("the head fits");
    fixy::View<std::uint64_t> shifted = source.window(1, 100).expect("the shifted run fits");
    fixy::copy(shifted, head).expect("the two windows have the same count");
    std::uint64_t index = 0;
    for (std::uint64_t const word : source.window(1, 100).expect("the shifted run fits")) {
        CRUCIBLE_FATAL_INVARIANT(word == index * 3 + 1);
        ++index;
    }

    fixy::View<std::uint64_t, 4> fixed_target = target.window<4>(0).expect("four words fit");
    fixy::View<std::uint64_t const, 4> fixed_source = source.window<4>(200).expect("four words fit");
    fixy::copy(fixed_target, fixed_source);
    CRUCIBLE_FATAL_INVARIANT(hold_same_values<std::uint64_t>(fixed_target, fixed_source));

    expect_overlapping_fixed_copies(ctx, source_owner);
}

// The fill runs in a function that the optimizer does not see through.  A
// call with an empty View then gives memset a null pointer at run time.
template <class T>
[[gnu::noipa]] void fill_opaque(fixy::View<T> run, T value) {
    fixy::fill(run, value);
}

// fill writes the value into each element of a window and into no other
// element.  The run is a fresh page of zero bytes.
template <class T>
void expect_fill_of_window(fixy::View<T> run, T background, T value) {
    fill_opaque(run, background);
    fill_opaque(run.window(10, 20).expect("the window fits in the page"), value);
    std::size_t position = 0;
    for (T const& element : run) {
        bool const is_in_window = position >= 10 && position < 30;
        CRUCIBLE_FATAL_INVARIANT(element == (is_in_window ? value : background));
        ++position;
    }
    CRUCIBLE_FATAL_INVARIANT(position == run.size());
}

// The fill of elements of one byte and of a wider element.  A byte, a
// record of one byte, a bool and an enumeration of one byte take the
// memset.  Each element type gets a fresh page, so no element starts its
// lifetime over the bytes of a different type.
void test_fill() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
    auto byte_region = map_region(ctx, owner, page_bytes);
    expect_fill_of_window<std::uint8_t>(byte_region.view<std::uint8_t>().expect("a page holds whole bytes"),
                                        std::uint8_t{0x11}, std::uint8_t{0xA5});
    auto tint_region = map_region(ctx, owner, page_bytes);
    expect_fill_of_window<Tint>(tint_region.view<Tint>().expect("a page holds whole records"), Tint{3}, Tint{200});
    auto flag_region = map_region(ctx, owner, page_bytes);
    fixy::View<bool> flags = flag_region.view<bool>().expect("a page holds whole bools");
    expect_fill_of_window<bool>(flags, false, true);
    expect_fill_of_window<bool>(flags, true, false);
    auto shade_region = map_region(ctx, owner, page_bytes);
    expect_fill_of_window<Shade>(shade_region.view<Shade>().expect("a page holds whole shades"), Shade::light,
                                 Shade::bright);
    auto word_region = map_region(ctx, owner, page_bytes);
    expect_fill_of_window<std::uint32_t>(word_region.view<std::uint32_t>().expect("a page holds whole words"),
                                         std::uint32_t{0x01020304}, std::uint32_t{0xDEADBEEF});
}

// The fill of an empty View writes nothing.  The View holds a null pointer
// and the count 0.
void test_fill_of_an_empty_view() {
    fill_opaque(fixy::View<std::uint8_t>{}, std::uint8_t{0x5A});
    fill_opaque(fixy::View<bool>{}, true);
    fill_opaque(fixy::View<std::uint32_t>{}, std::uint32_t{0x5A});
    EmptyMapping empty_region{};
    fixy::View<std::uint8_t> bytes = empty_region.view<std::uint8_t>().expect("an empty mapping gives an empty View");
    CRUCIBLE_FATAL_INVARIANT(bytes.size() == 0);
    fill_opaque(bytes, std::uint8_t{0x5A});
}

// A copy between two Views of different counts gives LengthMismatch and
// writes nothing.
void test_copy_of_unequal_counts_is_an_error() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
    auto region = map_region(ctx, owner, page_bytes);
    fixy::View<std::uint64_t> words = region.view<std::uint64_t>().expect("a page holds whole words");
    std::uint64_t position = 0;
    for (std::uint64_t& word : words) {
        word = position++;
    }
    auto const outcome =
        fixy::copy(words.window(0, 3).expect("three words fit"), words.window(10, 4).expect("four words fit"));
    CRUCIBLE_FATAL_INVARIANT(outcome.is_err() && outcome.err().is_some());
    std::uint64_t expected = 0;
    for (std::uint64_t const word : words.window(0, 3).expect("three words fit")) {
        CRUCIBLE_FATAL_INVARIANT(word == expected++);
    }
}

// A cursor never leaves its View: a read at the end and a step past the
// end end the process in each build.  Each one runs in a child through
// expect_dies of the Check family.
void test_cursor_at_the_end_aborts() {
    IoBlockCtx const ctx{eff::testing::test()};
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
    auto region = map_region(ctx, owner, page_bytes);
    fixy::View<std::uint64_t> words = region.view<std::uint64_t>().expect("a page holds whole words");
    fixy::View<std::uint64_t> pair = words.window(0, 2).expect("two words fit");
    ::fixy::check::expect_dies(
        test, [&pair] noexcept { static_cast<void>(*pair.end()); },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a View"});
    ::fixy::check::expect_dies(
        test,
        [&pair] noexcept {
            auto cursor = pair.begin();
            ++cursor;
            ++cursor;
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of a View"});
    fixy::View<std::uint64_t> empty = words.window(0, 0).expect("an empty window fits");
    ::fixy::check::expect_dies(
        test, [&empty] noexcept { static_cast<void>(*empty.begin()); },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a View"});
}

// A cursor tells if it is at its end and how many elements follow it, with
// no end cursor of the run.  The count of the run minus the remaining count
// is the position of the cursor.  A FixedArray owns the run, so the test
// needs no mapping.
void test_cursor_position() {
    fixy::FixedArray<std::uint64_t, 16> storage{};
    fixy::View<std::uint64_t const> const words = storage.view();
    fixy::View<std::uint64_t const> run = words.window(3, 5).expect("five words fit");
    auto cursor = run.begin();
    std::size_t position = 0;
    while (!cursor.is_at_end()) {
        CRUCIBLE_FATAL_INVARIANT(cursor.remaining() == run.size() - position);
        CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(run, *cursor) == fixy::some(position));
        ++cursor;
        ++position;
    }
    CRUCIBLE_FATAL_INVARIANT(position == 5 && cursor.remaining() == 0 && cursor == run.end());
    CRUCIBLE_FATAL_INVARIANT(run.end().is_at_end() && run.end().remaining() == 0);
    fixy::View<std::uint64_t> const empty{};
    CRUCIBLE_FATAL_INVARIANT(empty.begin().is_at_end() && empty.begin().remaining() == 0);
}

// The position of an element of a View is the inverse of get.  An element
// before the first one, after the last one or in a different run is not an
// element of the View.  Two FixedArrays own the two runs.
void test_offset_of_an_element() {
    fixy::FixedArray<Record, 100> first_storage{};
    fixy::FixedArray<Record, 20> second_storage{};
    fixy::View<Record const> const records = first_storage.view();
    fixy::View<Record const> middle = records.window(10, 20).expect("twenty records fit");
    for (std::size_t position = 0; position < middle.size(); ++position) {
        Record const& element = *middle.get(position).expect("the position is below the count");
        CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(middle, element) == fixy::some(position));
        CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(records, element) == fixy::some(position + 10));
    }
    Record const& before = *records.get(9).expect("record 9 fits");
    Record const& after = *records.get(30).expect("record 30 fits");
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(middle, before).is_none());
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(middle, after).is_none());
    fixy::View<Record const> const other = second_storage.view();
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(middle, *other.begin()).is_none());
    fixy::View<Record> const empty{};
    CRUCIBLE_FATAL_INVARIANT(fixy::offset_of(empty, *middle.begin()).is_none());
}

// A constant evaluation reads no address as a word, so offset_of compares the
// first address of the part with the address of each position of the whole
// there, and it gives the same answers: for an element, for a run, for an
// empty run at the end and for a run past the end.
consteval bool offset_of_agrees_in_constant_evaluation() {
    fixy::FixedArray<std::uint32_t, 6> numbers{};
    fixy::View<std::uint32_t const> const view = numbers.view();
    fixy::View<std::uint32_t const> const tail = view.window(2, 4).expect("four numbers fit");
    bool is_each_position_found = true;
    std::size_t position = 0;
    for (std::uint32_t const& number : view) {
        is_each_position_found = is_each_position_found && fixy::offset_of(view, number) == fixy::some(position);
        ++position;
    }
    fixy::View<std::uint32_t const> const end = view.window(6, 0).expect("an empty window fits at the end");
    return is_each_position_found && fixy::offset_of(tail, *view.begin()).is_none()
        && fixy::offset_of(tail, *tail.begin()) == fixy::some(std::size_t{0})
        && fixy::offset_of(view, tail) == fixy::some(std::size_t{2}) && fixy::offset_of(tail, view).is_none()
        && fixy::offset_of(view, end) == fixy::some(std::size_t{6})
        && fixy::offset_of(tail, end) == fixy::some(std::size_t{4});
}
static_assert(offset_of_agrees_in_constant_evaluation());

// The property: for random windows of two pages, a window is present
// exactly when it fits, it starts at its offset, and copy then gives
// equal runs.  A plain loop over the positions is the model.
void test_agrees_with_plain_model() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
    auto first_region = map_region(ctx, owner, page_bytes);
    auto second_region = map_region(ctx, owner, page_bytes);
    fixy::View<std::uint64_t> first = first_region.view<std::uint64_t>().expect("a page holds whole words");
    fixy::View<std::uint64_t> second = second_region.view<std::uint64_t>().expect("a page holds whole words");
    std::uint64_t const base = address_of_first(first);
    ::crucible::test::PhiloxStream stream{0x5EED0E610000D001u};
    for (std::uint64_t& word : first) {
        word = stream.next_wide();
    }
    for (int trial = 0; trial < 4096; ++trial) {
        std::size_t const offset = stream.below(first.size() + 8);
        std::size_t const count = stream.below(first.size() + 8);
        bool const model_fits = offset <= first.size() && count <= first.size() - offset;
        auto window = first.window(offset, count);
        CRUCIBLE_FATAL_INVARIANT(window.is_some() == model_fits);
        for (fixy::View<std::uint64_t> const& run : window) {
            CRUCIBLE_FATAL_INVARIANT(run.size() == count);
            if (count != 0) {
                CRUCIBLE_FATAL_INVARIANT(address_of_first(run) == base + offset * sizeof(std::uint64_t));
            }
            fixy::View<std::uint64_t> target = second.window(0, count).expect("the target holds the run");
            fixy::copy(target, run).expect("the target has the count of the run");
            CRUCIBLE_FATAL_INVARIANT(hold_same_values<std::uint64_t>(target, run));
        }
    }
}

}  // namespace

int main() {
    test_view_of_a_mapping();
    test_view_of_a_partial_element_is_none();
    test_view_of_an_empty_mapping();
    test_windows();
    test_copy();
    test_fill();
    test_fill_of_an_empty_view();
    test_copy_of_unequal_counts_is_an_error();
    test_cursor_at_the_end_aborts();
    test_cursor_position();
    test_offset_of_an_element();
    test_agrees_with_plain_model();
    return 0;
}
