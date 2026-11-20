// The temporary directory door of fixy/os/Fs.h (THE TEMPORARY DIRECTORY):
// mint_temporary_directory makes a new directory under the temporary
// directory of the host, and the end of its owner removes the tree.
//
// The cases read the name of a new directory, fill the directory and end its
// owner, mint two directories, take the next name when an entry holds a name,
// and move an owner.  CMake runs the test two times: with no TMPDIR, so the
// host directory is /tmp, and with TMPDIR set to the directory of the test.

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
namespace src = ::fixy::tags::source;
using ::foundation::os::Errno;

using Path = ::foundation::os::PathText<::fixy::kPathBytes>;
using WriteTruncate = ::fixy::atom::fs::mode<fs::open_mode::WriteTruncate>;

// The context of each door of the test: the file calls and the read of the
// process id.
constexpr ::fixy::TestRunnerCtx kCtx{eff::testing::test()};

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

[[nodiscard]] fs::TemporaryDirectory make_owner() {
    return std::move(fs::mint_temporary_directory(kCtx).expect("the temporary directory opens")).consume();
}

// The temporary directory of the host: the path of TMPDIR, or /tmp.
[[nodiscard]] Path host_directory() {
    Path host{};
    fixy::env::switches().temporary_directory.match(
        [&host](fixy::env::TemporaryDirectoryPath checked) {
            host.append(checked.c_text()).expect("the path of TMPDIR fits a path");
        },
        [&host] { host.append(fixy::TextView{"/tmp"}).expect("/tmp fits a path"); });
    return host;
}

// The path of a new directory without its count: the host directory, a
// slash, crucible-, the id of the process and a dash.
[[nodiscard]] Path name_prefix() {
    Path prefix = host_directory();
    prefix.append(fixy::TextView{"/crucible-"}).expect("the prefix fits a path");
    prefix.append_decimal(std::to_underlying(fixy::process::current_process(kCtx))).expect("the id fits a path");
    prefix.append('-').expect("the prefix fits a path");
    return prefix;
}

// The count of the name of a new directory, or no value when its path does not
// start with name_prefix().
[[nodiscard]] fixy::Option<std::uint64_t> count_of(fs::TemporaryDirectory const& owner) {
    Path const prefix = name_prefix();
    fixy::TextView const path = owner.path().view();
    if (!fixy::starts_with(path, prefix.c_text().view())) return fixy::none;
    fixy::TextView const digits =
        path.window(prefix.size(), path.size() - prefix.size()).expect("the path holds its prefix");
    return fixy::parse<std::uint64_t>(digits).ok();
}

// True when an entry is at the absolute path.
[[nodiscard]] bool holds_path(fixy::CText path) { return fs::has_entry(kCtx, path).value_or(true); }

// A new directory has the name of the door, and its owner can fill it.  The
// end of the owner removes the files and the directories below it, and then
// the directory.
[[nodiscard]] int check_owner_removes_its_tree() {
    Path path{};
    {
        fs::TemporaryDirectory const owner = make_owner();
        if (!owner.is_open() || count_of(owner).is_none()) return 1;
        path.append(owner.path()).expect("the path of the owner fits a path");
        if (!holds_path(path.c_text())) return 2;
        if (fs::make_directory_at(kCtx, owner.ref(), "inner").is_err()) return 3;
        bool const is_written = fs::mint_file_at<WriteTruncate>(kCtx, owner.ref(), "inner/file")
                                    .match(
                                        [](fixy::Linear<fs::FileOf<WriteTruncate>> opened) {
                                            fs::FileOf<WriteTruncate> const file = std::move(opened).consume();
                                            return fs::write_full(kCtx, file.ref(), fixy::TextView{"bytes"}).is_ok();
                                        },
                                        [](Errno) { return false; });
        if (!is_written) return 4;
        fs::DirectoryHandle const reopened =
            fs::open_directory(kCtx, sanitized(path.c_text().view())).expect("the path of the owner opens");
        if (!fs::has_entry_at(kCtx, reopened.ref(), "inner/file").value_or(false)) return 5;
    }
    if (holds_path(path.c_text())) return 6;
    return 0;
}

// Two owners at one time hold two directories with two names.
[[nodiscard]] int check_two_mints_get_two_names() {
    fs::TemporaryDirectory const first = make_owner();
    fs::TemporaryDirectory const second = make_owner();
    if (first.path().view() == second.path().view()) return 11;
    if (count_of(first).value_or(0) + 1 != count_of(second).value_or(0)) return 12;
    return 0;
}

// A name that an entry holds already makes the mint take the next count.
[[nodiscard]] int check_name_in_use_takes_the_next_count() {
    fs::TemporaryDirectory const before = make_owner();
    std::uint64_t const count = count_of(before).value_or(0);
    Path taken = name_prefix();
    taken.append_decimal(count + 1).expect("the name fits a path");
    Path const host_path = host_directory();
    fs::DirectoryHandle const host =
        fs::open_directory(kCtx, sanitized(host_path.c_text().view())).expect("the host directory opens");
    Path name{};
    name.append(fixy::TextView{"crucible-"}).expect("the name fits");
    name.append_decimal(std::to_underlying(fixy::process::current_process(kCtx))).expect("the name fits");
    name.append('-').expect("the name fits");
    name.append_decimal(count + 1).expect("the name fits");
    if (fs::make_directory_at(kCtx, host.ref(), name.c_text()).is_err()) return 21;
    int failed = 0;
    {
        fs::TemporaryDirectory const after = make_owner();
        if (count_of(after).value_or(0) != count + 2) failed = 22;
        if (!holds_path(taken.c_text())) failed = 23;
    }
    if (fs::remove_tree_at(kCtx, host.ref(), name.c_text()).is_err() && failed == 0) failed = 24;
    return failed;
}

// A move takes the directory, and the end of the source removes nothing.  A
// move assignment removes the tree of the target first.
[[nodiscard]] int check_moves_take_the_directory() {
    fs::TemporaryDirectory source = make_owner();
    Path first_path{};
    first_path.append(source.path()).expect("the path of the owner fits a path");
    fs::TemporaryDirectory target = std::move(source);
    if (source.is_open() || !target.is_open()) return 31;
    if (!holds_path(first_path.c_text())) return 32;
    fs::TemporaryDirectory other = make_owner();
    Path second_path{};
    second_path.append(other.path()).expect("the path of the owner fits a path");
    target = std::move(other);
    if (holds_path(first_path.c_text())) return 33;
    if (!holds_path(second_path.c_text()) || other.is_open()) return 34;
    if (target.path().view() != second_path.c_text().view()) return 35;
    return 0;
}

}  // namespace

int main() {
    int failed = check_owner_removes_its_tree();
    if (failed == 0) failed = check_two_mints_get_two_names();
    if (failed == 0) failed = check_name_in_use_takes_the_next_count();
    if (failed == 0) failed = check_moves_take_the_directory();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_temporary_directory: check {} failed\n", failed);
        return failed;
    }
    fixy::report(fixy::Sink::Out, "test_os_temporary_directory: passed\n");
    return 0;
}
