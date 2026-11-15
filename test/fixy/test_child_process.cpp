// The child door of fixy/os/Process.h: a child that exits with a code, a
// child whose body returns, a child that aborts, a child whose output is
// larger than the report and than a pipe, a Child that no wait takes, and the
// empty Child.
// The report holds the end of the child and the last bytes of its standard
// error.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Process.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Abort.h>

#include "../test_assert.h"

#include <cstdint>
#include <utility>

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
    child_writes_more_than_the_report(test);
    child_with_no_wait(test);
    empty_child_names_no_process();
    pass("test_child_process: PASSED\n");
    return 0;
}
