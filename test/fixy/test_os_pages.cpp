// The page doors of fixy/os/Pages.h, run against the kernel.
//
// Each case locks, unlocks or advises the pages of a real range of memory,
// or changes the THP setting of this process and puts it back.  A call that
// needs a privilege that the host can lack, such as a lock past the limit
// RLIMIT_MEMLOCK, accepts the error of an unprivileged process.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/NumaPlacement.h>
#include <fixy/os/Pages.h>

#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Auxiliary.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;
namespace pages = ::fixy::pages;

constexpr ::fixy::InitLoadCtx kStartup{::foundation::effects::testing::init()};

// The size of a page of this host.
[[nodiscard]] std::uint64_t page_size() noexcept {
    return os::read_auxiliary(os::AuxiliaryEntry::page_size).expect("the auxiliary vector gives the size of a page");
}

// THE FRESH PAGES.  A case that counts the pages with no frame takes an
// anonymous mapping of the kernel.  Each page holds zero, and no page has a
// frame before a write or a populate.  No allocator touches the mapping, so
// this holds in each build: the allocator of ThreadSanitizer writes each
// byte of a zeroed block, and AddressSanitizer writes the first page of a
// block.
using IoBlockCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Test,
    ::foundation::effects::Row<::foundation::effects::Effect::Test, ::foundation::effects::Effect::Alloc,
                               ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>>;

// The owner of the fresh pages of a case.
struct FreshPages final {
    using permission_row = ::foundation::effects::Row<>;
};

using WriteAnon = ::fixy::atom::mmap::with_prot<::fixy::mmap::prot::WriteCopy>;
using Anonymous = ::fixy::atom::mmap::with_share<::fixy::mmap::share::Anonymous>;

// The count of the fresh pages of one mapping.
constexpr std::size_t kFreshPageCount = 8;

// The fresh pages of a case (THE FRESH PAGES), for the permission of the
// case.
template <class Owner>
[[nodiscard]] auto map_fresh_pages(Owner const& owner) {
    IoBlockCtx const ctx{::foundation::effects::testing::test()};
    return ::fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, owner, kFreshPageCount * page_size())
        .expect("an anonymous mapping of the fresh pages works")
        .consume();
}

// Words that the program only reads.  The linker puts a constant with no
// relocation in a segment with no write access.
[[nodiscard]] consteval core::FixedArray<std::uint64_t, 4096> make_read_only_words() noexcept {
    core::FixedArray<std::uint64_t, 4096> words{};
    std::uint64_t value = 1;
    for (std::uint64_t& word : words) {
        word = value;
        ++value;
    }
    return words;
}

constexpr core::FixedArray<std::uint64_t, 4096> kReadOnlyWords = make_read_only_words();

// The count of the pages of bytes that have no frame.  A page that nothing
// wrote has no frame, and a page that a read only touched maps the shared
// zero page, which has no node either.
[[nodiscard]] std::size_t pages_with_no_frame(core::View<std::byte const> bytes) noexcept {
    return ::fixy::numa::census_page_nodes(kStartup, bytes)
        .expect("the kernel gives the node of each page")
        .pages_without_node();
}

constexpr std::size_t kPageBytes = 4096;

// True when the test runs under qemu-user (test/fixy/CMakeLists.txt sets the
// definition).  The emulator has no mlock2, and it refuses the THP options of
// prctl.  A kernel of the floor 5.6 has each of them, so on a kernel these
// refusals are failures.
constexpr bool kIsUnderEmulator = CRUCIBLE_TEST_UNDER_EMULATOR != 0;

// More bytes than the limit RLIMIT_MEMLOCK of an unprivileged process on a
// usual host, which is 8 MiB.
constexpr std::size_t kPastTheLockLimit = std::size_t{16} * 1024 * 1024;

// True when the error is the refusal of a lock that an unprivileged process
// gets past its limit.
[[nodiscard]] bool is_lock_refusal(os::Errno error) noexcept {
    return error == os::Errno::enomem || error == os::Errno::eperm || error == os::Errno::eagain;
}

// A lock and an unlock of one page need no privilege under the limit.  The
// lock on fault needs mlock2, and qemu-user has no mlock2.  The emulator
// gives enosys, and the case then skips the lock on fault.  A lock of more
// bytes than the limit gives enomem to an unprivileged process, and a
// privileged process gets the lock.  An unlock of the page then works in
// each case.
void lock_and_unlock_reach_the_kernel() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    core::HeapArray<std::byte, kPageBytes> page =
        core::mint_heap_array<std::byte, kPageBytes>(alloc, kPageBytes, std::byte{0});
    core::View<std::byte const> const bytes = page.view();

    pages::lock_pages(kStartup, bytes).expect("a lock of one page fits below the limit");
    pages::unlock_pages(kStartup, bytes).expect("the unlock of the locked page works");
    core::Result<core::Unit, os::Errno> const locked_on_fault = pages::lock_pages_on_fault(kStartup, bytes);
    bool const has_no_mlock2 = locked_on_fault.match(
        [](core::Unit) noexcept { return false; }, [](os::Errno error) noexcept { return error == os::Errno::enosys; });
    if (has_no_mlock2) {
        CRUCIBLE_FATAL_INVARIANT(kIsUnderEmulator);
        ::fixy::report(::fixy::Sink::Err, "lock_and_unlock_reach_the_kernel: the lock on fault SKIPPED (qemu-user)\n");
    } else {
        CRUCIBLE_FATAL_INVARIANT(locked_on_fault.is_ok());
        pages::unlock_pages(kStartup, bytes).expect("the unlock of the page locked on fault works");
    }

    core::HeapArray<std::byte, kPageBytes> large =
        core::mint_heap_array<std::byte, kPageBytes>(alloc, kPastTheLockLimit, std::byte{0});
    core::Result<core::Unit, os::Errno> const locked = pages::lock_pages(kStartup, large.view());
    bool const is_refused = locked.match([](core::Unit) noexcept { return false; },
                                         [](os::Errno error) noexcept { return is_lock_refusal(error); });
    CRUCIBLE_FATAL_INVARIANT(locked.is_ok() || is_refused);
    pages::unlock_pages(kStartup, large.view()).expect("an unlock of the large range works");
}

// The advice goes to each whole huge page of the range.  An aligned range of
// two huge pages takes the advice on each byte.  A range that starts one byte
// after the boundary holds one whole huge page.  A range shorter than a huge
// page holds none, and the door makes no system call.  A kernel with no THP
// refuses the advice with einval.
void huge_page_advice_covers_the_whole_huge_pages() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    core::HeapArray<std::byte, pages::huge_page_bytes> region =
        core::mint_heap_array<std::byte, pages::huge_page_bytes>(alloc, 2 * pages::huge_page_bytes, std::byte{0});
    core::View<std::byte const> const bytes = region.view();

    core::Result<std::size_t, os::Errno> const advised = pages::advise_huge_pages(kStartup, bytes);
    bool const is_without_thp = advised.match([](std::size_t) noexcept { return false; },
                                              [](os::Errno error) noexcept { return error == os::Errno::einval; });
    CRUCIBLE_FATAL_INVARIANT(advised.is_ok() || is_without_thp);
    if (is_without_thp) {
        return;
    }
    std::size_t const advised_bytes = advised.match([](std::size_t count) noexcept { return count; },
                                                    [](os::Errno) noexcept { return std::size_t{0}; });
    CRUCIBLE_FATAL_INVARIANT(advised_bytes == 2 * pages::huge_page_bytes);

    core::View<std::byte const> const shifted =
        bytes.window(1, 2 * pages::huge_page_bytes - 1).expect("the window fits in the range");
    CRUCIBLE_FATAL_INVARIANT(pages::advise_huge_pages(kStartup, shifted).value_or(0) == pages::huge_page_bytes);

    core::View<std::byte const> const short_range =
        bytes.window(kPageBytes, pages::huge_page_bytes).expect("the window fits in the range");
    CRUCIBLE_FATAL_INVARIANT(pages::advise_huge_pages(kStartup, short_range).expect("no call, no error") == 0);
    CRUCIBLE_FATAL_INVARIANT(pages::collapse_huge_pages(kStartup, short_range).expect("no call, no error") == 0);

    // The collapse needs Linux 6.1.  The kernel can refuse it for a reason of
    // the moment, such as a lack of free huge pages (enomem), a refused charge
    // of the cgroup (ebusy) or a page that another thread holds (eagain).  So
    // the case asserts only that a collapse that works covers the whole range.
    core::Result<std::size_t, os::Errno> const collapsed = pages::collapse_huge_pages(kStartup, bytes);
    std::size_t const collapsed_bytes = collapsed.match([](std::size_t count) noexcept { return count; },
                                                        [](os::Errno) noexcept { return 2 * pages::huge_page_bytes; });
    CRUCIBLE_FATAL_INVARIANT(collapsed_bytes == 2 * pages::huge_page_bytes);
}

// The populate door faults in each page that holds a byte of its range, and
// no other page.  A range of two bytes across the end of the first page
// reaches two pages, where a loop that steps one page from the first byte of
// the range reaches only the first.  An empty range makes no call, and the
// door writes no byte.  The advice needs Linux 5.14.  A whole page at a page
// boundary is the probe of the advice, so an einval after the probe works is
// the error of the door and not of an older kernel.  qemu user mode takes the
// advice and faults in no page, and the door then gives eopnotsupp.  The
// emulator also refuses the THP knob, so the probe comes before the knob.
// fresh holds the fresh pages of the case (THE FRESH PAGES).
void populate_faults_in_each_page_of(core::View<std::byte> const fresh) {
    std::size_t const page = page_size();
    core::View<std::byte const> const probe_page = fresh.window(page, page).expect("the page fits in the mapping");
    os::Errno const refusal =
        pages::populate_pages(kStartup, probe_page)
            .match([](core::Unit) noexcept { return os::Errno{}; }, [](os::Errno error) noexcept { return error; });
    if (refusal == os::Errno::einval) {
        ::fixy::report(::fixy::Sink::Err, "populate_faults_in_each_page_of_the_range: SKIPPED (Linux before 5.14)\n");
        return;
    }
    if (refusal == os::Errno::eopnotsupp) {
        CRUCIBLE_FATAL_INVARIANT(kIsUnderEmulator);
        ::fixy::report(::fixy::Sink::Err,
                       "populate_faults_in_each_page_of_the_range: SKIPPED (the emulator faults in no page)\n");
        return;
    }
    CRUCIBLE_FATAL_INVARIANT(refusal == os::Errno{});
    CRUCIBLE_FATAL_INVARIANT(pages_with_no_frame(probe_page) == 0);
    // A huge page would give a frame to the pages around a range, so the
    // process refuses huge pages in this case.
    pages::ThpSetting const first_thp =
        pages::exchange_thp_setting(kStartup, pages::ThpSetting::refused).expect("the process refuses THP");
    core::View<std::byte> const bytes =
        fresh.window(3 * page, 5 * page).expect("the five pages after the probe fit in the mapping");
    core::View<std::byte const> const four_pages = bytes.window(page, 4 * page).expect("the pages fit in the mapping");
    auto const window = [&four_pages](std::size_t offset, std::size_t count) {
        return four_pages.window(offset, count).expect("the window fits in the four pages");
    };
    CRUCIBLE_FATAL_INVARIANT(pages_with_no_frame(four_pages) == 4);

    pages::populate_pages(kStartup, core::View<std::byte const>{}).expect("an empty range makes no call");
    CRUCIBLE_FATAL_INVARIANT(pages_with_no_frame(four_pages) == 4);

    pages::populate_pages(kStartup, window(3 * page, page)).expect("the kernel faults in the last page");
    CRUCIBLE_FATAL_INVARIANT(pages_with_no_frame(window(3 * page, page)) == 0);
    CRUCIBLE_FATAL_INVARIANT(pages_with_no_frame(window(0, 3 * page)) == 3);

    pages::populate_pages(kStartup, window(page - 1, 2)).expect("the kernel faults in the two pages of the window");
    CRUCIBLE_FATAL_INVARIANT(pages_with_no_frame(window(0, 2 * page)) == 0);
    CRUCIBLE_FATAL_INVARIANT(pages_with_no_frame(window(2 * page, page)) == 1);

    std::size_t const mark_offset = (3 * page) + 7;
    *bytes.get(mark_offset).expect("the mark is inside the mapping") = std::byte{0x5a};
    pages::populate_pages(kStartup, four_pages).expect("the kernel faults in each page of the four pages");
    CRUCIBLE_FATAL_INVARIANT(pages_with_no_frame(four_pages) == 0);
    std::size_t position = page;
    for (std::byte const value : four_pages) {
        CRUCIBLE_FATAL_INVARIANT(value == (position == mark_offset ? std::byte{0x5a} : std::byte{0}));
        ++position;
    }
    static_cast<void>(pages::exchange_thp_setting(kStartup, first_thp).expect("the process puts back THP"));
}

// The case of the populate door on the fresh pages of its own permission,
// which the case ends after the mapping on each path.
void populate_faults_in_each_page_of_the_range() {
    auto owner = ::foundation::permissions::mint_permission_root<FreshPages>();
    {
        auto mapping = map_fresh_pages(owner);
        populate_faults_in_each_page_of(mapping.view<std::byte>().expect("a mapping holds bytes"));
    }
    ::foundation::linear::drop(std::move(owner));
}

// A page that the process cannot write takes no write fault, so the door
// gives an error.  A loop that writes a byte of each page ends the process
// on such a page with a fault of the protection.
// qemu user mode takes the advice and makes no fault, and the page of the
// words is in memory, so the residency check of the door finds no defect.
void populate_refuses_a_range_with_no_write_access() {
    if (kIsUnderEmulator) {
        ::fixy::report(::fixy::Sink::Err, "populate_refuses_a_range_with_no_write_access: SKIPPED (qemu-user)\n");
        return;
    }
    CRUCIBLE_FATAL_INVARIANT(pages::populate_pages(kStartup, core::bytes(kReadOnlyWords)).is_err());
}

// A record with padding, which bytes() refuses.
struct PaddedRecord {
    std::uint8_t tag = 0;
    std::uint64_t value = 0;
};

// storage_of gives the bytes that hold an object or a run of elements, from
// the address of the first byte.  An empty run gives no byte.
void storage_of_gives_the_bytes_of_an_object_and_of_a_run() {
    PaddedRecord const object{};
    core::View<std::byte const> const storage = pages::storage_of(object);
    CRUCIBLE_FATAL_INVARIANT(storage.size() == sizeof(PaddedRecord));
    CRUCIBLE_FATAL_INVARIANT(core::address_of(core::to(object))
                             == core::address_of(storage.get(0).expect("the storage holds a first byte")));

    core::FixedArray<PaddedRecord, 4> const records{};
    core::View<PaddedRecord const> const elements = records.view();
    core::View<std::byte const> const run_storage = pages::storage_of(elements);
    CRUCIBLE_FATAL_INVARIANT(run_storage.size() == 4 * sizeof(PaddedRecord));
    CRUCIBLE_FATAL_INVARIANT(core::address_of(elements.get(0).expect("the run holds a first element"))
                             == core::address_of(run_storage.get(0).expect("the storage holds a first byte")));
    CRUCIBLE_FATAL_INVARIANT(pages::storage_of(core::View<PaddedRecord const>{}).size() == 0);
}

// The exchange gives the setting before the call.  A second exchange with
// the same setting gives that setting back, so the set and the read reach
// the right options of prctl.  The last exchange puts the first setting back.
// qemu-user refuses the two options of prctl with einval, and the case then
// skips.  On a kernel, einval is a wrong option, and the case fails.
void thp_exchange_reads_and_sets_the_setting() {
    core::Result<pages::ThpSetting, os::Errno> const exchanged =
        pages::exchange_thp_setting(kStartup, pages::ThpSetting::refused);
    bool const is_refused_option = exchanged.match([](pages::ThpSetting) noexcept { return false; },
                                                   [](os::Errno error) noexcept { return error == os::Errno::einval; });
    if (is_refused_option) {
        CRUCIBLE_FATAL_INVARIANT(kIsUnderEmulator);
        ::fixy::report(::fixy::Sink::Err, "thp_exchange_reads_and_sets_the_setting: SKIPPED (qemu-user)\n");
        return;
    }
    CRUCIBLE_FATAL_INVARIANT(exchanged.is_ok());
    pages::ThpSetting const first = exchanged.match([](pages::ThpSetting setting) noexcept { return setting; },
                                                    [](os::Errno) noexcept { return pages::ThpSetting::allowed; });
    pages::ThpSetting const refused =
        pages::exchange_thp_setting(kStartup, pages::ThpSetting::refused).expect("the process reads the refusal");
    CRUCIBLE_FATAL_INVARIANT(refused == pages::ThpSetting::refused);
    pages::ThpSetting const again =
        pages::exchange_thp_setting(kStartup, pages::ThpSetting::allowed).expect("the process allows THP");
    CRUCIBLE_FATAL_INVARIANT(again == pages::ThpSetting::refused);
    pages::ThpSetting const allowed =
        pages::exchange_thp_setting(kStartup, first).expect("the process puts back its first setting");
    CRUCIBLE_FATAL_INVARIANT(allowed == pages::ThpSetting::allowed);
    pages::ThpSetting const back =
        pages::exchange_thp_setting(kStartup, first).expect("the process reads its first setting");
    CRUCIBLE_FATAL_INVARIANT(back == first);
}

}  // namespace

int main() {
    lock_and_unlock_reach_the_kernel();
    huge_page_advice_covers_the_whole_huge_pages();
    populate_faults_in_each_page_of_the_range();
    populate_refuses_a_range_with_no_write_access();
    storage_of_gives_the_bytes_of_an_object_and_of_a_run();
    thp_exchange_reads_and_sets_the_setting();
    return 0;
}
