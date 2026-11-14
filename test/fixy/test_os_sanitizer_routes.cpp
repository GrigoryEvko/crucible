// The routes of the raw primitive in a build with ThreadSanitizer for a
// close (THE SANITIZERS of foundation/os/Syscall.h).
//
// THE CLOSE
//   The sanitizer keeps a state for each descriptor number.  A read or a
//   write of the C library reads that state, and a close of the C library
//   checks it against each use in a different thread and then clears it.  A
//   raw close that the sanitizer does not see leaves the state of the closed
//   descriptor.  The kernel then gives the same number to the next open, and
//   a close of the C library in a different thread reports a race with a use
//   of the closed descriptor, which no thread shares.
//
//   The first thread opens /dev/null through the door, reads it through the
//   door, and closes it through the door.  The second thread then opens
//   /dev/null through the door and gets the same number.  It closes that
//   descriptor with close(2) of the C library, as a different library of the
//   process does.
//
// In each case a relaxed atomic word orders the two threads.  The sanitizer
// takes no happens-before edge from it, as it takes none from the kernel when
// the kernel gives a number again.  The tsan preset runs the test with
// halt_on_error=1, so a report of a race fails it.  Each other preset runs the
// same steps with no sanitizer.

#include <fixy/Core.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>

#include <cstddef>
#include <cstdint>
#include <utility>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two threads with no happens-before edge, and a close of the C library")
#include <unistd.h>

namespace {

// A word that the first thread of a case sets to a value other than zero
// when it ends its part.  The two threads read and write it with relaxed
// order only, so the sanitizer takes no happens-before edge from it.
struct RelaxedWord {
    std::uint64_t value = 0;
};

void publish_relaxed(RelaxedWord& word, std::uint64_t value) noexcept {
    __atomic_store_n(&word.value, value, __ATOMIC_RELAXED);
}

[[nodiscard]] std::uint64_t read_relaxed(RelaxedWord const& word) noexcept {
    return __atomic_load_n(&word.value, __ATOMIC_RELAXED);
}

// Closes DESCRIPTOR with close(2) of the C library, which the sanitizer
// intercepts.
[[nodiscard]] bool close_through_the_c_library(int descriptor) noexcept { return ::close(descriptor) == 0; }

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

namespace eff = ::foundation::effects;
namespace fs = ::fixy::fs;
namespace src = ::fixy::tags::source;
using ::fixy::thread::ThreadOptions;

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

// The second thread of a case spins until the first one publishes.
void wait_for(RelaxedWord const& word) noexcept {
    while (read_relaxed(word) == 0U) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

// A descriptor of /dev/null under DEVICES, which must open.
[[nodiscard]] fs::OwnedFd open_null(BgCtx const& ctx, fs::Dirfd const& devices) {
    return fs::mint_file_at<fs::read_only>(ctx, devices, "null").expect("/dev/null opens for a read").consume();
}

// The count of the descriptors that the second thread of the close case
// keeps before it gives up.
constexpr std::size_t kOpenTries = 64;

// Opens /dev/null until the descriptor has FIRST_NUMBER, and keeps each
// lower descriptor open while it tries.  The kernel gives each open the
// lowest free number, so a number that a different part of the process freed
// below FIRST_NUMBER comes first.  It closes the descriptor with FIRST_NUMBER
// through the C library, and gives true when it found and closed it.
[[nodiscard]] bool open_again_and_close(BgCtx const& ctx, fs::Dirfd const& devices, int first_number,
                                        std::size_t tries_left) {
    if (tries_left == 0) return false;
    fs::OwnedFd null = open_null(ctx, devices);
    if (null.get() == first_number) return close_through_the_c_library(null.release());
    if (null.get() > first_number) return false;
    return open_again_and_close(ctx, devices, first_number, tries_left - 1);
}

// THE CLOSE: a close of the C library after a raw close of the same number
// reports no race.
[[nodiscard]] int check_reused_number_keeps_no_state() {
    BgCtx const ctx{eff::testing::bg()};
    fs::Dirfd const devices = fs::open_dirfd(ctx, sanitized("/dev")).expect("/dev opens");
    RelaxedWord closed{};
    bool has_read = false;
    bool has_closed = false;
    auto first = [&]() noexcept {
        int first_number = -1;
        {
            fs::OwnedFd const null = open_null(ctx, devices);
            first_number = null.get();
            foundation::core::FixedArray<std::byte, 8> bytes{};
            has_read = fs::read_full(ctx, null, bytes.view()).is_ok();
        }
        publish_relaxed(closed, static_cast<std::uint64_t>(first_number) + 1U);
    };
    auto second = [&]() noexcept {
        wait_for(closed);
        int const first_number = static_cast<int>(read_relaxed(closed) - 1U);
        has_closed = open_again_and_close(ctx, devices, first_number, kOpenTries);
    };
    if (::fixy::thread::run_joined(ctx, ThreadOptions{}, first, second).is_err()) return 1;
    if (!has_read) return 2;
    if (!has_closed) return 3;
    return 0;
}

}  // namespace

int main() {
    int const failed = check_reused_number_keeps_no_state();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_sanitizer_routes: check {} failed\n", failed);
        return failed;
    }
    fixy::report(fixy::Sink::Out, "test_os_sanitizer_routes: passed\n");
    return 0;
}
