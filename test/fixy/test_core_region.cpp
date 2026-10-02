// Tests of foundation/core/Region.h and of OwnedMmap::view: the View of a
// mapping, its windows, copy and fill.
//
// The storage of each View is an anonymous mapping, because a mapping is
// the owner that gives a View in this tree.  The mint of a mapping gives
// a std::expected, so map_region holds one, and the report of the
// quarantine plugin names it there and nowhere else.

#include <fixy/Core.h>
#include <fixy/os/Mmap.h>
#include <foundation/Platform.h>

#include "../foundation/abort_probe.h"
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

template <class Owner>
[[nodiscard]] auto map_region(IoBlockCtx const& ctx, Owner const& owner, std::size_t bytes) {
    auto mapped = fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, owner, bytes);
    CRUCIBLE_FATAL_INVARIANT(mapped);
    return std::move(*mapped).consume();
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

void test_windows() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
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
// end end the process in each build.
void test_cursor_at_the_end_aborts() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
    auto region = map_region(ctx, owner, page_bytes);
    fixy::View<std::uint64_t> words = region.view<std::uint64_t>().expect("a page holds whole words");
    fixy::View<std::uint64_t> pair = words.window(0, 2).expect("two words fit");
    bool const end_read_aborts = ::foundation::test::aborts([&pair] { static_cast<void>(*pair.end()); });
    bool const past_end_step_aborts = ::foundation::test::aborts([&pair] {
        auto cursor = pair.begin();
        ++cursor;
        ++cursor;
        ++cursor;
    });
    fixy::View<std::uint64_t> empty = words.window(0, 0).expect("an empty window fits");
    bool const empty_read_aborts = ::foundation::test::aborts([&empty] { static_cast<void>(*empty.begin()); });
    CRUCIBLE_FATAL_INVARIANT(end_read_aborts && past_end_step_aborts && empty_read_aborts);
}

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
    ::foundation::test::PhiloxStream stream{0x5EED0E610000D001u};
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
    test_copy_of_unequal_counts_is_an_error();
    test_cursor_at_the_end_aborts();
    test_agrees_with_plain_model();
    return 0;
}
