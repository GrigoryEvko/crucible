// The page source door of foundation/os/PageSource.h.
//
// The reservation: a count of bytes rounds up to whole pages, a reservation
// of a huge page starts at a huge page, a count of zero and a count above a
// cap are refused, and the ranges of a reservation lie inside it.
//
// The calls: a committed range reads zero, a decommit with MADV_DONTNEED
// makes it read zero again, a populate and a collapse change no byte, a
// process that refuses THP gets the base pages of a collapse, a bind to a
// node that the host does not have gives the unknown placement, and on a
// host with two nodes a bind to node 1 puts the first page on node 1, or
// numa_foreign of node 1 counts the page that went to another node.  A grow
// keeps each byte and the alignment of a huge page, and refuses a smaller
// count and a reservation of two kinds of access.
//
// The children: a read of a reserved page that no commit opened faults, a
// range of a different reservation ends the process, and a range of the
// place that a grow left ends the process.

#include <foundation/os/PageSource.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/NumaCounters.h>
#include <fixy/os/Pages.h>
#include <fixy/os/Process.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Auxiliary.h>
#include <foundation/os/Errno.h>
#include <foundation/os/File.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace os = ::foundation::os;
namespace fp = ::fixy::process;
namespace pages = ::fixy::pages;

using ::fixy::ByteCount;

constexpr ::fixy::InitLoadCtx kStartup{::foundation::effects::testing::init()};

constexpr std::size_t kMebibyte = std::size_t{1} << 20;

// The signal of a fault, SIGSEGV.
constexpr int kFaultSignal = 11;

[[nodiscard]] std::size_t host_page_bytes() noexcept {
    return os::read_auxiliary(os::AuxiliaryEntry::page_size).value_or(0);
}

[[nodiscard]] os::RangeStart start_at(std::size_t offset) noexcept {
    return ::fixy::side_as<os::side::RangeStart>(ByteCount{offset});
}

[[nodiscard]] os::Reservation reserve_or_end(ByteCount bytes, os::ReserveAlignment alignment) noexcept {
    return os::reserve(bytes, alignment).expect("the host gives a reservation of some MiB");
}

[[nodiscard]] os::CommittedRange commit_whole(os::Reservation& reservation) noexcept {
    return reservation.commit(reservation.whole()).expect("the host commits the pages of a reservation");
}

// True when each byte of the View reads zero.  Complexity: linear in the
// bytes.
[[nodiscard]] bool reads_zero(::fixy::View<std::uint8_t> bytes) noexcept {
    for (std::uint8_t const byte : bytes) {
        if (byte != 0) return false;
    }
    return true;
}

// Writes value into the byte at the start of each page of the View.
// Complexity: one write for each page.
void write_each_page(::fixy::View<std::uint8_t> bytes, std::uint8_t value) noexcept {
    std::size_t const page = host_page_bytes();
    for (std::size_t offset = 0; offset < bytes.size(); offset += page)
        *bytes.get(offset).expect("an offset below the size lies in the View") = value;
}

// True when the byte at the start of each page of the View holds value.
[[nodiscard]] bool holds_each_page(::fixy::View<std::uint8_t> bytes, std::uint8_t value) noexcept {
    std::size_t const page = host_page_bytes();
    for (std::size_t offset = 0; offset < bytes.size(); offset += page) {
        if (*bytes.get(offset).expect("an offset below the size lies in the View") != value) return false;
    }
    return true;
}

void test_reserve_rounds_to_whole_pages(::fixy::TestRunnerCtx const& test) {
    std::size_t const page = host_page_bytes();
    CRUCIBLE_TEST_ASSERT(test, page != 0);
    os::Reservation reservation = reserve_or_end(ByteCount{3 * page + 1}, os::ReserveAlignment::page);
    CRUCIBLE_TEST_ASSERT(test, reservation.size() == ByteCount{4 * page});
    CRUCIBLE_TEST_ASSERT(test, reservation.page_size() == ByteCount{page});
    CRUCIBLE_TEST_ASSERT(test, reservation.address_of(reservation.whole()) % page == 0);
    ::fixy::check::pass(test, "a reserve rounds its bytes up to whole pages\n");
}

void test_reserve_at_a_huge_page(::fixy::TestRunnerCtx const& test) {
    os::Reservation reservation = reserve_or_end(ByteCount{6 * kMebibyte}, os::ReserveAlignment::huge_page);
    CRUCIBLE_TEST_ASSERT(test, reservation.address_of(reservation.whole()) % os::kPageSourceHugePageBytes == 0);
    CRUCIBLE_TEST_ASSERT(test, reservation.size() == ByteCount{6 * kMebibyte});
    ::fixy::check::pass(test, "a reserve of the alignment of a huge page starts at a huge page\n");
}

void test_reserve_refusals(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, os::reserve(ByteCount{0}, os::ReserveAlignment::page).err().value_or(os::Errno::eio)
                                   == os::Errno::einval);
    CRUCIBLE_TEST_ASSERT(test,
                         os::reserve(ByteCount{2 * kMebibyte}, os::ReserveAlignment::page, os::ReserveCap{kMebibyte})
                                 .err()
                                 .value_or(os::Errno::eio)
                             == os::Errno::enomem);
    CRUCIBLE_TEST_ASSERT(
        test, os::reserve(ByteCount{kMebibyte}, os::ReserveAlignment::page, os::ReserveCap{kMebibyte}).is_ok());
    // A count that no rounding to whole pages holds.
    CRUCIBLE_TEST_ASSERT(
        test, os::reserve(ByteCount{~std::size_t{0}}, os::ReserveAlignment::page).err().value_or(os::Errno::eio)
                  == os::Errno::enomem);
    ::fixy::check::pass(test, "a reserve of zero bytes, past the cap or past the address space is refused\n");
}

void test_ranges_lie_inside(::fixy::TestRunnerCtx const& test) {
    std::size_t const page = host_page_bytes();
    os::Reservation reservation = reserve_or_end(ByteCount{8 * page}, os::ReserveAlignment::page);
    CRUCIBLE_TEST_ASSERT(test, reservation.range(start_at(page), ByteCount{2 * page}).is_some());
    CRUCIBLE_TEST_ASSERT(test, reservation.range(start_at(6 * page), ByteCount{2 * page}).is_some());
    CRUCIBLE_TEST_ASSERT(test, reservation.range(start_at(7 * page), ByteCount{2 * page}).is_none());
    CRUCIBLE_TEST_ASSERT(test, reservation.range(start_at(1), ByteCount{page}).is_none());
    CRUCIBLE_TEST_ASSERT(test, reservation.range(start_at(0), ByteCount{page + 1}).is_none());
    CRUCIBLE_TEST_ASSERT(test, reservation.range(start_at(0), ByteCount{0}).is_none());
    CRUCIBLE_TEST_ASSERT(test, reservation.range(start_at(9 * page), ByteCount{page}).is_none());
    CRUCIBLE_TEST_ASSERT(test, reservation.whole().size() == ByteCount{8 * page});
    os::ReservedRange const part =
        reservation.range(start_at(3 * page), ByteCount{page}).expect("a page inside the reservation is a range");
    os::RangeStart const part_start = part.start();
    CRUCIBLE_TEST_ASSERT(test, part_start.value() == ByteCount{3 * page});
    CRUCIBLE_TEST_ASSERT(test, reservation.address_of(part) == reservation.address_of(reservation.whole()) + 3 * page);
    ::fixy::check::pass(test, "a range of whole pages inside the reservation is a range, and each other part is not\n");
}

void test_commit_and_decommit(::fixy::TestRunnerCtx const& test) {
    os::Reservation reservation = reserve_or_end(ByteCount{kMebibyte}, os::ReserveAlignment::page);
    os::CommittedRange const committed = commit_whole(reservation);
    ::fixy::View<std::uint8_t> const bytes = reservation.bytes_of(committed);
    CRUCIBLE_TEST_ASSERT(test, bytes.size() == kMebibyte);
    CRUCIBLE_TEST_ASSERT(test, reads_zero(bytes));
    write_each_page(bytes, 0x5a);
    CRUCIBLE_TEST_ASSERT(test, holds_each_page(bytes, 0x5a));
    CRUCIBLE_TEST_ASSERT(test, reservation.decommit(committed, os::PurgeKind::dont_need) == os::PurgeKind::dont_need);
    CRUCIBLE_TEST_ASSERT(test, reads_zero(bytes));
    write_each_page(bytes, 0x3c);
    // The floor of the base is 5.6, and MADV_FREE came in 4.5.
    CRUCIBLE_TEST_ASSERT(test, reservation.decommit(committed, os::PurgeKind::lazy_free) == os::PurgeKind::lazy_free);
    ::fixy::check::pass(test, "a committed range reads zero, and reads zero again after MADV_DONTNEED\n");
}

void test_populate_changes_no_byte(::fixy::TestRunnerCtx const& test) {
    os::Reservation reservation = reserve_or_end(ByteCount{kMebibyte}, os::ReserveAlignment::page);
    os::CommittedRange const committed = commit_whole(reservation);
    ::fixy::View<std::uint8_t> const bytes = reservation.bytes_of(committed);
    *bytes.get(0).expect("the first byte lies in the range") = 0x11;
    os::PopulateKind const kind = reservation.populate(committed);
    CRUCIBLE_TEST_ASSERT(test, kind != os::PopulateKind::deferred);
    CRUCIBLE_TEST_ASSERT(test, *bytes.get(0).expect("the first byte lies in the range") == 0x11);
    CRUCIBLE_TEST_ASSERT(test, *bytes.get(kMebibyte - 1).expect("the last byte lies in the range") == 0);
    ::fixy::check::pass(test, "a populate faults in the pages and changes no byte\n");
}

void test_huge_pages_change_no_byte(::fixy::TestRunnerCtx const& test) {
    os::Reservation reservation = reserve_or_end(ByteCount{4 * kMebibyte}, os::ReserveAlignment::huge_page);
    os::HugePageAdvice const advice = reservation.advise_huge(reservation.whole());
    CRUCIBLE_TEST_ASSERT(test, advice == os::HugePageAdvice::advised || advice == os::HugePageAdvice::refused);
    os::CommittedRange const committed = commit_whole(reservation);
    ::fixy::View<std::uint8_t> const bytes = reservation.bytes_of(committed);
    write_each_page(bytes, 0x77);
    os::PageKind const kind = reservation.collapse(committed);
    CRUCIBLE_TEST_ASSERT(test, kind == os::PageKind::huge || kind == os::PageKind::base);
    CRUCIBLE_TEST_ASSERT(test, holds_each_page(bytes, 0x77));
    // A range of one page holds no whole huge page, so it gets the base
    // pages with no call.
    os::CommittedRange const one_page =
        reservation.commit(reservation.range(start_at(0), reservation.page_size()).expect("the first page is a range"))
            .expect("a committed page commits again");
    CRUCIBLE_TEST_ASSERT(test, reservation.collapse(one_page) == os::PageKind::base);
    ::fixy::check::pass(test, "the huge page advice and a collapse change no byte\n");
}

// A process that refuses THP gets einval for each collapse, and the door
// then gives the base pages, the fallback of a kernel below 6.1.
void test_refused_collapse_gives_base_pages(::fixy::TestRunnerCtx const& test) {
    pages::ThpSetting const first =
        pages::exchange_thp_setting(kStartup, pages::ThpSetting::refused).expect("the process refuses THP");
    os::Reservation reservation = reserve_or_end(ByteCount{4 * kMebibyte}, os::ReserveAlignment::huge_page);
    os::CommittedRange const committed = commit_whole(reservation);
    write_each_page(reservation.bytes_of(committed), 0x42);
    CRUCIBLE_TEST_ASSERT(test, reservation.collapse(committed) == os::PageKind::base);
    CRUCIBLE_TEST_ASSERT(test, holds_each_page(reservation.bytes_of(committed), 0x42));
    static_cast<void>(pages::exchange_thp_setting(kStartup, first).expect("the process puts back its THP setting"));
    ::fixy::check::pass(test, "a refused collapse gives the base pages\n");
}

void test_bind_falls_back(::fixy::TestRunnerCtx const& test) {
    os::Reservation reservation = reserve_or_end(ByteCount{2 * kMebibyte}, os::ReserveAlignment::page);
    os::ReservedRange const whole = reservation.whole();
    CRUCIBLE_TEST_ASSERT(test, reservation.bind(whole, os::Placement::unknown()) == os::Placement::unknown());
    // No host of the tree has 63 nodes, so the kernel refuses the node, and
    // a node past the set issues no call.
    CRUCIBLE_TEST_ASSERT(test, reservation.bind(whole, os::Placement::on_node(os::NumaNodeId{62}))
                                   == os::Placement::unknown());
    CRUCIBLE_TEST_ASSERT(test, reservation.bind(whole, os::Placement::on_node(os::NumaNodeId{64}))
                                   == os::Placement::unknown());
    // Node 0 is on each host.  A kernel with no NUMA, as qemu user mode can
    // be, refuses each bind, and the door then gives the unknown placement.
    os::Placement const first = os::Placement::on_node(os::NumaNodeId{0});
    os::Placement const got = reservation.bind(whole, first);
    CRUCIBLE_TEST_ASSERT(test, got == first || got == os::Placement::unknown());
    ::fixy::check::pass(test, "a bind to a node that the host does not have keeps the policy of the thread\n");
}

// The counters of node 1 (fixy/os/NumaCounters.h).
[[nodiscard]] ::fixy::Option<::fixy::numa::NodeAllocationCounters>
counters_of_node_one(::fixy::TestRunnerCtx const& test) noexcept {
    return ::fixy::numa::read_allocation_counters(test, ::fixy::NumaNodeId{1}).ok();
}

// The growth of numa_hit and numa_foreign of node 1 over the write of a range.
struct NodeOneGrowth {
    std::uint64_t hit = 0;
    std::uint64_t foreign = 0;
};

// What the bind case saw: the node of the first page, the count of the pages
// of the write, and the growth of the counters of node 1, which has no value
// when a read of the counters failed.  An Id has no empty value, so the node
// starts at node 0, and each observation names its node.
struct FirstPageSeen {
    os::NumaNodeId node{0};
    std::uint64_t pages = 0;
    ::fixy::Option<NodeOneGrowth> growth{};
};

// The verdict on the first page of a range that a bind sent to node 1.
enum class FirstPageVerdict : std::uint8_t {
    // The first page is on node 1.
    on_node_one,
    // The first page is on another node, and numa_foreign of node 1 counts a
    // page that went to another node.
    fell_back,
    // The first page is on another node, and numa_foreign of node 1 counts no
    // such page.
    misplaced,
    // The first page is on another node, and the counters of node 1 did not
    // count the pages of the write, as a kernel with vm.numa_stat at 0 does.
    uncounted,
};

// Each page of the write counts in numa_hit or numa_foreign of node 1.  So a
// first page off node 1 is a fallback of the kernel only when numa_foreign
// grew.
[[nodiscard]] FirstPageVerdict judge_first_page(FirstPageSeen const& seen) noexcept {
    if (seen.node == os::NumaNodeId{1}) return FirstPageVerdict::on_node_one;
    return seen.growth.match(
        [&seen](NodeOneGrowth const& growth) noexcept {
            if (growth.hit + growth.foreign < seen.pages) return FirstPageVerdict::uncounted;
            return growth.foreign != 0 ? FirstPageVerdict::fell_back : FirstPageVerdict::misplaced;
        },
        []() noexcept { return FirstPageVerdict::uncounted; });
}

// The verdict through planted observations: a first page off node 1 passes
// only with a counted fallback.  The second observation is the one that the
// old case refused at its assert of node 1: the page on node 0, and
// numa_foreign of node 1 one higher.
void test_first_page_verdict(::fixy::TestRunnerCtx const& test) {
    constexpr std::uint64_t kPages = 512;
    ::fixy::Option<NodeOneGrowth> const counted_fallback =
        ::fixy::Option<NodeOneGrowth>::some(NodeOneGrowth{.hit = kPages - 1, .foreign = 1});
    ::fixy::Option<NodeOneGrowth> const no_fallback =
        ::fixy::Option<NodeOneGrowth>::some(NodeOneGrowth{.hit = kPages, .foreign = 0});
    ::fixy::Option<NodeOneGrowth> const nothing_counted = ::fixy::Option<NodeOneGrowth>::some(NodeOneGrowth{});
    CRUCIBLE_TEST_ASSERT(test, judge_first_page({.node = os::NumaNodeId{1}, .pages = kPages})
                                   == FirstPageVerdict::on_node_one);
    CRUCIBLE_TEST_ASSERT(test,
                         judge_first_page({.node = os::NumaNodeId{0}, .pages = kPages, .growth = counted_fallback})
                             == FirstPageVerdict::fell_back);
    CRUCIBLE_TEST_ASSERT(test, judge_first_page({.node = os::NumaNodeId{0}, .pages = kPages, .growth = no_fallback})
                                   == FirstPageVerdict::misplaced);
    CRUCIBLE_TEST_ASSERT(test, judge_first_page({.node = os::NumaNodeId{0}, .pages = kPages, .growth = nothing_counted})
                                   == FirstPageVerdict::uncounted);
    CRUCIBLE_TEST_ASSERT(test,
                         judge_first_page({.node = os::NumaNodeId{0}, .pages = kPages}) == FirstPageVerdict::uncounted);
    ::fixy::check::pass(test, "the first page off node 1 passes only with a counted fallback\n");
}

// On a host with two nodes, a bind to node 1 before the first touch sends each
// page to node 1.  The bind of one node is MPOL_PREFERRED, so the kernel gives
// a page to another node when node 1 has fewer free pages than its low
// watermark, and it counts that page in numa_foreign of node 1.  The free
// pages of a node are a fact of the whole host, so the first page is on node 1,
// or numa_foreign of node 1 grew over the write (judge_first_page).  A host
// with one node refuses the bind, and the test skips.
void test_bind_places_the_pages(::fixy::TestRunnerCtx const& test) {
    os::Reservation reservation = reserve_or_end(ByteCount{2 * kMebibyte}, os::ReserveAlignment::page);
    os::Placement const second = os::Placement::on_node(os::NumaNodeId{1});
    if (reservation.bind(reservation.whole(), second) != second) {
        ::fixy::check::pass(test, "  [skipped] the host has one node\n");
        return;
    }
    os::CommittedRange const committed = commit_whole(reservation);
    ::fixy::Option<::fixy::numa::NodeAllocationCounters> const before = counters_of_node_one(test);
    write_each_page(reservation.bytes_of(committed), 0x01);
    ::fixy::Option<::fixy::numa::NodeAllocationCounters> const after = counters_of_node_one(test);
    os::NumaNodeId const node =
        reservation.node_of_first_page(committed).expect("the census reads the node of a touched page");
    ::fixy::Option<NodeOneGrowth> const growth = before.match(
        [&after](::fixy::numa::NodeAllocationCounters const& first) noexcept {
            return after.match(
                [&first](::fixy::numa::NodeAllocationCounters const& last) noexcept {
                    return ::fixy::Option<NodeOneGrowth>::some(
                        NodeOneGrowth{.hit = last.hit >= first.hit ? last.hit - first.hit : 0,
                                      .foreign = last.foreign >= first.foreign ? last.foreign - first.foreign : 0});
                },
                []() noexcept { return ::fixy::Option<NodeOneGrowth>{}; });
        },
        []() noexcept { return ::fixy::Option<NodeOneGrowth>{}; });
    FirstPageVerdict const verdict =
        judge_first_page({.node = node, .pages = 2 * kMebibyte / host_page_bytes(), .growth = growth});
    if (verdict == FirstPageVerdict::uncounted) {
        ::fixy::check::pass(test, "  [skipped] the first page is off node 1, and the counters of node 1 did not "
                                  "count the write\n");
        return;
    }
    if (verdict == FirstPageVerdict::fell_back) {
        ::fixy::report(::fixy::Sink::Err,
                       "  [fallback] the first page is on node {}, and numa_foreign of node 1 grew\n", node.raw());
    }
    CRUCIBLE_TEST_ASSERT(test, verdict != FirstPageVerdict::misplaced);
    os::NumaNodeSet const both = os::NumaNodeSet{}
                                     .with(os::numa_node_slot(os::NumaNodeId{0}).expect("node 0 has a slot"))
                                     .with(os::numa_node_slot(os::NumaNodeId{1}).expect("node 1 has a slot"));
    os::Reservation interleaved = reserve_or_end(ByteCount{2 * kMebibyte}, os::ReserveAlignment::page);
    CRUCIBLE_TEST_ASSERT(test, interleaved.bind(interleaved.whole(), os::Placement::interleaved(both))
                                   == os::Placement::interleaved(both));
    ::fixy::check::pass(test, "a bind to node 1 puts the first page on node 1\n");
}

void test_current_node(::fixy::TestRunnerCtx const& test) {
    os::NumaNodeId const node = os::current_node().expect("getcpu gives the node of the calling thread");
    CRUCIBLE_TEST_ASSERT(test, node.raw() < os::kNumaNodeSetBits);
    ::fixy::check::pass(test, "getcpu gives the node of the calling thread\n");
}

void test_grow_keeps_each_byte(::fixy::TestRunnerCtx const& test) {
    ::fixy::FixedArray<os::ReserveAlignment, 2> const alignments{os::ReserveAlignment::page,
                                                                 os::ReserveAlignment::huge_page};
    for (os::ReserveAlignment const alignment : alignments) {
        os::Reservation reservation = reserve_or_end(ByteCount{4 * kMebibyte}, alignment);
        os::CommittedRange const before = commit_whole(reservation);
        write_each_page(reservation.bytes_of(before), 0x6d);
        CRUCIBLE_TEST_ASSERT(test, reservation.grow(ByteCount{16 * kMebibyte}).is_ok());
        CRUCIBLE_TEST_ASSERT(test, reservation.size() == ByteCount{16 * kMebibyte});
        if (alignment == os::ReserveAlignment::huge_page) {
            CRUCIBLE_TEST_ASSERT(test, reservation.address_of(reservation.whole()) % os::kPageSourceHugePageBytes == 0);
        }
        os::CommittedRange const after = commit_whole(reservation);
        ::fixy::View<std::uint8_t> const bytes = reservation.bytes_of(after);
        CRUCIBLE_TEST_ASSERT(test,
                             holds_each_page(bytes.window(0, 4 * kMebibyte).expect("the old part lies inside"), 0x6d));
        CRUCIBLE_TEST_ASSERT(
            test, reads_zero(bytes.window(4 * kMebibyte, 12 * kMebibyte).expect("the new part lies inside")));
    }
    ::fixy::check::pass(test, "a grow keeps each byte, and the alignment of a huge page\n");
}

void test_grow_refusals(::fixy::TestRunnerCtx const& test) {
    std::size_t const page = host_page_bytes();
    os::Reservation reservation = reserve_or_end(ByteCount{4 * kMebibyte}, os::ReserveAlignment::page);
    CRUCIBLE_TEST_ASSERT(test,
                         reservation.grow(ByteCount{kMebibyte}).err().value_or(os::Errno::eio) == os::Errno::einval);
    CRUCIBLE_TEST_ASSERT(test, reservation.grow(ByteCount{4 * kMebibyte}).is_ok());
    os::CommittedRange const half =
        reservation.commit(reservation.range(start_at(0), ByteCount{2 * kMebibyte}).expect("the first half is a range"))
            .expect("the host commits the first half");
    CRUCIBLE_TEST_ASSERT(test, reservation.grow(ByteCount{8 * kMebibyte}).err().value_or(os::Errno::eio)
                                   == os::Errno::efault);
    CRUCIBLE_TEST_ASSERT(test, reservation.size() == ByteCount{4 * kMebibyte});
    *reservation.bytes_of(half).get(page).expect("a byte of the first half lies in its View") = 0x2e;
    ::fixy::check::pass(test, "a grow refuses a smaller count and a reservation of two kinds of access\n");
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the child reads a reserved page that no commit opened, which faults")
namespace {

// Reads the byte at an address of a reserved page.
[[nodiscard]] std::uint8_t read_reserved_byte(std::uintptr_t address) noexcept {
    return *__builtin_bit_cast(std::uint8_t const volatile*, address);
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// The child faults: it ends by SIGSEGV, or by the report of a sanitizer that
// catches the fault, which names it.
void test_reserved_page_faults(::fixy::TestRunnerCtx const& test) {
    fp::Child child = fp::spawn_child(test, [] noexcept {
                          os::Reservation reservation =
                              reserve_or_end(ByteCount{kMebibyte}, os::ReserveAlignment::page);
                          std::uint8_t const byte = read_reserved_byte(reservation.address_of(reservation.whole()));
                          ::fixy::report(::fixy::Sink::Err, "the read gave {} and did not fault\n", byte);
                      }).expect("the test starts a child");
    fp::ChildReport const report = fp::wait(std::move(child)).expect("the test waits for its child");
    bool const is_fault_signal = report.is_signaled_by(kFaultSignal);
    bool const is_sanitizer_fault = ::fixy::find(report.output(), ::fixy::TextView{"SEGV"}).is_some();
    if (!is_fault_signal && !is_sanitizer_fault) {
        ::fixy::report(::fixy::Sink::Err, "the child ended with the kind {} and the number {}, and wrote:\n{}\n",
                       static_cast<std::uint8_t>(report.end().kind), report.end().number, report.output());
    }
    CRUCIBLE_TEST_ASSERT(test, is_fault_signal || is_sanitizer_fault);
    ::fixy::check::pass(test, "a read of a reserved page that no commit opened faults\n");
}

void test_range_of_another_reservation_dies(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            os::Reservation first = reserve_or_end(ByteCount{kMebibyte}, os::ReserveAlignment::page);
            os::Reservation second = reserve_or_end(ByteCount{kMebibyte}, os::ReserveAlignment::page);
            static_cast<void>(second.commit(first.whole()));
        },
        ::fixy::TextView{"PageSource: a range of the reservation at"});
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            os::Reservation reservation = reserve_or_end(ByteCount{4 * kMebibyte}, os::ReserveAlignment::huge_page);
            os::CommittedRange const old = commit_whole(reservation);
            // A reservation of a huge page always moves at a grow.
            reservation.grow(ByteCount{8 * kMebibyte}).expect("the host grows the reservation");
            static_cast<void>(reservation.bytes_of(old));
        },
        ::fixy::TextView{"PageSource: a range of the reservation at"});
    ::fixy::check::pass(test, "a range of a different reservation, or of the old place of a grow, ends the process\n");
}

// The resident pages of the process: the second field of /proc/self/statm.
[[nodiscard]] std::uint64_t resident_pages(::fixy::TestRunnerCtx const& test) noexcept {
    ::foundation::core::FixedText<256> text{};
    CRUCIBLE_TEST_ASSERT(test, os::read_file("/proc/self/statm", text).is_ok());
    std::uint64_t resident = 0;
    std::uint32_t field = 0;
    for (char const character : text.view()) {
        if (character == ' ') {
            ++field;
            continue;
        }
        if (field == 1 && character >= '0' && character <= '9') {
            resident = resident * 10 + static_cast<std::uint64_t>(character - '0');
        }
    }
    return resident;
}

// The end of a reservation of 2 GiB with no commit writes no shadow of a
// sanitizer: the resident pages grow by at most 16 MiB.  An unpoison of the
// whole range before a raw unmap wrote 256 MiB of shadow.
void test_end_of_a_large_reservation_writes_no_shadow(::fixy::TestRunnerCtx const& test) {
    constexpr std::size_t kLargeBytes = std::size_t{2} << 30;
    constexpr std::uint64_t kLimitBytes = std::uint64_t{16} << 20;
    std::uint64_t const page = host_page_bytes();
    std::uint64_t const before = resident_pages(test);
    {
        os::Reservation const large = reserve_or_end(ByteCount{kLargeBytes}, os::ReserveAlignment::huge_page);
        static_cast<void>(large.size());
    }
    std::uint64_t const after = resident_pages(test);
    std::uint64_t const grown = after > before ? (after - before) * page : 0;
    if (grown > kLimitBytes) {
        ::fixy::report(::fixy::Sink::Err, "the end of a reservation of 2 GiB grew the resident bytes by {}\n", grown);
    }
    CRUCIBLE_TEST_ASSERT(test, grown <= kLimitBytes);
    ::fixy::check::pass(test, "the end of a reservation of 2 GiB writes no shadow\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_reserve_rounds_to_whole_pages(test);
    test_reserve_at_a_huge_page(test);
    test_reserve_refusals(test);
    test_ranges_lie_inside(test);
    test_commit_and_decommit(test);
    test_populate_changes_no_byte(test);
    test_huge_pages_change_no_byte(test);
    test_refused_collapse_gives_base_pages(test);
    test_bind_falls_back(test);
    test_first_page_verdict(test);
    test_bind_places_the_pages(test);
    test_current_node(test);
    test_grow_keeps_each_byte(test);
    test_grow_refusals(test);
    test_reserved_page_faults(test);
    test_range_of_another_reservation_dies(test);
    test_end_of_a_large_reservation_writes_no_shadow(test);
    return 0;
}
