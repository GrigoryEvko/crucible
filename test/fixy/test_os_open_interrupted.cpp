// An open of a FIFO with no writer waits for a writer.  A timer signal whose
// handler has no SA_RESTART interrupts that wait again and again, and the
// open door starts the open again after each signal.  So the open waits until
// a writer opens the FIFO, and it gives the file.  The old door issued openat
// one time, so the first signal made the open give eintr, and the caller saw
// a failed open of a file that exists.
//
// The reader runs in one child and the writer in a second child.  The timer
// raises SIGALRM in the reader each 2 ms.  The writer waits 100 ms, so the
// reader waits in its open through about 50 signals, and then the writer
// opens the FIFO with no wait each 1 ms until a reader is there.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <fixy/os/Process.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The count of the timer signals in the reader.  A store to an atomic word is
// safe in a handler.
constinit ::foundation::core::Atomic<std::uint32_t> timer_signals{0};

void count_the_signal(int) noexcept { static_cast<void>(timer_signals.fetch_add_acq_rel(1)); }

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a timer signal with no SA_RESTART interrupts an open of a FIFO with no writer")
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

namespace {

// The directory of the FIFO, and the path of the FIFO in it.
char fifo_directory[64] = "/tmp/crucible_open_interrupted_XXXXXX";
char fifo_path[96] = {};
std::size_t fifo_path_length = 0;

// Makes a directory of its own and the FIFO "fifo" in it.
[[nodiscard]] bool make_the_fifo() noexcept {
    if (::mkdtemp(fifo_directory) == nullptr) return false;
    std::size_t length = 0;
    for (char const* character = fifo_directory; *character != '\0'; ++character) {
        fifo_path[length] = *character;
        ++length;
    }
    for (char const* character = "/fifo"; *character != '\0'; ++character) {
        fifo_path[length] = *character;
        ++length;
    }
    fifo_path_length = length;
    return ::mkfifo(fifo_path, 0600) == 0;
}

void remove_the_fifo() noexcept {
    static_cast<void>(::unlink(fifo_path));
    static_cast<void>(::rmdir(fifo_directory));
}

// The text of the path of the FIFO.
[[nodiscard]] ::fixy::TextView fifo_text() noexcept { return ::fixy::mint_foreign_text(fifo_path, fifo_path_length); }

// Installs the handler of SIGALRM with no SA_RESTART, and a timer that raises
// the signal each 2 ms.
[[nodiscard]] bool start_the_timer() noexcept {
    struct ::sigaction action{};
    action.sa_handler = count_the_signal;
    action.sa_flags = 0;
    static_cast<void>(::sigemptyset(&action.sa_mask));
    if (::sigaction(SIGALRM, &action, nullptr) != 0) return false;
    ::itimerval timer{};
    timer.it_interval.tv_usec = 2000;
    timer.it_value.tv_usec = 2000;
    return ::setitimer(ITIMER_REAL, &timer, nullptr) == 0;
}

void stop_the_timer() noexcept {
    ::itimerval stopped{};
    static_cast<void>(::setitimer(ITIMER_REAL, &stopped, nullptr));
}

void pause_for(long nanoseconds) noexcept {
    ::timespec pause{};
    pause.tv_nsec = nanoseconds;
    static_cast<void>(::nanosleep(&pause, nullptr));
}

// Waits 100 ms, then opens the FIFO for writing with no wait each 1 ms until
// a reader waits in its open, for at most 5 s.  The writer holds the FIFO
// open 50 ms, so the open of the reader ends.
[[nodiscard]] bool open_the_writer() noexcept {
    pause_for(100000000);
    for (int attempt = 0; attempt < 5000; ++attempt) {
        int const descriptor = ::open(fifo_path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (descriptor >= 0) {
            pause_for(50000000);
            static_cast<void>(::close(descriptor));
            return true;
        }
        pause_for(1000000);
    }
    return false;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// Opens the FIFO for reading through the open door while the timer
// interrupts the open.  The child exits with 0 when the open gives the file
// after at least one signal.
void read_the_fifo() noexcept {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    if (!start_the_timer()) ::fixy::fatal("the reader does not start the timer");
    const bool is_opened =
        ::fixy::fs::mint_file<::fixy::fs::read_only>(
            ctx, ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(fifo_text()))
                     .expect("the path of the FIFO passes the sanitizer"))
            .match(
                [](::fixy::Linear<::fixy::fs::FileOf<::fixy::fs::read_only>> opened) {
                    static_cast<void>(std::move(opened).consume());
                    return true;
                },
                [](::foundation::os::Errno) { return false; });
    stop_the_timer();
    if (!is_opened) ::fixy::fatal("the open of the FIFO failed while a timer signal interrupted it");
    if (timer_signals.load_acquire() == 0) ::fixy::fatal("no timer signal reached the open of the FIFO");
}

void write_the_fifo() noexcept {
    if (!open_the_writer()) ::fixy::fatal("no reader waited in its open of the FIFO");
}

}  // namespace

int main() {
    CRUCIBLE_TEST_ASSERT(kTest, make_the_fifo() && "the test makes its FIFO");
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    ::fixy::process::Child reader =
        ::fixy::process::spawn_child(ctx, [] noexcept { read_the_fifo(); }).expect("the test starts the reader");
    ::fixy::process::Child writer =
        ::fixy::process::spawn_child(ctx, [] noexcept { write_the_fifo(); }).expect("the test starts the writer");
    const ::fixy::process::ChildReport reader_report =
        ::fixy::process::wait(std::move(reader)).expect("the test waits for the reader");
    const ::fixy::process::ChildReport writer_report =
        ::fixy::process::wait(std::move(writer)).expect("the test waits for the writer");
    remove_the_fifo();
    CRUCIBLE_TEST_ASSERT(kTest, reader_report.is_exit_with(0)
                                    && "an open of a FIFO starts again after each signal, until a writer opens it");
    CRUCIBLE_TEST_ASSERT(kTest, writer_report.is_exit_with(0));
    ::fixy::check::pass(kTest, "test_os_open_interrupted: all tests passed\n");
    return 0;
}
