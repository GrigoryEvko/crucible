// The tree removal of fixy/os/Fs.h (the tree removal): remove_tree_at and
// remove_entries_below.
//
// The setup test test_os_remove_tree_setup makes the directory
// remove_tree_fixture in the directory of the test.  It holds the tree tree,
// with a link to a directory and a link to a file outside the tree.  The
// removal takes the links, and their targets stay.
//
// Each other case builds a tree of its own name under remove_tree_fixture,
// removes it, and reads back which entries stay.  The cases cover a nested
// tree, a directory with more entries than one buffer of a listing, a file and
// a missing entry, the paths that name the directory itself, a path that
// leaves the directory, the borrow of an empty handle, and the bound of the
// depth.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
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

// The context of each door of the test.
constexpr ::fixy::TestRunnerCtx kCtx{eff::testing::test()};

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

// The directory that the setup test made, which each case builds under.
[[nodiscard]] fs::DirectoryHandle open_root() {
    return fs::open_directory(kCtx, sanitized("remove_tree_fixture")).expect("the setup test made the fixture tree");
}

// The path of part under base, with a slash between them.
[[nodiscard]] Path joined(Path base, fixy::TextView part) {
    base.append('/').expect("a test path fits");
    base.append(part).expect("a test path fits");
    return base;
}

// Writes a file of one line at path under dir.
[[nodiscard]] bool writes_file(fs::DirectoryRef<fs::directory_rights> dir, fixy::CText path) {
    return fs::mint_file_at<WriteTruncate>(kCtx, dir, path)
        .match(
            [](fixy::Linear<fs::FileOf<WriteTruncate>> opened) {
                fs::FileOf<WriteTruncate> const file = std::move(opened).consume();
                return fs::write_full(kCtx, file.ref(), fixy::TextView{"line\n"}).is_ok();
            },
            [](Errno) { return false; });
}

[[nodiscard]] bool makes_directory(fs::DirectoryRef<fs::directory_rights> dir, fixy::CText path) {
    return fs::make_directory_at(kCtx, dir, path).is_ok();
}

// True when an entry is at path under dir.  A failed read counts as an entry,
// so a check that wants no entry fails on it.
[[nodiscard]] bool holds_entry(fs::DirectoryRef<fs::directory_rights> dir, fixy::CText path) {
    return fs::has_entry_at(kCtx, dir, path).value_or(true);
}

// A tree of three levels with a file at each level goes, and the root of the
// test stays.
[[nodiscard]] int check_removes_a_nested_tree() {
    fs::DirectoryHandle const root = open_root();
    if (!makes_directory(root.ref(), "nested") || !makes_directory(root.ref(), "nested/a")) return 1;
    if (!makes_directory(root.ref(), "nested/a/b")) return 2;
    if (!writes_file(root.ref(), "nested/f") || !writes_file(root.ref(), "nested/a/g")) return 3;
    if (!writes_file(root.ref(), "nested/a/b/h")) return 4;
    if (fs::remove_tree_at(kCtx, root.ref(), "nested").is_err()) return 5;
    if (holds_entry(root.ref(), "nested")) return 6;
    if (!root.is_open()) return 7;
    return 0;
}

// The count of the entries of the wide directory.  A name of 40 characters
// takes a record of 64 bytes in a listing, so 300 entries need five buffers
// of 4096 bytes.
constexpr std::size_t kWideEntries = 300;

// A directory with more entries than one buffer of a listing, and with
// directories that hold entries between the files, goes empty, and it stays.
[[nodiscard]] int check_empties_a_wide_directory() {
    fs::DirectoryHandle const root = open_root();
    if (!makes_directory(root.ref(), "wide")) return 11;
    for (std::size_t index = 0; index < kWideEntries; ++index) {
        Path name{};
        name.append(fixy::TextView{"wide/entry-of-a-long-name-for-the-buffer-"}).expect("a test path fits");
        name.append_decimal(index).expect("a test path fits");
        bool const is_directory = index % 50 == 7;
        if (is_directory) {
            if (!makes_directory(root.ref(), name.c_text())) return 12;
            Path const inner = joined(name, fixy::TextView{"inner"});
            if (!writes_file(root.ref(), inner.c_text())) return 13;
        } else if (!writes_file(root.ref(), name.c_text())) {
            return 14;
        }
    }
    fs::DirectoryHandle const wide = fs::open_directory_at(kCtx, root.ref(), "wide").expect("the wide directory opens");
    if (fs::remove_entries_below(kCtx, wide.ref()).is_err()) return 15;
    if (!holds_entry(root.ref(), "wide")) return 16;
    for (std::size_t index = 0; index < kWideEntries; ++index) {
        Path name{};
        name.append(fixy::TextView{"wide/entry-of-a-long-name-for-the-buffer-"}).expect("a test path fits");
        name.append_decimal(index).expect("a test path fits");
        if (holds_entry(root.ref(), name.c_text())) return 17;
    }
    if (fs::remove_tree_at(kCtx, root.ref(), "wide").is_err()) return 18;
    if (holds_entry(root.ref(), "wide")) return 19;
    return 0;
}

// A file goes as an unlink removes it, and a missing entry gives enoent.
[[nodiscard]] int check_removes_a_file_and_refuses_a_missing_entry() {
    fs::DirectoryHandle const root = open_root();
    if (!writes_file(root.ref(), "lone-file")) return 21;
    if (fs::remove_tree_at(kCtx, root.ref(), "lone-file").is_err()) return 22;
    if (holds_entry(root.ref(), "lone-file")) return 23;
    if (fs::remove_tree_at(kCtx, root.ref(), "lone-file").err().value_or(Errno{}) != Errno::enoent) return 24;
    return 0;
}

// A path that names the directory itself gives einval and removes nothing.
// A path that leaves the directory gives exdev.  The borrow of an empty handle
// gives ebadf.
[[nodiscard]] int check_refuses_the_paths_that_name_no_entry() {
    fs::DirectoryHandle const root = open_root();
    if (!writes_file(root.ref(), "kept-file")) return 31;
    if (fs::remove_tree_at(kCtx, root.ref(), ".").err().value_or(Errno{}) != Errno::einval) return 32;
    if (fs::remove_tree_at(kCtx, root.ref(), "..").err().value_or(Errno{}) != Errno::einval) return 33;
    if (fs::remove_tree_at(kCtx, root.ref(), "//").err().value_or(Errno{}) != Errno::einval) return 34;
    if (!holds_entry(root.ref(), "kept-file")) return 35;
    if (fs::remove_tree_at(kCtx, root.ref(), "../kept-file").err().value_or(Errno{}) != Errno::exdev) return 36;
    fs::DirectoryHandle emptied = fs::open_directory_at(kCtx, root.ref(), ".").expect("the root opens again");
    fs::DirectoryHandle const taken = std::move(emptied);
    if (fs::remove_tree_at(kCtx, emptied.ref(), "kept-file").err().value_or(Errno{}) != Errno::ebadf) return 37;
    if (fs::remove_entries_below(kCtx, emptied.ref()).err().value_or(Errno{}) != Errno::ebadf) return 38;
    if (!taken.is_open() || !holds_entry(root.ref(), "kept-file")) return 39;
    return 0;
}

// A chain of directories deeper than the bound of the walk, with a file at its
// end, gives enametoolong.  A removal from a directory in the middle of the
// chain then takes the lower part, and a removal of the top takes the rest.
[[nodiscard]] int check_bounds_the_depth() {
    constexpr std::size_t kChain = fs::kRemoveTreeDepth + 3;
    constexpr std::size_t kMiddle = 64;
    fs::DirectoryHandle const root = open_root();
    Path chain{};
    Path middle{};
    for (std::size_t level = 0; level < kChain; ++level) {
        if (level != 0) chain.append('/').expect("a test path fits");
        chain.append('d').expect("a test path fits");
        if (level + 1 == kMiddle) middle = chain;
        if (!makes_directory(root.ref(), chain.c_text())) return 41;
    }
    Path const bottom = joined(chain, fixy::TextView{"file"});
    if (!writes_file(root.ref(), bottom.c_text())) return 42;
    if (fs::remove_tree_at(kCtx, root.ref(), "d").err().value_or(Errno{}) != Errno::enametoolong) return 43;
    if (!holds_entry(root.ref(), bottom.c_text())) return 44;
    fs::DirectoryHandle const halfway =
        fs::open_directory_at(kCtx, root.ref(), middle.c_text()).expect("the middle of the chain opens");
    if (fs::remove_tree_at(kCtx, halfway.ref(), "d").is_err()) return 45;
    if (fs::remove_tree_at(kCtx, root.ref(), "d").is_err()) return 46;
    if (holds_entry(root.ref(), "d")) return 47;
    return 0;
}

// The fixture tree of the setup test: the removal takes a link to a directory
// outside the tree and a link to a file outside it, and each target stays.
[[nodiscard]] int check_keeps_the_targets_of_links() {
    fs::DirectoryHandle const fixture = open_root();
    if (!holds_entry(fixture.ref(), "tree/directory-link") || !holds_entry(fixture.ref(), "tree/file-link")) return 51;
    if (fs::remove_tree_at(kCtx, fixture.ref(), "tree").is_err()) return 52;
    if (holds_entry(fixture.ref(), "tree")) return 53;
    if (!holds_entry(fixture.ref(), "outside/keep")) return 54;
    return 0;
}

}  // namespace

int main() {
    int failed = check_removes_a_nested_tree();
    if (failed == 0) failed = check_empties_a_wide_directory();
    if (failed == 0) failed = check_removes_a_file_and_refuses_a_missing_entry();
    if (failed == 0) failed = check_refuses_the_paths_that_name_no_entry();
    if (failed == 0) failed = check_bounds_the_depth();
    if (failed == 0) failed = check_keeps_the_targets_of_links();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_remove_tree: check {} failed\n", failed);
        return failed;
    }
    fixy::report(fixy::Sink::Out, "test_os_remove_tree: passed\n");
    return 0;
}
