// The sweep of the temporary trees of fixy/os/Fs.h (THE SWEEP).
//
// The setup test test_os_temporary_sweep_setup makes the directory
// temporary_sweep_fixture, and CMake sets TMPDIR to it.  The fixture holds a
// tree of an ended process, a link of the form to a directory, a file of the
// form, and directories of names that do not have the form.  The test adds a
// tree of a child that it did not wait for and a tree of its own mint.
//
// The first sweep removes only the tree of the ended process.  The link, the
// target of the link, the file, each other name, the tree of the child and the
// tree of the mint stay.  After the wait for the child, a second sweep removes
// the tree of the child.  Only this test uses the fixture, so the counts of
// each report are exact.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Environment.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <fixy/os/Process.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace fs = ::fixy::fs;
namespace fp = ::fixy::process;
namespace src = ::fixy::tags::source;

using Path = ::foundation::os::PathText<::fixy::kPathBytes>;

// The context of each door of the test: the file calls, the identity reads
// and the child door.
constexpr ::fixy::TestRunnerCtx kCtx{eff::testing::test()};

// The entries of the fixture that each sweep keeps.
constexpr ::fixy::FixedArray<::fixy::CText, 12> kKeptNames{
    "crucible-4194305-2",    "target",
    "target/keep",           "crucible-4194305-6",
    "crucible-4194305-007",  "crucible-0x400001-1",
    "crucible-4194305",      "crucible-4194305-3-4",
    "crucible-4294967296-1", "scratch-4194305-5",
    "crucible-0-1",          "crucible-2147483648-1",
};

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

// The fixture directory, which CMake gives as TMPDIR.
[[nodiscard]] fs::DirectoryHandle open_fixture() {
    Path host{};
    fixy::env::switches().temporary_directory.match(
        [&host](fixy::env::TemporaryDirectoryPath checked) {
            host.append(checked.c_text()).expect("the path of TMPDIR fits a path");
        },
        [] { fixy::fatal("test_os_temporary_sweep: CMake sets TMPDIR to the fixture directory"); });
    return fs::open_directory(kCtx, sanitized(host.c_text().view())).expect("the setup test made the fixture");
}

// The name of a tree of the mint for the process: crucible-PROCESS-0.
[[nodiscard]] Path tree_name_of(fp::ProcessId process) {
    Path name{};
    name.append(fixy::TextView{"crucible-"}).expect("the name fits a path");
    name.append_decimal(std::to_underlying(process)).expect("the name fits a path");
    name.append(fixy::TextView{"-0"}).expect("the name fits a path");
    return name;
}

// True when an entry is at path under dir.  An error counts as an entry, so a
// check that an entry is gone fails on it.
[[nodiscard]] bool holds_entry(fs::DirectoryRef<fs::directory_rights> dir, fixy::CText path) {
    return fs::has_entry_at(kCtx, dir, path).value_or(true);
}

// The count of the kept names that are gone, or zero.
[[nodiscard]] std::size_t count_missing_kept(fs::DirectoryRef<fs::directory_rights> dir) {
    std::size_t missing = 0;
    for (fixy::CText const name : kKeptNames) {
        if (!fs::has_entry_at(kCtx, dir, name).value_or(false)) ++missing;
    }
    return missing;
}

[[nodiscard]] int check_sweep() {
    fs::DirectoryHandle const fixture = open_fixture();
    if (!holds_entry(fixture.ref(), "crucible-4194305-1/inner/file")) return 1;
    if (count_missing_kept(fixture.ref()) != 0) return 2;

    fp::Child child = fp::spawn_child(kCtx, [] noexcept {}).expect("the test starts a child");
    Path const child_tree = tree_name_of(child.id());
    if (fs::make_directory_at(kCtx, fixture.ref(), child_tree.c_text()).is_err()) return 3;
    fs::TemporaryDirectory const own =
        std::move(fs::mint_temporary_directory(kCtx).expect("the mint makes a tree")).consume();
    Path own_path{};
    own_path.append(own.path()).expect("the path of the mint fits a path");

    // The child is not waited for, so it holds its id and its tree stays.
    fs::SweepReport const first = fs::sweep_temporary_directories(kCtx).expect("the sweep lists the fixture");
    if (first.removed != 1 || first.failed != 0) return 4;
    if (holds_entry(fixture.ref(), "crucible-4194305-1")) return 5;
    if (count_missing_kept(fixture.ref()) != 0) return 6;
    if (!holds_entry(fixture.ref(), child_tree.c_text())) return 7;
    if (!fs::has_entry(kCtx, own_path.c_text()).value_or(false)) return 8;

    // After the wait the id of the child is gone, so its tree goes.
    fp::ChildReport const report = fp::wait(std::move(child)).expect("the test waits for its child");
    if (!report.is_exit_with(0)) return 9;
    fs::SweepReport const second = fs::sweep_temporary_directories(kCtx).expect("the sweep lists the fixture");
    if (second.removed != 1 || second.failed != 0) return 10;
    if (holds_entry(fixture.ref(), child_tree.c_text())) return 11;
    if (count_missing_kept(fixture.ref()) != 0) return 12;
    if (!fs::has_entry(kCtx, own_path.c_text()).value_or(false)) return 13;
    return 0;
}

}  // namespace

int main() {
    int const failed = check_sweep();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_temporary_sweep: check {} failed\n", failed);
        return failed;
    }
    fixy::report(fixy::Sink::Out, "test_os_temporary_sweep: passed\n");
    return 0;
}
