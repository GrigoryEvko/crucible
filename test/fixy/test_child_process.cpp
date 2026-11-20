// The child door of fixy/os/Process.h: a child that exits with a code, a
// child whose body returns, a child that aborts, a child that is not
// dumpable, a child whose output is larger than the report and than a pipe,
// a Child that no wait takes, the decode of a wait, and the empty Child.
// The report holds the end of the child and the last bytes of its standard
// error.
//
// The wait with a limit: a child that ends before the limit, and a child
// that blocks on its full pipe and ends at the limit with a report.
//
// The traced child: the stops of its system calls, a text in its memory, a
// signal that ends it, its limit, a TracedChild that no wait takes, and the
// empty TracedChild.
//
// The probe of a process: the process of the test, a child before and after
// its wait, an id that no process can hold, two ids that the kernel refuses,
// and the answer of each error.
//
// The identity door: the effective user id against the status of the process.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <fixy/os/Process.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Abort.h>
#include <foundation/os/ChildProcess.h>
#include <foundation/os/File.h>
#include <foundation/Quarantine.h>

#include <cstdint>
#include <utility>

namespace {

namespace fp = ::fixy::process;

// The text of each line of the large output.
constexpr ::fixy::TextView kLongLine{
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"};

// The count of the lines of the large output.  With the last line of four
// characters, the output has about 40,000 bytes: more than the report holds.
// The child with no wait writes two times as many, more than a pipe of 64 KiB
// holds.
constexpr int kLongLines = 400;

// The limit of a wait whose child never ends by itself, and the limit of a
// wait whose child ends at once.
constexpr fp::WaitLimit kShortLimit{200};
constexpr fp::WaitLimit kLongLimit{60000};

// The path that the traced child reads, and the capacity of a text that holds
// each path of an open: the bound of the kernel, PATH_MAX.
constexpr ::fixy::CText kStatusPath{"/proc/self/status"};
constexpr std::size_t kPathCapacity = 4096;

// The largest count of the bytes of /proc/self/status that the test reads.
constexpr std::size_t kStatusBytes = 65536;

// An id above PID_MAX_LIMIT of a 64-bit kernel, 4194304, so no process can
// hold it.  And the smallest id that the kernel reads as a negative word.
constexpr fp::ProcessId kIdAboveTheLimit{4194305};
constexpr fp::ProcessId kNegativeId{0x80000000U};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the test reads the dumpable flag and the system call numbers of the child")
#include <sys/prctl.h>
#include <sys/syscall.h>

namespace {

// The dumpable flag of the calling process (PR_GET_DUMPABLE): 0 when the
// kernel writes no core dump of it, or 1 when it does.
[[nodiscard]] int dumpable_flag_of_this_process() noexcept { return ::prctl(PR_GET_DUMPABLE, 0, 0, 0, 0); }

// The numbers of the system calls that the traced child makes.
constexpr fp::SyscallNumber kGetpidNumber{SYS_getpid};
constexpr fp::SyscallNumber kOpenatNumber{SYS_openat};

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

template <class Body>
[[nodiscard]] fp::ChildReport run_child(::fixy::TestRunnerCtx const& test, Body&& body) {
    fp::Child child = fp::spawn_child(test, std::forward<Body>(body)).expect("the test starts a child");
    return fp::wait(std::move(child)).expect("the test waits for its child");
}

// Starts a traced child, or gives none on a host that refuses the trace, such
// as the emulator of a cross build.  The skip writes a line.
template <class Body>
[[nodiscard]] ::fixy::Option<fp::TracedChild> start_traced(::fixy::TestRunnerCtx const& test, fp::TracedMemory memory,
                                                           Body&& body) {
    ::fixy::Result<fp::TracedChild, ::foundation::os::Errno> started =
        fp::spawn_traced_child(test, memory, std::forward<Body>(body));
    if (started.is_err() && started.err().value_or(::foundation::os::Errno::eio) == ::foundation::os::Errno::eperm) {
        ::fixy::check::pass(test, "  [skipped] the host refuses the trace of a child\n");
        return ::fixy::none;
    }
    return ::fixy::Option<fp::TracedChild>::some(std::move(started).expect("the test starts a traced child"));
}

// Writes the large output to the standard error two times, which is more
// than a pipe of 64 KiB holds.
void write_more_than_a_pipe() noexcept {
    for (int line = 0; line < 2 * kLongLines; ++line) {
        ::fixy::report(::fixy::Sink::Err, "{}\n", kLongLine);
    }
}

void child_exits_with_its_code(::fixy::TestRunnerCtx const& test) {
    fp::ChildReport const report = run_child(test, [&test] noexcept {
        ::fixy::report(::fixy::Sink::Err, "the child writes this line\n");
        fp::exit_now(test, 3);
    });
    CRUCIBLE_TEST_ASSERT(test, report.is_exit_with(3));
    CRUCIBLE_TEST_ASSERT(test, !report.is_signaled_by(::foundation::os::kAbortSignal));
    CRUCIBLE_TEST_ASSERT(test, report.output() == ::fixy::TextView{"the child writes this line\n"});
    CRUCIBLE_TEST_ASSERT(test, report.dropped_bytes() == 0);
    CRUCIBLE_TEST_ASSERT(test, !report.is_ended_at_limit());
}

void child_body_returns(::fixy::TestRunnerCtx const& test) {
    fp::ChildReport const report = run_child(test, [] noexcept {});
    CRUCIBLE_TEST_ASSERT(test, report.is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(test, report.output().size() == 0);
}

void child_aborts(::fixy::TestRunnerCtx const& test) {
    fp::ChildReport const report = run_child(test, [] noexcept { ::fixy::fatal("the child ends here"); });
    CRUCIBLE_TEST_ASSERT(test, report.is_signaled_by(::foundation::os::kAbortSignal));
    CRUCIBLE_TEST_ASSERT(test, ::fixy::find(report.output(), ::fixy::TextView{"the child ends here"}).is_some());
    // THE CORE DUMP of foundation/os/ChildProcess.h: the kernel writes no
    // core dump of a child of the door, also with no core limit.
    CRUCIBLE_TEST_ASSERT(test, !report.end().has_core_dump);
}

// A child of the door is not dumpable, so no host writes a core dump of it
// (THE CORE DUMP of foundation/os/ChildProcess.h).  The child exits with the
// dumpable flag that the kernel gives it.
void child_is_not_dumpable(::fixy::TestRunnerCtx const& test) {
    fp::ChildReport const report = run_child(
        test, [&test] noexcept { fp::exit_now(test, static_cast<std::uint8_t>(dumpable_flag_of_this_process())); });
    CRUCIBLE_TEST_ASSERT(test, report.is_exit_with(0));
}

void child_writes_more_than_the_report(::fixy::TestRunnerCtx const& test) {
    fp::ChildReport const report = run_child(test, [] noexcept {
        for (int line = 0; line < kLongLines; ++line) {
            ::fixy::report(::fixy::Sink::Err, "{}\n", kLongLine);
        }
        ::fixy::report(::fixy::Sink::Err, "END\n");
    });
    constexpr std::uint64_t total = std::uint64_t{kLongLines} * (kLongLine.size() + 1) + 4;
    CRUCIBLE_TEST_ASSERT(test, report.is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(test, report.output().size() == fp::kChildOutputCapacity);
    CRUCIBLE_TEST_ASSERT(test, report.dropped_bytes() == total - fp::kChildOutputCapacity);
    CRUCIBLE_TEST_ASSERT(test, ::fixy::ends_with(report.output(), ::fixy::TextView{"x\nEND\n"}));
}

// The destructor of a Child that no wait took reads its output and waits for
// it, so a child that writes more than a pipe holds does not block the test.
void child_with_no_wait(::fixy::TestRunnerCtx const& test) {
    fp::Child child =
        fp::spawn_child(test, [] noexcept { write_more_than_a_pipe(); }).expect("the test starts a child");
    CRUCIBLE_TEST_ASSERT(test, static_cast<std::uint32_t>(child.id()) > 0);
}

// The decode of a status word of waitpid gives the end of a wait.  On Linux,
// the bits 8 to 15 of the word hold the exit code, the low seven bits hold
// the signal, and the bit 0x80 tells that the kernel wrote a core dump.  No
// child of the door writes a core dump, so only the decode of a word shows
// the bit.
void decode_tells_the_core_dump(::fixy::TestRunnerCtx const& test) {
    namespace os = ::foundation::os;
    os::ChildEnd const exited = os::decode_child_end(0x0300);
    CRUCIBLE_TEST_ASSERT(test, exited.kind == os::ChildEndKind::exited);
    CRUCIBLE_TEST_ASSERT(test, exited.number == 3);
    CRUCIBLE_TEST_ASSERT(test, !exited.has_core_dump);

    os::ChildEnd const aborted = os::decode_child_end(0x0006);
    CRUCIBLE_TEST_ASSERT(test, aborted.kind == os::ChildEndKind::signaled);
    CRUCIBLE_TEST_ASSERT(test, aborted.number == os::kAbortSignal);
    CRUCIBLE_TEST_ASSERT(test, !aborted.has_core_dump);

    os::ChildEnd const dumped = os::decode_child_end(0x0086);
    CRUCIBLE_TEST_ASSERT(test, dumped.kind == os::ChildEndKind::signaled);
    CRUCIBLE_TEST_ASSERT(test, dumped.number == os::kAbortSignal);
    CRUCIBLE_TEST_ASSERT(test, dumped.has_core_dump);

    // The signal 11 is SIGSEGV.
    os::ChildEnd const faulted = os::decode_child_end(0x008b);
    CRUCIBLE_TEST_ASSERT(test, faulted.kind == os::ChildEndKind::signaled);
    CRUCIBLE_TEST_ASSERT(test, faulted.number == 11);
    CRUCIBLE_TEST_ASSERT(test, faulted.has_core_dump);
}

// The empty Child names no process, so a wait for it gives echild and waits
// for no child of the test.
void empty_child_names_no_process(::fixy::TestRunnerCtx const& test) {
    fp::Child empty{};
    CRUCIBLE_TEST_ASSERT(test, static_cast<std::uint32_t>(empty.id()) == 0);
    CRUCIBLE_TEST_ASSERT(test, fp::wait(std::move(empty)).err().value_or(::foundation::os::Errno::eio)
                                   == ::foundation::os::Errno::echild);
    fp::Child empty_with_a_limit{};
    CRUCIBLE_TEST_ASSERT(
        test, fp::wait_within(std::move(empty_with_a_limit), kShortLimit).err().value_or(::foundation::os::Errno::eio)
                  == ::foundation::os::Errno::echild);
}

// A child that ends before the limit gives its own end, and its output.
void child_ends_within_the_limit(::fixy::TestRunnerCtx const& test) {
    fp::Child child = fp::spawn_child(test, [&test] noexcept {
                          ::fixy::report(::fixy::Sink::Err, "the child ends in time\n");
                          fp::exit_now(test, 4);
                      }).expect("the test starts a child");
    fp::ChildReport const report = fp::wait_within(std::move(child), kLongLimit).expect("the test waits for its child");
    CRUCIBLE_TEST_ASSERT(test, report.is_exit_with(4));
    CRUCIBLE_TEST_ASSERT(test, !report.is_ended_at_limit());
    CRUCIBLE_TEST_ASSERT(test, report.output() == ::fixy::TextView{"the child ends in time\n"});
}

// The wait with a limit reads no output while the child runs, so a child that
// writes more than a pipe holds blocks on the full pipe.  The wait ends it at
// the limit, and then the report holds the last bytes of the pipe.
void child_on_a_full_pipe_ends_at_the_limit(::fixy::TestRunnerCtx const& test) {
    fp::Child child =
        fp::spawn_child(test, [] noexcept { write_more_than_a_pipe(); }).expect("the test starts a child");
    fp::ChildReport const report =
        fp::wait_within(std::move(child), kShortLimit).expect("the test waits for its child");
    CRUCIBLE_TEST_ASSERT(test, report.is_ended_at_limit());
    CRUCIBLE_TEST_ASSERT(test, report.is_signaled_by(fp::kLimitSignal));
    CRUCIBLE_TEST_ASSERT(test, report.output().size() == fp::kChildOutputCapacity);
    CRUCIBLE_TEST_ASSERT(test, report.dropped_bytes() > 0);
    CRUCIBLE_TEST_ASSERT(test, ::fixy::starts_with(report.output(), ::fixy::TextView{"x"}));
}

// The visitor of a trace sees the entry of getpid and its exit, whose word is
// the id of the child.
void traced_child_stops_at_its_system_calls(::fixy::TestRunnerCtx const& test) {
    ::fixy::Option<fp::TracedChild> started = start_traced(test, fp::TracedMemory::sealed, [&test] noexcept {
        static_cast<void>(fp::current_process(test));
        fp::exit_now(test, 5);
    });
    if (started.is_none()) return;
    fp::TracedChild child = std::move(started).expect("the host traces the child");
    std::uint32_t const id = static_cast<std::uint32_t>(child.id());
    bool is_in_getpid = false;
    bool has_seen_the_id = false;
    fp::ChildReport const report =
        fp::trace_within(std::move(child), kLongLimit, [&](fp::SyscallStop const& stop) noexcept {
            if (stop.kind() == fp::SyscallStopKind::entry) {
                is_in_getpid = stop.number() == kGetpidNumber;
            } else if (is_in_getpid) {
                has_seen_the_id = has_seen_the_id || stop.result() == std::int64_t{id};
                is_in_getpid = false;
            }
            CRUCIBLE_TEST_ASSERT(test, static_cast<std::uint32_t>(stop.child()) == id);
        }).expect("the test traces its child");
    CRUCIBLE_TEST_ASSERT(test, report.is_exit_with(5));
    CRUCIBLE_TEST_ASSERT(test, !report.is_ended_at_limit());
    CRUCIBLE_TEST_ASSERT(test, has_seen_the_id);
}

// The reads of the path of each openat of a traced child: the count that gave
// the path of the child, and the count that the kernel refused with eio.
struct PathReads {
    int found = 0;
    int refused = 0;
};

// Traces a child that opens kStatusPath, and reads the path of each openat
// from the memory of the child.  None on a host that refuses the trace.
[[nodiscard]] ::fixy::Option<PathReads> read_the_paths_of_a_child(::fixy::TestRunnerCtx const& test,
                                                                  fp::TracedMemory memory) {
    ::fixy::Option<fp::TracedChild> started = start_traced(test, memory, [] noexcept {
        ::fixy::FixedText<4096> status{};
        static_cast<void>(::foundation::os::read_file(kStatusPath, status));
    });
    if (started.is_none()) return ::fixy::none;
    PathReads reads{};
    fp::ChildReport const report =
        fp::trace_within(std::move(started).expect("the host traces the child"), kLongLimit,
                         [&reads](fp::SyscallStop const& stop) noexcept {
                             if (stop.kind() != fp::SyscallStopKind::entry || stop.number() != kOpenatNumber) return;
                             ::fixy::FixedText<kPathCapacity> path{};
                             fp::ChildAddress const address{stop.argument(fp::SyscallArgument::second)};
                             ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> const read =
                                 stop.read_text(address, path);
                             if (read.is_ok() && path.view() == kStatusPath.view()) ++reads.found;
                             if (read.is_err()
                                 && read.err().value_or(::foundation::os::Errno::eio) == ::foundation::os::Errno::eio) {
                                 ++reads.refused;
                             }
                         })
            .expect("the test traces its child");
    CRUCIBLE_TEST_ASSERT(test, report.is_exit_with(0));
    return ::fixy::Option<PathReads>::some(reads);
}

// The visitor reads the path of an openat from the memory of a readable
// child.  The kernel refuses the same read in a sealed child, which is not
// dumpable.
void traced_child_gives_a_text_of_its_memory(::fixy::TestRunnerCtx const& test) {
    ::fixy::Option<PathReads> readable = read_the_paths_of_a_child(test, fp::TracedMemory::readable);
    if (readable.is_none()) return;
    PathReads const from_readable = std::move(readable).expect("the host traces the child");
    CRUCIBLE_TEST_ASSERT(test, from_readable.found >= 1);
    CRUCIBLE_TEST_ASSERT(test, from_readable.refused == 0);
    PathReads const from_sealed =
        read_the_paths_of_a_child(test, fp::TracedMemory::sealed).expect("the host traces the child");
    CRUCIBLE_TEST_ASSERT(test, from_sealed.found == 0);
    CRUCIBLE_TEST_ASSERT(test, from_sealed.refused >= 1);
}

// The tracer delivers the signal of the child, so the abort ends the child
// by SIGABRT, as it ends a child with no tracer.
void traced_child_ends_by_its_signal(::fixy::TestRunnerCtx const& test) {
    ::fixy::Option<fp::TracedChild> started =
        start_traced(test, fp::TracedMemory::sealed, [] noexcept { ::foundation::os::abort_process(); });
    if (started.is_none()) return;
    fp::ChildReport const report = fp::wait_within(std::move(started).expect("the host traces the child"), kLongLimit)
                                       .expect("the test waits for its traced child");
    CRUCIBLE_TEST_ASSERT(test, report.is_signaled_by(::foundation::os::kAbortSignal));
    CRUCIBLE_TEST_ASSERT(test, !report.is_ended_at_limit());
}

// A traced child that blocks on its full pipe ends at the limit of its wait.
void traced_child_ends_at_the_limit(::fixy::TestRunnerCtx const& test) {
    ::fixy::Option<fp::TracedChild> started =
        start_traced(test, fp::TracedMemory::sealed, [] noexcept { write_more_than_a_pipe(); });
    if (started.is_none()) return;
    fp::ChildReport const report = fp::wait_within(std::move(started).expect("the host traces the child"), kShortLimit)
                                       .expect("the test waits for its traced child");
    CRUCIBLE_TEST_ASSERT(test, report.is_ended_at_limit());
    CRUCIBLE_TEST_ASSERT(test, report.is_signaled_by(fp::kLimitSignal));
    CRUCIBLE_TEST_ASSERT(test, report.output().size() == fp::kChildOutputCapacity);
}

// The destructor of a TracedChild that no wait took ends the child, so the
// child, which waits in its first stop, does not stay.
void traced_child_with_no_wait(::fixy::TestRunnerCtx const& test) {
    ::fixy::Option<fp::TracedChild> started = start_traced(test, fp::TracedMemory::sealed, [] noexcept {});
    if (started.is_none()) return;
    fp::TracedChild const child = std::move(started).expect("the host traces the child");
    CRUCIBLE_TEST_ASSERT(test, static_cast<std::uint32_t>(child.id()) > 0);
}

// The empty TracedChild names no process, so each wait for it gives echild.
void empty_traced_child_names_no_process(::fixy::TestRunnerCtx const& test) {
    fp::TracedChild empty{};
    CRUCIBLE_TEST_ASSERT(test, static_cast<std::uint32_t>(empty.id()) == 0);
    CRUCIBLE_TEST_ASSERT(test,
                         fp::wait_within(std::move(empty), kShortLimit).err().value_or(::foundation::os::Errno::eio)
                             == ::foundation::os::Errno::echild);
    fp::TracedChild empty_to_trace{};
    CRUCIBLE_TEST_ASSERT(
        test, fp::trace_within(std::move(empty_to_trace), kShortLimit, [](fp::SyscallStop const&) noexcept {})
                      .err()
                      .value_or(::foundation::os::Errno::eio)
                  == ::foundation::os::Errno::echild);
}

// THE PROCESS PROBE of fixy/os/Process.h.  A child that ended holds its id
// until its wait, so it is alive.  After the wait the kernel frees the id, and
// it gives the ids in a sequence up to 4194304, so no other process takes the
// id before the next probe.
void probe_tells_the_state_of_a_process(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, fp::probe_process(test, fp::current_process(test)) == fp::ProcessState::alive);
    fp::Child child = fp::spawn_child(test, [] noexcept {}).expect("the test starts a child");
    fp::ProcessId const child_id = child.id();
    CRUCIBLE_TEST_ASSERT(test, fp::probe_process(test, child_id) == fp::ProcessState::alive);
    fp::ChildReport const report = fp::wait(std::move(child)).expect("the test waits for its child");
    CRUCIBLE_TEST_ASSERT(test, report.is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(test, fp::probe_process(test, child_id) == fp::ProcessState::gone);
    CRUCIBLE_TEST_ASSERT(test, fp::probe_process(test, kIdAboveTheLimit) == fp::ProcessState::gone);
    // The kernel gives einval for the id zero and for a negative word.
    CRUCIBLE_TEST_ASSERT(test, fp::probe_process(test, fp::ProcessId{}) == fp::ProcessState::unknown);
    CRUCIBLE_TEST_ASSERT(test, fp::probe_process(test, kNegativeId) == fp::ProcessState::unknown);
}

// An open that the kernel refuses for a process that it holds is alive, and
// each error other than esrch and eperm is unknown.
void probe_judges_each_error(::fixy::TestRunnerCtx const& test) {
    using ::foundation::os::Errno;
    CRUCIBLE_TEST_ASSERT(test, fp::detail::judge_probe_error_(Errno::esrch) == fp::ProcessState::gone);
    CRUCIBLE_TEST_ASSERT(test, fp::detail::judge_probe_error_(Errno::eperm) == fp::ProcessState::alive);
    CRUCIBLE_TEST_ASSERT(test, fp::detail::judge_probe_error_(Errno::einval) == fp::ProcessState::unknown);
    CRUCIBLE_TEST_ASSERT(test, fp::detail::judge_probe_error_(Errno::enoent) == fp::ProcessState::unknown);
    CRUCIBLE_TEST_ASSERT(test, fp::detail::judge_probe_error_(Errno::emfile) == fp::ProcessState::unknown);
}

// The effective user id of the line Uid of /proc/PID/status, or no value.
// The line holds the real, the effective, the saved and the file system user
// ids, each after a tab.  The open of a directory follows no symbolic link, so
// the path names the process by its id and not by the link /proc/self.
[[nodiscard]] ::fixy::Option<std::uint32_t> status_effective_user(::fixy::TestRunnerCtx const& test) {
    ::foundation::os::PathText<::fixy::kPathBytes> path{};
    path.append(::fixy::TextView{"/proc/"}).expect("the path of the process fits a path");
    path.append_decimal(std::to_underlying(fp::current_process(test))).expect("the path of the process fits a path");
    ::fixy::fs::DirectoryHandle const self =
        ::fixy::fs::open_directory(
            test, ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(path.c_text().view()))
                      .expect("the path of the process passes the sanitizer"))
            .expect("the directory of the process opens");
    ::fixy::GrowText status = ::fixy::mint_grow_text(kStatusBytes);
    if (::fixy::fs::read_whole_at(test, ::foundation::effects::testing::test().alloc, self.ref(), "status", status)
            .is_err()) {
        return ::fixy::none;
    }
    return status.with_view([](::fixy::TextView text) -> ::fixy::Option<std::uint32_t> {
        constexpr ::fixy::TextView kUidLine{"\nUid:\t"};
        std::size_t const line = ::fixy::find(text, kUidLine).value_or(text.size());
        if (line == text.size()) return ::fixy::none;
        ::fixy::TextView const fields =
            text.window(line + kUidLine.size(), text.size() - line - kUidLine.size()).value_or(::fixy::TextView{});
        std::size_t const real_end = ::fixy::find(fields, '\t').value_or(fields.size());
        if (real_end == fields.size()) return ::fixy::none;
        ::fixy::TextView const rest =
            fields.window(real_end + 1, fields.size() - real_end - 1).value_or(::fixy::TextView{});
        std::size_t const effective_end = ::fixy::find(rest, '\t').value_or(rest.size());
        return ::fixy::parse<std::uint32_t>(rest.window(0, effective_end).value_or(::fixy::TextView{})).ok();
    });
}

// THE IDENTITY DOOR of fixy/os/Process.h: the effective user id of the door is
// the effective user id of the status of the process in /proc.
void current_user_is_the_effective_user(::fixy::TestRunnerCtx const& test) {
    ::fixy::Option<std::uint32_t> oracle = status_effective_user(test);
    CRUCIBLE_TEST_ASSERT(test, oracle.is_some());
    CRUCIBLE_TEST_ASSERT(test, std::to_underlying(fp::current_user(test)) == std::move(oracle).value_or(0xFFFFFFFFU));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    child_exits_with_its_code(test);
    child_body_returns(test);
    child_aborts(test);
    child_is_not_dumpable(test);
    child_writes_more_than_the_report(test);
    child_with_no_wait(test);
    decode_tells_the_core_dump(test);
    empty_child_names_no_process(test);
    child_ends_within_the_limit(test);
    child_on_a_full_pipe_ends_at_the_limit(test);
    traced_child_stops_at_its_system_calls(test);
    traced_child_gives_a_text_of_its_memory(test);
    traced_child_ends_by_its_signal(test);
    traced_child_ends_at_the_limit(test);
    traced_child_with_no_wait(test);
    empty_traced_child_names_no_process(test);
    probe_tells_the_state_of_a_process(test);
    probe_judges_each_error(test);
    current_user_is_the_effective_user(test);
    ::fixy::check::pass(test, "test_child_process: PASSED\n");
    return 0;
}
