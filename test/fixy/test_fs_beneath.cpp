// Each door under a directory refuses a path that escapes the directory.
//
// A door under a Dirfd reads its path beneath the directory of the Dirfd.
// The cases give the doors four kinds of path that leave the directory:
//
//   - an absolute path
//   - a ".." part that climbs above the directory
//   - a symbolic link in a middle part whose target is outside the
//     directory.  Each Linux host has /dev/fd, a link to /proc/self/fd.
//   - a magic link of procfs in a middle part: root of /proc/self, which
//     names the root directory of the process.
//
// The kernel gives exdev for each of the first three, and eloop for the
// magic link.  A ".." part and a link that stay inside the directory are
// permitted.
//
// The directories of the cases are /proc/sys, /proc and /dev, which each
// Linux host has.  Each escape names a file that a read can open, so a door
// that does not refuse the escape opens it, and the case fails.

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
    eff::Alloc const alloc{};
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

// Each open refuses each of the four escapes.  The cases run to the end, so
// the report names each door that did not refuse an escape.  The link case
// goes through /dev/fd to the descriptor of a Dirfd of /proc/sys.
[[nodiscard]] int each_open_refuses_each_escape() {
    fs::Dirfd const proc_sys = directory_at("/proc/sys");
    fs::Dirfd const proc = directory_at("/proc");
    fs::Dirfd const dev = directory_at("/dev");
    PathBuffer descriptor{};
    descriptor.append_decimal(static_cast<std::uint64_t>(proc_sys.get())).expect("a number fits the buffer");

    int failed = opens_refuse("absolute", proc_sys, joined("/proc/sys/kernel/ostype"), joined("/dev"), Errno::exdev);
    failed += opens_refuse("dot-dot", proc_sys, joined("kernel/../../sys/kernel/ostype"), joined(".."), Errno::exdev);
    failed += opens_refuse("symbolic link", dev, joined("fd/", descriptor.c_text().view(), "/kernel/ostype"),
                           joined("fd/", descriptor.c_text().view(), "/kernel"), Errno::exdev);
    failed += opens_refuse("magic link", proc, joined("self/root/proc/sys/kernel/ostype"), joined("self/root/dev"),
                           Errno::eloop);
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

}  // namespace

int main() {
    if (int const rc = each_open_refuses_each_escape(); rc != 0) return rc;
    if (int const rc = each_open_permits_a_path_inside(); rc != 0) return rc;
    return 0;
}
