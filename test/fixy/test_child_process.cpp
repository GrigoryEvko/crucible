// The child door of fixy/os/Process.h: a child that exits with a code, a
// child whose body returns, a child that aborts, a child that is not
// dumpable, a child whose output is
// larger than the report and than a pipe, a Child that no wait takes, the
// decode of a wait, and the empty Child.
// The report holds the end of the child and the last bytes of its standard
// error.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Process.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Abort.h>
#include <foundation/os/ChildProcess.h>
#include <foundation/Quarantine.h>

#include "../test_assert.h"

#include <cstdint>
#include <utility>

#include <sys/prctl.h>

namespace {

namespace fp = ::fixy::process;

using ::crucible::test::pass;

// The text of each line of the large output.
constexpr ::fixy::TextView kLongLine{
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"};

// The count of the lines of the large output.  With the last line of four
// characters, the output has about 40,000 bytes: more than the report holds.
// The child with no wait writes two times as many, more than a pipe of 64 KiB
// holds.
constexpr int kLongLines = 400;

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the test reads the dumpable flag that the child door gave the child")
namespace {

// The dumpable flag of the calling process (PR_GET_DUMPABLE): 0 when the
// kernel writes no core dump of it, or 1 when it does.
[[nodiscard]] int dumpable_flag_of_this_process() noexcept { return ::prctl(PR_GET_DUMPABLE, 0, 0, 0, 0); }

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

template <class Body>
[[nodiscard]] fp::ChildReport run_child(::fixy::TestRunnerCtx const& test, Body&& body) {
    fp::Child child = fp::spawn_child(test, std::forward<Body>(body)).expect("the test starts a child");
    return fp::wait(std::move(child)).expect("the test waits for its child");
}

void child_exits_with_its_code(::fixy::TestRunnerCtx const& test) {
    fp::ChildReport const report = run_child(test, [&test] noexcept {
        ::fixy::report(::fixy::Sink::Err, "the child writes this line\n");
        fp::exit_now(test, 3);
    });
    assert(report.is_exit_with(3));
    assert(!report.is_signaled_by(::foundation::os::kAbortSignal));
    assert(report.output() == ::fixy::TextView{"the child writes this line\n"});
    assert(report.dropped_bytes() == 0);
}

void child_body_returns(::fixy::TestRunnerCtx const& test) {
    fp::ChildReport const report = run_child(test, [] noexcept {});
    assert(report.is_exit_with(0));
    assert(report.output().size() == 0);
}

void child_aborts(::fixy::TestRunnerCtx const& test) {
    fp::ChildReport const report = run_child(test, [] noexcept { ::fixy::fatal("the child ends here"); });
    assert(report.is_signaled_by(::foundation::os::kAbortSignal));
    assert(::fixy::find(report.output(), ::fixy::TextView{"the child ends here"}).is_some());
    // THE CORE DUMP of foundation/os/ChildProcess.h: the kernel writes no
    // core dump of a child of the door, also with no core limit.
    assert(!report.end().has_core_dump);
}

// A child of the door is not dumpable, so no host writes a core dump of it
// (THE CORE DUMP of foundation/os/ChildProcess.h).  The child exits with the
// dumpable flag that the kernel gives it.
void child_is_not_dumpable(::fixy::TestRunnerCtx const& test) {
    fp::ChildReport const report = run_child(
        test, [&test] noexcept { fp::exit_now(test, static_cast<std::uint8_t>(dumpable_flag_of_this_process())); });
    assert(report.is_exit_with(0));
}

void child_writes_more_than_the_report(::fixy::TestRunnerCtx const& test) {
    fp::ChildReport const report = run_child(test, [] noexcept {
        for (int line = 0; line < kLongLines; ++line) {
            ::fixy::report(::fixy::Sink::Err, "{}\n", kLongLine);
        }
        ::fixy::report(::fixy::Sink::Err, "END\n");
    });
    constexpr std::uint64_t total = std::uint64_t{kLongLines} * (kLongLine.size() + 1) + 4;
    assert(report.is_exit_with(0));
    assert(report.output().size() == fp::kChildOutputCapacity);
    assert(report.dropped_bytes() == total - fp::kChildOutputCapacity);
    assert(::fixy::ends_with(report.output(), ::fixy::TextView{"x\nEND\n"}));
}

// The destructor of a Child that no wait took reads its output and waits for
// it, so a child that writes more than a pipe holds does not block the test.
void child_with_no_wait(::fixy::TestRunnerCtx const& test) {
    fp::Child child = fp::spawn_child(test, [] noexcept {
                          for (int line = 0; line < 2 * kLongLines; ++line) {
                              ::fixy::report(::fixy::Sink::Err, "{}\n", kLongLine);
                          }
                      }).expect("the test starts a child");
    assert(static_cast<std::uint32_t>(child.id()) > 0);
}

// The decode of a status word of waitpid gives the end of a wait.  On Linux,
// the bits 8 to 15 of the word hold the exit code, the low seven bits hold
// the signal, and the bit 0x80 tells that the kernel wrote a core dump.  No
// child of the door writes a core dump, so only the decode of a word shows
// the bit.
void decode_tells_the_core_dump() {
    namespace os = ::foundation::os;
    os::ChildEnd const exited = os::decode_child_end(0x0300);
    assert(exited.kind == os::ChildEndKind::exited);
    assert(exited.number == 3);
    assert(!exited.has_core_dump);

    os::ChildEnd const aborted = os::decode_child_end(0x0006);
    assert(aborted.kind == os::ChildEndKind::signaled);
    assert(aborted.number == os::kAbortSignal);
    assert(!aborted.has_core_dump);

    os::ChildEnd const dumped = os::decode_child_end(0x0086);
    assert(dumped.kind == os::ChildEndKind::signaled);
    assert(dumped.number == os::kAbortSignal);
    assert(dumped.has_core_dump);

    // The signal 11 is SIGSEGV.
    os::ChildEnd const faulted = os::decode_child_end(0x008b);
    assert(faulted.kind == os::ChildEndKind::signaled);
    assert(faulted.number == 11);
    assert(faulted.has_core_dump);
}

// The empty Child names no process, so a wait for it gives echild and waits
// for no child of the test.
void empty_child_names_no_process() {
    fp::Child empty{};
    assert(static_cast<std::uint32_t>(empty.id()) == 0);
    assert(fp::wait(std::move(empty)).err().value_or(::foundation::os::Errno::eio) == ::foundation::os::Errno::echild);
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
    decode_tells_the_core_dump();
    empty_child_names_no_process();
    pass("test_child_process: PASSED\n");
    return 0;
}
