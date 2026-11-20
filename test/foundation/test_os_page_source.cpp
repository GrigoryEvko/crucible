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
// host with two nodes a bind to node 1 puts the first page on node 1.  A grow
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
#include <fixy/os/Pages.h>
#include <fixy/os/Process.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Auxiliary.h>
#include <foundation/os/Errno.h>

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

// On a host with two nodes, a bind to node 1 before the first touch puts each
// page on node 1.  A host with one node refuses the bind, and the test skips.
void test_bind_places_the_pages(::fixy::TestRunnerCtx const& test) {
    os::Reservation reservation = reserve_or_end(ByteCount{2 * kMebibyte}, os::ReserveAlignment::page);
    os::Placement const second = os::Placement::on_node(os::NumaNodeId{1});
    if (reservation.bind(reservation.whole(), second) != second) {
        ::fixy::check::pass(test, "  [skipped] the host has one node\n");
        return;
    }
    os::CommittedRange const committed = commit_whole(reservation);
    write_each_page(reservation.bytes_of(committed), 0x01);
    os::NumaNodeId const node =
        reservation.node_of_first_page(committed).expect("the census reads the node of a touched page");
    CRUCIBLE_TEST_ASSERT(test, node == os::NumaNodeId{1});
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
    test_bind_places_the_pages(test);
    test_current_node(test);
    test_grow_keeps_each_byte(test);
    test_grow_refusals(test);
    test_reserved_page_faults(test);
    test_range_of_another_reservation_dies(test);
    return 0;
}
