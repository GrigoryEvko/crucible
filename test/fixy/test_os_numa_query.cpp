// Tests of the two NUMA queries of fixy/os: the census of the node of each
// page of a range (NumaPlacement.h), and the allocation counters of a node
// (NumaCounters.h), run against the kernel.
//
// The storage of each range is an anonymous mapping, because a mapping is
// the owner that gives a View in this tree.  The mint of a mapping gives a
// std::expected, so map_pages holds one, as map_region of
// test/fixy/test_core_region.cpp does.

#include <fixy/Core.h>
#include <fixy/os/Topology.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/NumaCounters.h>
#include <fixy/os/NumaPlacement.h>
#include <foundation/Platform.h>

#include "../test_assert.h"

#include <cstddef>
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

template <class Owner>
[[nodiscard]] auto map_pages(IoBlockCtx const& ctx, Owner const& owner, std::size_t bytes) {
    auto mapped = fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, owner, bytes);
    CRUCIBLE_FATAL_INVARIANT(mapped);
    return std::move(*mapped).consume();
}

[[nodiscard]] std::size_t page_bytes() noexcept { return fixy::concurrent::Topology::instance().page_size_bytes(); }

// Writes one byte in each page of the View.
void touch_each_page(fixy::View<std::byte> bytes) {
    std::size_t position = 0;
    for (std::byte& value : bytes) {
        if (position % page_bytes() == 0) value = std::byte{1};
        ++position;
    }
}

// The sum of the pages on each concrete node of the census.
[[nodiscard]] std::size_t pages_on_each_node(fixy::numa::PageNodeCensus const& census) {
    std::size_t total = 0;
    for (int index = 0; index < 254; ++index) {
        total += census.pages_on(fixy::numa::concrete_numa_node(index).expect("an index below 254 names a node"));
    }
    return total;
}

// Each page that this thread wrote has a node, the counts of the nodes add
// up to the pages, and the node of most pages holds each of them.
void test_census_of_a_written_range() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
    auto region = map_pages(ctx, owner, 4 * page_bytes());
    fixy::View<std::byte> bytes = region.view<std::byte>().expect("a mapping holds bytes");
    touch_each_page(bytes);

    fixy::numa::PageNodeCensus const census =
        fixy::numa::census_page_nodes(ctx, bytes).expect("the kernel gives the node of each page");
    assert(census.pages() == 4);
    assert(census.pages_without_node() == 0);
    assert(pages_on_each_node(census) == 4);
    std::size_t largest = 0;
    for (int index = 0; index < 254; ++index) {
        fixy::NumaNodeId const node = fixy::numa::concrete_numa_node(index).expect("an index below 254 names a node");
        assert(census.pages_on(node) + census.pages_off(node) == 4);
        if (census.pages_on(node) > largest) largest = census.pages_on(node);
    }
    assert(largest > 0);
    assert(census.pages_on(fixy::NumaNodeId::None) == 0 && census.pages_on(fixy::NumaNodeId::Any) == 0);
    crucible::test::pass("  test_census_of_a_written_range: PASSED\n");
}

// A page that nothing touched has no frame, and a page that the range only
// read maps the shared zero page.  Neither has a node.
void test_census_of_untouched_and_read_ranges() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<ScratchRegion>();
    auto untouched_region = map_pages(ctx, owner, 3 * page_bytes());
    fixy::View<std::byte const> untouched = untouched_region.view<std::byte const>().expect("a mapping holds bytes");
    fixy::numa::PageNodeCensus const untouched_census =
        fixy::numa::census_page_nodes(ctx, untouched).expect("the kernel gives the node of each page");
    assert(untouched_census.pages() == 3 && untouched_census.pages_without_node() == 3);
    assert(!untouched_census.is_all_on(fixy::numa::concrete_numa_node(0).expect("node 0 has an id")));

    auto read_region = map_pages(ctx, owner, 2 * page_bytes());
    fixy::View<std::byte const> read_bytes = read_region.view<std::byte const>().expect("a mapping holds bytes");
    std::size_t nonzero_bytes = 0;
    for (std::byte const value : read_bytes) {
        if (value != std::byte{0}) ++nonzero_bytes;
    }
    assert(nonzero_bytes == 0);
    fixy::numa::PageNodeCensus const read_census =
        fixy::numa::census_page_nodes(ctx, read_bytes).expect("the kernel gives the node of each page");
    assert(read_census.pages() == 2 && read_census.pages_without_node() == 2);
    crucible::test::pass("  test_census_of_untouched_and_read_ranges: PASSED\n");
}

// An empty range has no page.  A range that is not page-aligned counts each
// page that holds one of its bytes.
void test_census_of_empty_and_unaligned_ranges() {
    IoBlockCtx const ctx{eff::testing::test()};
    fixy::numa::PageNodeCensus const empty =
        fixy::numa::census_page_nodes(ctx, fixy::View<std::byte const>{}).expect("an empty range needs no query");
    assert(empty.pages() == 0 && empty.pages_without_node() == 0);
    assert(!empty.is_all_on(fixy::numa::concrete_numa_node(0).expect("node 0 has an id")));

    auto const owner = perm::mint_permission_root<ScratchRegion>();
    auto region = map_pages(ctx, owner, 3 * page_bytes());
    fixy::View<std::byte> bytes = region.view<std::byte>().expect("a mapping holds bytes");
    touch_each_page(bytes);
    std::size_t const page = page_bytes();
    auto census_of = [&ctx, &bytes](std::size_t offset, std::size_t count) {
        return fixy::numa::census_page_nodes(ctx, bytes.window(offset, count).expect("the window fits"))
            .expect("the kernel gives the node of each page")
            .pages();
    };
    assert(census_of(100, page) == 2);
    assert(census_of(page, 1) == 1);
    assert(census_of(page - 1, 2) == 2);
    assert(census_of(page - 1, page + 2) == 3);
    assert(census_of(0, 3 * page) == 3);
    assert(census_of(2 * page, 0) == 0);
    crucible::test::pass("  test_census_of_empty_and_unaligned_ranges: PASSED\n");
}

// The counters of a node of this host are readable, and no counter goes
// back between two reads.
void test_counters_of_a_node_of_this_host() {
    IoBlockCtx const ctx{eff::testing::test()};
    int const first_node = fixy::concurrent::Topology::instance().numa_node_of_cpu(0).value_or(0);
    fixy::NumaNodeId const node = fixy::numa::concrete_numa_node(first_node).expect("the topology names a node");
    auto first_read = fixy::numa::read_allocation_counters(ctx, node);
    if (first_read.is_err()) {
        fixy::numa::NumaQueryError const error = first_read.err().expect("the read failed");
        assert(error.step == fixy::numa::NumaQueryStep::CounterRead);
        crucible::test::pass("  test_counters_of_a_node_of_this_host: SKIPPED (the kernel gives no numastat file)\n");
        return;
    }
    fixy::numa::NodeAllocationCounters const before = std::move(first_read).expect("the read succeeded");
    fixy::numa::NodeAllocationCounters const after =
        fixy::numa::read_allocation_counters(ctx, node).expect("a second read of the same file succeeds");
    assert(before.hit + before.miss > 0);
    assert(after.hit >= before.hit && after.miss >= before.miss && after.foreign >= before.foreign);
    assert(after.interleave_hit >= before.interleave_hit && after.local >= before.local && after.other >= before.other);
    crucible::test::pass("  test_counters_of_a_node_of_this_host: PASSED\n");
}

// A sentinel names no counter file, and a concrete node that the host lacks
// has no file to open.
void test_counters_of_nodes_with_no_file() {
    IoBlockCtx const ctx{eff::testing::test()};
    fixy::numa::NumaQueryError const none_error =
        fixy::numa::read_allocation_counters(ctx, fixy::NumaNodeId::None).err().expect("None has no counters");
    fixy::numa::NumaQueryError const any_error =
        fixy::numa::read_allocation_counters(ctx, fixy::NumaNodeId::Any).err().expect("Any has no counters");
    assert(none_error.step == fixy::numa::NumaQueryStep::NodeNotConcrete && none_error.error_number == 0);
    assert(any_error.step == fixy::numa::NumaQueryStep::NodeNotConcrete && any_error.error_number == 0);
    fixy::numa::NumaQueryError const missing =
        fixy::numa::read_allocation_counters(ctx, fixy::numa::concrete_numa_node(253).expect("node 253 has an id"))
            .err()
            .expect("no shipped host has node 253");
    assert(missing.step == fixy::numa::NumaQueryStep::CounterRead && missing.error_number != 0);
    crucible::test::pass("  test_counters_of_nodes_with_no_file: PASSED\n");
}

// The parse takes the six counters in any order, skips a line with another
// name, and refuses a text that lacks a counter or breaks a line.
void test_parse_of_counter_texts() {
    fixy::numa::NodeAllocationCounters const counters =
        fixy::numa::parse_allocation_counters("numa_hit 1\nnuma_miss 2\nnuma_foreign 3\ninterleave_hit 4\n"
                                              "local_node 5\nother_node 6\n")
            .expect("a full text parses");
    assert(counters.hit == 1 && counters.miss == 2 && counters.foreign == 3);
    assert(counters.interleave_hit == 4 && counters.local == 5 && counters.other == 6);

    fixy::numa::NodeAllocationCounters const reordered =
        fixy::numa::parse_allocation_counters("other_node 6\nnew_counter 9\nlocal_node 5\n\ninterleave_hit 4\n"
                                              "numa_foreign 18446744073709551615\nnuma_miss 2\nnuma_hit 1")
            .expect("a text with another counter, an empty line and no final line break parses");
    assert(reordered.foreign == 18446744073709551615u && reordered.other == 6);

    auto const is_format_error =
        [](fixy::Result<fixy::numa::NodeAllocationCounters, fixy::numa::NumaQueryError> const& parsed) {
            return parsed.err().match(
                [](fixy::numa::NumaQueryError const& error) {
                    return error.step == fixy::numa::NumaQueryStep::CounterFormat && error.error_number == 0;
                },
                [] { return false; });
        };
    assert(is_format_error(fixy::numa::parse_allocation_counters("")));
    assert(is_format_error(fixy::numa::parse_allocation_counters(
        "numa_hit 1\nnuma_miss 2\nnuma_foreign 3\ninterleave_hit 4\nlocal_node 5\n")));
    assert(is_format_error(fixy::numa::parse_allocation_counters(
        "numa_hit 1\nnuma_hit 1\nnuma_miss 2\nnuma_foreign 3\ninterleave_hit 4\nlocal_node 5\nother_node 6\n")));
    assert(is_format_error(fixy::numa::parse_allocation_counters(
        "numa_hit 18446744073709551616\nnuma_miss 2\nnuma_foreign 3\ninterleave_hit 4\nlocal_node 5\nother_node 6\n")));
    assert(is_format_error(fixy::numa::parse_allocation_counters(
        "numa_hit\nnuma_miss 2\nnuma_foreign 3\ninterleave_hit 4\nlocal_node 5\nother_node 6\n")));
    assert(is_format_error(fixy::numa::parse_allocation_counters(
        "numa_hit 1 2\nnuma_miss 2\nnuma_foreign 3\ninterleave_hit 4\nlocal_node 5\nother_node 6\n")));
    crucible::test::pass("  test_parse_of_counter_texts: PASSED\n");
}

}  // namespace

int main() {
    crucible::test::pass("test_os_numa_query:\n");
    test_census_of_a_written_range();
    test_census_of_untouched_and_read_ranges();
    test_census_of_empty_and_unaligned_ranges();
    test_counters_of_a_node_of_this_host();
    test_counters_of_nodes_with_no_file();
    test_parse_of_counter_texts();
    crucible::test::pass("test_os_numa_query: 6 groups, all passed\n");
    return 0;
}
