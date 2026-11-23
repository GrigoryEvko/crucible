// The routes of the raw primitive in a build with ThreadSanitizer for a
// close and for a map (the sanitizers of foundation/os/Syscall.h).
//
// Close
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
// Map
//   The sanitizer keeps a shadow of each address that a thread reads or
//   writes.  A raw unmap that it does not see leaves the shadow of the range,
//   and a raw map that gives the same range again starts with the accesses
//   of the old map.  A write of a different thread to the new map then
//   reports a race with a write to the old map, which no thread shares.
//
//   The case runs in a child process of the child door, so only the two
//   threads of the case map memory while it runs.  The second thread starts
//   and tells the first one, so the stack and the alternate signal stack of
//   each thread exist before the unmap.  The first thread then maps a
//   guarded region through the door, writes its first byte, and unmaps it.
//   The second thread maps the same range with the fixed map of the door,
//   and writes its first byte.  So the kernel gives the same range each time,
//   and the case does not hope for the place of a map.  The runtime of a
//   sanitizer can map memory of its own into the free range between the
//   unmap and the fixed map.  The fixed map then gives eexist, and the case
//   runs a new round with two new threads.
//
// In each case a relaxed atomic word orders the two threads.  The sanitizer
// takes no happens-before edge from it, as it takes none from the kernel when
// the kernel gives a number or a range again.  The tsan preset runs the test
// with halt_on_error=1, so a report of a race fails it.  Each other preset
// runs the same steps with no sanitizer.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <fixy/os/Process.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/Transfer.h>
#include <foundation/core/Array.h>
#include <foundation/os/GuardedRegion.h>

#include <cstddef>
#include <cstdint>
#include <utility>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two threads with no happens-before edge, and a close of the C library")
#include <unistd.h>

namespace {

// Writes value to the first usable byte of a region.
void write_first_byte(::foundation::os::GuardedRegion& region, char value) noexcept { region.data_mut()[0] = value; }

// A word that the first thread of a case sets to a value other than zero
// when it ends its part.  The two threads read and write it with relaxed
// order only, so the sanitizer takes no happens-before edge from it.  The
// word is private, and each access of it is atomic, so the class is a cell
// of its payload (payload_cell of foundation/Transfer.h): two threads can
// read and write it at the same time.
template <class Word>
class[[= ::foundation::transfer::payload_cell{}]] RelaxedCell {
public:
    void publish_relaxed(Word value) noexcept { __atomic_store_n(&value_, value, __ATOMIC_RELAXED); }

    [[nodiscard]] Word read_relaxed() const noexcept { return __atomic_load_n(&value_, __ATOMIC_RELAXED); }

private:
    Word value_ = 0;
};

using RelaxedWord = RelaxedCell<std::uint64_t>;

// A file of /dev/null, open for reading only.
using NullFile = ::fixy::fs::FileOf<::fixy::fs::read_only>;

// The number of the descriptor of file, which the kernel gave the open.
[[nodiscard]] int number_of(NullFile const& file) noexcept { return file.ref().abi_descriptor(); }

// Takes the descriptor of file, and closes it with close(2) of the C library,
// which the sanitizer intercepts.  The file is empty after the call.
[[nodiscard]] bool close_through_the_c_library(NullFile&& file) noexcept {
    return ::close(std::move(file).release_to_abi()) == 0;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

namespace eff = ::foundation::effects;
namespace fs = ::fixy::fs;
namespace os = ::foundation::os;
namespace src = ::fixy::tags::source;
using ::fixy::thread::ThreadOptions;

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

// The second thread of a case spins until the first one publishes.
void wait_for(RelaxedWord const& word) noexcept {
    while (word.read_relaxed() == 0U) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

// A file of /dev/null under devices, which must open.
[[nodiscard]] NullFile open_null(BgCtx const& ctx, fs::DirectoryHandle const& devices) {
    return fs::mint_file_at<fs::read_only>(ctx, devices.ref(), "null").expect("/dev/null opens for a read").consume();
}

// The count of the descriptors that the second thread of the close case
// keeps before it gives up.
constexpr std::size_t kOpenTries = 64;

// Opens /dev/null until the descriptor has the first number, and keeps each
// lower descriptor open while it tries.  The kernel gives each open the
// lowest free number, so a number that a different part of the process freed
// below the first number comes first.  It closes the descriptor with the first number
// through the C library, and gives true when it found and closed it.
[[nodiscard]] bool open_again_and_close(BgCtx const& ctx, fs::DirectoryHandle const& devices, int first_number,
                                        std::size_t tries_left) {
    if (tries_left == 0) return false;
    NullFile null = open_null(ctx, devices);
    if (number_of(null) == first_number) return close_through_the_c_library(std::move(null));
    if (number_of(null) > first_number) return false;
    return open_again_and_close(ctx, devices, first_number, tries_left - 1);
}

// The close: a close of the C library after a raw close of the same number
// reports no race.
[[nodiscard]] int check_reused_number_keeps_no_state() {
    BgCtx const ctx{eff::testing::bg()};
    fs::DirectoryHandle const devices = fs::open_directory(ctx, sanitized("/dev")).expect("/dev opens");
    RelaxedWord closed{};
    // Only the thread of a body writes its result, and the main thread reads
    // it after the join, so the result gives the two bodies no edge.
    foundation::core::Atomic<bool> has_read{false};
    foundation::core::Atomic<bool> has_closed{false};
    auto first = [&](BgCtx const& thread_ctx) noexcept {
        int first_number = -1;
        {
            NullFile const null = open_null(thread_ctx, devices);
            first_number = number_of(null);
            foundation::core::FixedArray<std::byte, 8> bytes{};
            bool const is_read = bytes.with_mut([&thread_ctx, &null](auto run) noexcept {
                return fs::read_full(thread_ctx, null.ref(), std::move(run)).is_ok();
            });
            has_read.store_release(is_read);
        }
        closed.publish_relaxed(static_cast<std::uint64_t>(first_number) + 1U);
    };
    auto second = [&](BgCtx const& thread_ctx) noexcept {
        wait_for(closed);
        int const first_number = static_cast<int>(closed.read_relaxed() - 1U);
        has_closed.store_release(open_again_and_close(thread_ctx, devices, first_number, kOpenTries));
    };
    if (::fixy::thread::run_joined(ctx, ThreadOptions{}, std::move(first), std::move(second)).is_err()) return 1;
    if (!has_read.load_acquire()) return 2;
    if (!has_closed.load_acquire()) return 3;
    return 0;
}

// The usable bytes of each region of the map case.
constexpr ::fixy::ByteCount kRegionBytes{std::size_t{1} << 20U};

// The count of the rounds of the map case.  The runtime of a sanitizer can map
// memory of its own into the free range between the unmap and the fixed map
// of a round.  The fixed map then gives eexist, and the case runs a new round
// with two new threads.
constexpr int kMapRounds = 16;

// The end of one round of the map case.
enum class RoundEnd : std::uint8_t {
    // The second thread mapped the range again and wrote to it.
    reused,
    // A map of the runtime held a part of the range, so the fixed map gave
    // eexist.
    range_taken,
};

// One round of the map case.  An error of the fixed map other than eexist
// ends the process.
[[nodiscard]] RoundEnd reuse_range_once(BgCtx const& ctx) {
    RelaxedWord second_started{};
    RelaxedWord unmapped{};
    // Only the second thread writes the result, and the caller reads it after
    // the join, so the result gives the two bodies no edge.
    foundation::core::Atomic<bool> has_reused{false};
    auto first = [&]() noexcept {
        wait_for(second_started);
        std::uint64_t first_address = 0;
        {
            os::GuardedRegion region = os::map_guarded_region(kRegionBytes).expect("a guarded region maps");
            first_address = static_cast<std::uint64_t>(region.address());
            write_first_byte(region, 1);
        }
        unmapped.publish_relaxed(first_address);
    };
    auto second = [&]() noexcept {
        second_started.publish_relaxed(1);
        wait_for(unmapped);
        foundation::core::Result<os::GuardedRegion, os::Errno> again =
            os::map_guarded_region_at(os::RegionAddress{unmapped.read_relaxed()}, kRegionBytes);
        if (again.is_err()) {
            os::Errno const error = std::move(again).err().expect("a refused map holds its error");
            if (error != os::Errno::eexist) {
                fixy::fatal("test_os_sanitizer_routes: the fixed map of the map case failed with the error {}, and "
                            "only eexist tells that a map of the runtime took the range",
                            static_cast<std::uint16_t>(error));
            }
            return;
        }
        os::GuardedRegion region = std::move(again).expect("a fixed map with no error holds a region");
        write_first_byte(region, 2);
        has_reused.store_release(true);
    };
    if (::fixy::thread::run_joined(ctx, ThreadOptions{}, std::move(first), std::move(second)).is_err()) {
        fixy::fatal("test_os_sanitizer_routes: the thread door did not start the two threads of the map case");
    }
    return has_reused.load_acquire() ? RoundEnd::reused : RoundEnd::range_taken;
}

// The exit code of the child of the map case when a map of the runtime took
// the range in each round.
constexpr std::uint8_t kRangeTakenInEachRound = 12;

// The limit of the wait for the child of the map case.  A round that does not
// end, such as one whose second thread does not start, ends at the limit.
constexpr ::fixy::process::WaitLimit kMapCaseLimit{120000};

// The map: a write to a new map of a range that a raw unmap gave back reports
// no race with a write to the old map.  The rounds run in a child process,
// and a report of the sanitizer ends the child with a code other than zero.
// The fork comes after each thread of the close case joined, so the child
// starts its threads from a process of one thread, as ThreadSanitizer
// requires.  The child writes the round that reused the range to its
// standard error, and the parent writes that output.
[[nodiscard]] int check_reused_range_keeps_no_shadow() {
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    auto run_rounds = [&test]() noexcept {
        BgCtx const ctx{eff::testing::bg()};
        for (int round = 0; round < kMapRounds; ++round) {
            if (reuse_range_once(ctx) == RoundEnd::reused) {
                fixy::report(fixy::Sink::Err, "the round {} of the map case reused the range\n", round);
                return;
            }
        }
        ::fixy::process::exit_now(test, kRangeTakenInEachRound);
    };
    ::fixy::process::Child child =
        ::fixy::process::spawn_child(test, run_rounds).expect("the child door starts the child of the map case");
    ::fixy::process::ChildReport const report = ::fixy::process::wait_within(std::move(child), kMapCaseLimit)
                                                    .expect("the child door collects the child of the map case");
    bool const is_reused = report.is_exit_with(0);
    fixy::report(is_reused ? fixy::Sink::Out : fixy::Sink::Err, "{}", report.output());
    if (is_reused) return 0;
    if (report.is_exit_with(kRangeTakenInEachRound)) return 12;
    return 11;
}

}  // namespace

int main() {
    int failed = check_reused_number_keeps_no_state();
    if (failed == 0) failed = check_reused_range_keeps_no_shadow();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_sanitizer_routes: check {} failed\n", failed);
        return failed;
    }
    fixy::report(fixy::Sink::Out, "test_os_sanitizer_routes: passed\n");
    return 0;
}
