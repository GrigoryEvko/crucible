// Each door under a directory refuses a path that escapes the directory.
//
// A door under a Dirfd reads its path beneath the directory of the Dirfd.
// The cases give the doors four kinds of path that leave the directory:
//
//   - an absolute path
//   - a ".." part that climbs above the directory
//   - a symbolic link in a middle part whose target is outside the
//     directory.  Each Linux host has /sys/class/net/lo, a relative link to
//     ../../devices/virtual/net/lo, and /dev/fd, an absolute link to
//     /proc/self/fd.
//   - a magic link of procfs in a middle part: root of /proc/self, which
//     names the root directory of the process.
//
// The kernel gives exdev for each of the first three, and eloop for the
// magic link.  A ".." part and a link that stay inside the directory are
// permitted.
//
// The directories of the cases are in /proc, /sys and /dev, which each Linux
// host has.  Each escape of an open names a file that a read can open, so a
// door that does not refuse the escape opens it, and the case fails.  Each
// escape of a door that makes, removes or renames an entry names an entry
// that is missing in procfs or sysfs, which make no entry, so such a door
// changes no file.  The target of each such escape is on the mount of the
// directory, so a rename that the kernel gets gives no exdev of its own.

#include <fixy/Core.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace src = fixy::tags::source;

namespace {

using foundation::os::Errno;

using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

using PathBuffer = foundation::os::PathText<fixy::kPathBytes>;

// The path of the parts, one after the other.
[[nodiscard]] PathBuffer joined(fixy::TextView first, fixy::TextView second = {}, fixy::TextView third = {}) {
    PathBuffer path{};
    path.append(first).expect("a path of the test fits its buffer");
    path.append(second).expect("a path of the test fits its buffer");
    path.append(third).expect("a path of the test fits its buffer");
    return path;
}

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

// The Dirfd of the directory at path, which must open.
[[nodiscard]] fs::Dirfd directory_at(fixy::TextView path) {
    IoBlockCtx const ctx{eff::testing::test()};
    return fs::open_dirfd(ctx, sanitized(path)).expect("each directory of the test opens");
}

// The error of a result, or the zero Errno for a result that holds a value.
template <typename T>
[[nodiscard]] Errno error_of(fixy::Result<T, Errno> const& result) {
    return result.err().value_or(Errno{});
}

// The error of an open, or the zero Errno for an open that gave a
// descriptor.  The match closes the descriptor of an open that succeeded.
[[nodiscard]] Errno open_error_of(fixy::Result<fixy::Linear<fs::OwnedFd>, Errno> minted) {
    return std::move(minted).match(
        [](fixy::Linear<fs::OwnedFd> opened) -> Errno {
            fs::OwnedFd const closed_on_return = std::move(opened).consume();
            return Errno{};
        },
        [](Errno error) -> Errno { return error; });
}

// Reports a door that did not give the expected error, and gives one.
[[nodiscard]] int refused_badly(fixy::TextView escape, fixy::TextView door, Errno given, Errno expected) {
    fixy::report(fixy::Sink::Err, "the {} escape: {} gave errno {}, and the refusal is errno {}\n", escape, door,
                 std::to_underlying(given), std::to_underlying(expected));
    return 1;
}

// The opens of one escape: mint_file_at and read_whole_at of the file, and
// open_dirfd_at of the directory.  Each gives expected.  It gives the count
// of the doors that did not.
[[nodiscard]] int opens_refuse(fixy::TextView escape, fs::Dirfd const& dir, PathBuffer const& file,
                               PathBuffer const& directory, Errno expected) {
    IoBlockCtx const ctx{eff::testing::test()};
    eff::Alloc const alloc = eff::testing::test().alloc;
    int failed = 0;
    if (Errno const given = open_error_of(fs::mint_file_at<fs::read_only>(ctx, dir, file.c_text()));
        given != expected) {
        failed += refused_badly(escape, "mint_file_at", given, expected);
    }
    foundation::core::GrowText text = foundation::core::mint_grow_text(4096);
    if (Errno const given = error_of(fs::read_whole_at(ctx, alloc, dir, file.c_text(), text)); given != expected) {
        failed += refused_badly(escape, "read_whole_at", given, expected);
    }
    if (Errno const given = error_of(fs::open_dirfd_at(ctx, dir, directory.c_text())); given != expected) {
        failed += refused_badly(escape, "open_dirfd_at", given, expected);
    }
    return failed;
}

// Each open refuses each escape.  The cases run to the end, so the report
// names each door that did not refuse an escape.  The absolute link goes
// through /dev/fd to the descriptor of a Dirfd of /proc/sys.
[[nodiscard]] int each_open_refuses_each_escape() {
    fs::Dirfd const proc_sys = directory_at("/proc/sys");
    fs::Dirfd const proc = directory_at("/proc");
    fs::Dirfd const net = directory_at("/sys/class/net");
    fs::Dirfd const dev = directory_at("/dev");
    PathBuffer descriptor{};
    descriptor.append_decimal(static_cast<std::uint64_t>(proc_sys.get())).expect("a number fits the buffer");
    fixy::TextView const number = descriptor.c_text().view();

    int failed = opens_refuse("absolute", proc_sys, joined("/proc/sys/kernel/ostype"), joined("/dev"), Errno::exdev);
    failed += opens_refuse("dot-dot", proc_sys, joined("kernel/../../sys/kernel/ostype"), joined(".."), Errno::exdev);
    failed += opens_refuse("symbolic link", net, joined("lo/address"), joined("lo/statistics"), Errno::exdev);
    failed += opens_refuse("absolute link", dev, joined("fd/", number, "/kernel/ostype"),
                           joined("fd/", number, "/kernel"), Errno::exdev);
    failed += opens_refuse("magic link", proc, joined("self/root/proc/sys/kernel/ostype"), joined("self/root/dev"),
                           Errno::eloop);
    return failed;
}

// The doors of one entry under one escape: unlink_at, make_directory_at and
// the two forms of commit_atomic_at take missing, a path of an entry that is
// not there, and has_entry_at takes present, a path of an entry that is
// there.  A commit takes the escape as its source, with the target
// no-target, and as its target, with the source no-entry.  Each door gives
// expected.  It gives the count of the doors that did not.
[[nodiscard]] int entry_doors_refuse(fixy::TextView escape, fs::Dirfd const& dir, PathBuffer const& missing,
                                     PathBuffer const& present, Errno expected) {
    IoBlockCtx const ctx{eff::testing::test()};
    using Replacing = fs::atomicity::Rename;
    using NoReplace = fs::atomicity::RenameAt2NoReplace;
    int failed = 0;
    if (Errno const given = error_of(fs::unlink_at(ctx, dir, missing.c_text())); given != expected) {
        failed += refused_badly(escape, "unlink_at", given, expected);
    }
    if (Errno const given = error_of(fs::make_directory_at(ctx, dir, missing.c_text())); given != expected) {
        failed += refused_badly(escape, "make_directory_at", given, expected);
    }
    if (Errno const given = error_of(fs::has_entry_at(ctx, dir, present.c_text())); given != expected) {
        failed += refused_badly(escape, "has_entry_at", given, expected);
    }
    if (Errno const given = error_of(fs::commit_atomic_at<Replacing>(ctx, dir, missing.c_text(), "no-target"));
        given != expected) {
        failed += refused_badly(escape, "commit_atomic_at<Rename> from the escape", given, expected);
    }
    if (Errno const given = error_of(fs::commit_atomic_at<Replacing>(ctx, dir, "no-entry", missing.c_text()));
        given != expected) {
        failed += refused_badly(escape, "commit_atomic_at<Rename> to the escape", given, expected);
    }
    if (Errno const given = error_of(fs::commit_atomic_at<NoReplace>(ctx, dir, missing.c_text(), "no-target"));
        given != expected) {
        failed += refused_badly(escape, "commit_atomic_at<RenameAt2NoReplace> from the escape", given, expected);
    }
    if (Errno const given = error_of(fs::commit_atomic_at<NoReplace>(ctx, dir, "no-entry", missing.c_text()));
        given != expected) {
        failed += refused_badly(escape, "commit_atomic_at<RenameAt2NoReplace> to the escape", given, expected);
    }
    return failed;
}

// Each door of one entry refuses each escape, and four more forms of a path
// that leaves the directory: a bare "..", a ".." part at the end, a slash at
// the end, and the name of an entry of "/".  The last form takes a Dirfd of
// "/", because the target of the escape must be on the mount of the
// directory.  /proc of the last form is a mount point, so a rename of it
// gives ebusy, and no door changes it.
[[nodiscard]] int each_entry_door_refuses_each_escape() {
    fs::Dirfd const proc_sys = directory_at("/proc/sys");
    fs::Dirfd const proc = directory_at("/proc");
    fs::Dirfd const net = directory_at("/sys/class/net");
    fs::Dirfd const root = directory_at("/");

    int failed = entry_doors_refuse("absolute", proc_sys, joined("/proc/sys/kernel/no-entry"),
                                    joined("/proc/sys/kernel/ostype"), Errno::exdev);
    failed += entry_doors_refuse("dot-dot", proc_sys, joined("kernel/../../sys/kernel/no-entry"),
                                 joined("kernel/../../sys/kernel/ostype"), Errno::exdev);
    failed += entry_doors_refuse("symbolic link", net, joined("lo/no-entry"), joined("lo/address"), Errno::exdev);
    failed += entry_doors_refuse("magic link", proc, joined("self/root/proc/sys/kernel/no-entry"),
                                 joined("self/root/proc/sys/kernel/ostype"), Errno::eloop);
    failed += entry_doors_refuse("bare dot-dot", proc_sys, joined(".."), joined(".."), Errno::exdev);
    failed +=
        entry_doors_refuse("final dot-dot", proc_sys, joined("kernel/../.."), joined("kernel/../.."), Errno::exdev);
    failed += entry_doors_refuse("final slash", proc_sys, joined("../no-entry/"), joined("../sys/"), Errno::exdev);
    failed += entry_doors_refuse("entry of the root", root, joined("/proc"), joined("/proc"), Errno::exdev);
    return failed;
}

// A ".." part and a symbolic link that stay inside the directory open.
// /proc/self is a link to the directory of the process, in /proc.
[[nodiscard]] int each_open_permits_a_path_inside() {
    IoBlockCtx const ctx{eff::testing::test()};
    fs::Dirfd const proc_sys = directory_at("/proc/sys");
    fs::Dirfd const proc = directory_at("/proc");
    if (Errno const given = open_error_of(fs::mint_file_at<fs::read_only>(ctx, proc_sys, "kernel/../kernel/ostype"));
        given != Errno{}) {
        fixy::report(fixy::Sink::Err, "mint_file_at refused kernel/../kernel/ostype with errno {}\n",
                     std::to_underlying(given));
        return 1;
    }
    if (Errno const given = error_of(fs::open_dirfd_at(ctx, proc_sys, "kernel/..")); given != Errno{}) {
        fixy::report(fixy::Sink::Err, "open_dirfd_at refused kernel/.. with errno {}\n", std::to_underlying(given));
        return 1;
    }
    if (Errno const given = open_error_of(fs::mint_file_at<fs::read_only>(ctx, proc, "self/status"));
        given != Errno{}) {
        fixy::report(fixy::Sink::Err, "mint_file_at refused self/status under /proc with errno {}\n",
                     std::to_underlying(given));
        return 1;
    }
    return 0;
}

// The doors of one entry take a ".." part and a symbolic link that stay
// inside the directory.  has_entry_at sees an entry through them, and sees
// /proc/self, a link at the last part, as an entry.  An unlink and a commit
// of a missing entry reach the kernel, which gives enoent.
[[nodiscard]] int each_entry_door_permits_a_path_inside() {
    IoBlockCtx const ctx{eff::testing::test()};
    fs::Dirfd const proc_sys = directory_at("/proc/sys");
    fs::Dirfd const proc = directory_at("/proc");
    bool const has_seen = fs::has_entry_at(ctx, proc_sys, "kernel/../kernel/ostype").value_or(false)
                       && fs::has_entry_at(ctx, proc, "self/status").value_or(false)
                       && fs::has_entry_at(ctx, proc, "self").value_or(false)
                       && !fs::has_entry_at(ctx, proc_sys, "kernel/../kernel/no-entry").value_or(true);
    if (!has_seen) {
        fixy::report(fixy::Sink::Err, "has_entry_at did not see an entry through a path inside the directory\n");
        return 1;
    }
    if (Errno const given = error_of(fs::unlink_at(ctx, proc_sys, "kernel/../kernel/no-entry"));
        given != Errno::enoent) {
        fixy::report(fixy::Sink::Err, "unlink_at of kernel/../kernel/no-entry gave errno {}\n",
                     std::to_underlying(given));
        return 1;
    }
    if (Errno const given = error_of(fs::commit_atomic_at<fs::atomicity::Rename>(
            ctx, proc_sys, "kernel/../kernel/no-entry", "kernel/no-target"));
        given != Errno::enoent) {
        fixy::report(fixy::Sink::Err, "commit_atomic_at of kernel/../kernel/no-entry gave errno {}\n",
                     std::to_underlying(given));
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    int const failed = each_open_refuses_each_escape() + each_entry_door_refuses_each_escape();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "{} doors did not refuse an escape\n", failed);
        return 1;
    }
    if (int const rc = each_open_permits_a_path_inside(); rc != 0) return rc;
    if (int const rc = each_entry_door_permits_a_path_inside(); rc != 0) return rc;
    return 0;
}
