// The write fault of a test of fixy/os/Fs.h: WriteFaultScope::arm makes the
// writes of write_full on the calling thread fail after a budget of bytes,
// with a stated errno, until the scope ends.
//
// The cases fail a write with no budget, cut a write at its budget, let the
// writes pass after the end of the scope, reach a test context that a caller
// narrowed to a row with no Test, write through a live scope with a context of
// the background source, and end the process at a second arm.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace fs = ::fixy::fs;
using ::foundation::core::ByteCount;
using ::foundation::os::Errno;

// The context of the test.
constexpr ::fixy::TestRunnerCtx kTest{eff::testing::test()};

// The row of the file calls of a store: a test context and a background
// context narrowed to it.
using FileRow = eff::Row<eff::Effect::IO, eff::Effect::Block>;
using TestFileCtx = eff::ExecCtx<eff::Test, FileRow>;
using BgFileCtx = eff::ExecCtx<eff::Bg, FileRow>;

using WriteTruncate = ::fixy::atom::fs::mode<fs::open_mode::WriteTruncate>;
using WriteFile = fs::FileOf<WriteTruncate>;

constexpr ::fixy::TextView kText{"twelve bytes"};

[[nodiscard]] fs::TemporaryDirectory make_owner() {
    return std::move(fs::mint_temporary_directory(kTest).expect("the temporary directory opens")).consume();
}

// The file name under dir, open for writes from its start.
[[nodiscard]] WriteFile open_file(fs::TemporaryDirectory const& dir, ::fixy::CText name) {
    return fs::mint_file_at<WriteTruncate>(kTest, dir.ref(), name).expect("the test opens its file").consume();
}

// True when the whole file name under dir is text.
[[nodiscard]] bool holds_text(fs::TemporaryDirectory const& dir, ::fixy::CText name, ::fixy::TextView text) {
    ::fixy::GrowText read = ::fixy::mint_grow_text(64);
    bool const is_read = fs::read_whole_at(kTest, kTest.cap().alloc, dir.ref(), name, read).is_ok();
    return is_read && read.with_view([text](::fixy::TextView held) noexcept { return held == text; });
}

// True when the write gave the error.
[[nodiscard]] bool is_error(::fixy::Result<::fixy::Unit, Errno> const& written, Errno error) {
    return written.is_err_and([error](Errno const& given) noexcept { return given == error; });
}

// A fault with no budget fails the next write with its errno, and the write
// puts no byte in the file.
void test_no_budget_fails_the_next_write() {
    fs::TemporaryDirectory const dir = make_owner();
    WriteFile const file = open_file(dir, "file");
    {
        fs::WriteFaultScope const fault = fs::WriteFaultScope::arm(kTest, ByteCount{0}, fs::WriteFailure::device_error);
        CRUCIBLE_TEST_ASSERT(kTest, is_error(fs::write_full(kTest, file.ref(), kText), Errno::eio));
    }
    CRUCIBLE_TEST_ASSERT(kTest, holds_text(dir, "file", ::fixy::TextView{}));
    ::fixy::check::pass(kTest, "  no_budget_fails_the_next_write: PASSED\n");
}

// A write that crosses the budget takes the bytes up to it, and gives the
// errno.  Each later write gives the errno and takes no byte.  After the end
// of the scope, a write passes.
void test_budget_cuts_a_write() {
    fs::TemporaryDirectory const dir = make_owner();
    WriteFile const file = open_file(dir, "file");
    {
        fs::WriteFaultScope const fault = fs::WriteFaultScope::arm(kTest, ByteCount{5}, fs::WriteFailure::no_space);
        CRUCIBLE_TEST_ASSERT(kTest, is_error(fs::write_full(kTest, file.ref(), kText), Errno::enospc));
        CRUCIBLE_TEST_ASSERT(kTest, holds_text(dir, "file", ::fixy::TextView{"twelv"}));
        CRUCIBLE_TEST_ASSERT(kTest, is_error(fs::write_full(kTest, file.ref(), kText), Errno::enospc));
        CRUCIBLE_TEST_ASSERT(kTest, holds_text(dir, "file", ::fixy::TextView{"twelv"}));
    }
    CRUCIBLE_TEST_ASSERT(kTest, fs::write_full(kTest, file.ref(), ::fixy::TextView{"e bytes"}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, holds_text(dir, "file", kText));
    ::fixy::check::pass(kTest, "  budget_cuts_a_write:            PASSED\n");
}

// A test context that a caller narrows to a row with no Test keeps the test
// source, so its writes take the fault.  A context of the background source
// writes each byte through the live scope.
void test_source_picks_the_fault() {
    fs::TemporaryDirectory const dir = make_owner();
    WriteFile const narrowed_file = open_file(dir, "narrowed");
    WriteFile const background_file = open_file(dir, "background");
    TestFileCtx const narrowed = kTest.in_row<FileRow>();
    BgFileCtx const background{eff::testing::bg()};
    {
        fs::WriteFaultScope const fault = fs::WriteFaultScope::arm(kTest, ByteCount{0}, fs::WriteFailure::no_space);
        CRUCIBLE_TEST_ASSERT(kTest, is_error(fs::write_full(narrowed, narrowed_file.ref(), kText), Errno::enospc));
        CRUCIBLE_TEST_ASSERT(kTest, fs::write_full(background, background_file.ref(), kText).is_ok());
    }
    CRUCIBLE_TEST_ASSERT(kTest, holds_text(dir, "narrowed", ::fixy::TextView{}));
    CRUCIBLE_TEST_ASSERT(kTest, holds_text(dir, "background", kText));
    ::fixy::check::pass(kTest, "  source_picks_the_fault:         PASSED\n");
}

// A second arm while a scope lives ends the process, because the second fault
// would hide the first.
void test_second_arm_ends_the_process() {
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            fs::WriteFaultScope const first = fs::WriteFaultScope::arm(kTest, ByteCount{0}, fs::WriteFailure::no_space);
            fs::WriteFaultScope const second =
                fs::WriteFaultScope::arm(kTest, ByteCount{0}, fs::WriteFailure::device_error);
        },
        "the thread holds a fault, so a second arm would hide the first");
    ::fixy::check::pass(kTest, "  second_arm_ends_the_process:    PASSED\n");
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_os_fs_write_fault\n");
    test_no_budget_fails_the_next_write();
    test_budget_cuts_a_write();
    test_source_picks_the_fault();
    test_second_arm_ends_the_process();
    return 0;
}
