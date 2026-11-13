// The page doors of fixy/os/Pages.h, run against the kernel.
//
// Each case locks, unlocks or advises the pages of a real range of memory,
// or changes the THP setting of this process and puts it back.  A call that
// needs a privilege that the host can lack, such as a lock past the limit
// RLIMIT_MEMLOCK, accepts the error of an unprivileged process.

#include <fixy/Ctx.h>
#include <fixy/os/Pages.h>

#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include <cstddef>

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;
namespace pages = ::fixy::pages;

constexpr ::fixy::InitLoadCtx kStartup{::foundation::effects::testing::init()};

constexpr std::size_t kPageBytes = 4096;

// More bytes than the limit RLIMIT_MEMLOCK of an unprivileged process on a
// usual host, which is 8 MiB.
constexpr std::size_t kPastTheLockLimit = std::size_t{16} * 1024 * 1024;

// True when the error is the refusal of a lock that an unprivileged process
// gets past its limit.
[[nodiscard]] bool is_lock_refusal(os::Errno error) noexcept {
    return error == os::Errno::enomem || error == os::Errno::eperm || error == os::Errno::eagain;
}

// A lock and an unlock of one page need no privilege under the limit.  The
// lock on fault needs Linux 4.4.  A lock of more bytes than the limit gives
// enomem to an unprivileged process, and a privileged process gets the lock.
// An unlock of the page then works in each case.
void lock_and_unlock_reach_the_kernel() {
    ::foundation::effects::Alloc const alloc{};
    core::HeapArray<std::byte, kPageBytes> page =
        core::mint_heap_array<std::byte, kPageBytes>(alloc, kPageBytes, std::byte{0});
    core::View<std::byte const> const bytes = page.view();

    pages::lock_pages(kStartup, bytes).expect("a lock of one page fits below the limit");
    pages::unlock_pages(kStartup, bytes).expect("the unlock of the locked page works");
    pages::lock_pages_on_fault(kStartup, bytes).expect("a lock on fault of one page fits below the limit");
    pages::unlock_pages(kStartup, bytes).expect("the unlock of the page locked on fault works");

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
    ::foundation::effects::Alloc const alloc{};
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

// The exchange gives the setting before the call.  A second exchange with
// the same setting gives that setting back, so the set and the read reach
// the right options of prctl.  The last exchange puts the first setting back.
void thp_exchange_reads_and_sets_the_setting() {
    pages::ThpSetting const first =
        pages::exchange_thp_setting(kStartup, pages::ThpSetting::refused).expect("the process refuses THP");
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
    thp_exchange_reads_and_sets_the_setting();
    return 0;
}
