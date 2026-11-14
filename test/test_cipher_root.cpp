// The Cipher makes a new root one name at a time: from "/" for an absolute
// root, and from the current directory for a relative root.
//
// The case of an absolute root runs in a current directory that the process
// can search and cannot read, so an open of "." fails.  The make of an
// absolute root does not read the current directory, so the Cipher opens.
// A process of the root user reads each directory, so for it the case
// passes with no unreadable directory.  The case of a relative root makes
// the root under a current directory that the process can read.

#include <crucible/Cipher.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <foundation/Quarantine.h>
#include <foundation/os/PathText.h>

#include "scratch_dir.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the current directory of the process is a directory that it cannot read")
#include <sys/stat.h>
#include <unistd.h>

namespace {

// The characters of the path of the scratch directory.
[[nodiscard]] fixy::TextView text_of(crucible::test::ScratchDir const& scratch) noexcept {
    return fixy::mint_foreign_text(scratch.path().data(), scratch.path().size());
}

// Makes the directory name of the scratch directory the current directory.
[[nodiscard]] bool enter(crucible::test::ScratchDir const& scratch, char const* name) noexcept {
    return ::chdir(scratch.file(name).c_str()) == 0;
}

// Makes the directory name of the scratch directory the current directory,
// with the search permission and no read permission.
[[nodiscard]] bool enter_unreadable(crucible::test::ScratchDir const& scratch, char const* name) noexcept {
    return enter(scratch, name) && ::chmod(scratch.file(name).c_str(), 0100) == 0;
}

// Gives the directory name of the scratch directory its permissions again,
// and makes "/" the current directory.
[[nodiscard]] bool leave(crucible::test::ScratchDir const& scratch, char const* name) noexcept {
    return ::chmod(scratch.file(name).c_str(), 0700) == 0 && ::chdir("/") == 0;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

using PathBuffer = foundation::os::PathText<fixy::kPathBytes>;

// The path of the parts, one after the other.
[[nodiscard]] PathBuffer joined(fixy::TextView first, fixy::TextView second = {}) {
    PathBuffer path{};
    path.append(first).expect("a path of the test fits its buffer");
    path.append(second).expect("a path of the test fits its buffer");
    return path;
}

[[nodiscard]] crucible::Cipher open_cipher(fixy::TextView root) {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    return crucible::Cipher::open(ctx, ctx.cap().alloc, fixy::mint_tagged<fixy::tags::source::External>(root));
}

// True when the Cipher made the object directory of its root at path.
[[nodiscard]] bool has_objects(PathBuffer const& root) {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    PathBuffer const objects = joined(root.c_text().view(), "/objects");
    return fixy::fs::has_entry(ctx, objects.c_text()).value_or(false);
}

// A new absolute root of three levels opens in a current directory that
// does not open.
[[nodiscard]] int absolute_root_opens_from_an_unreadable_directory(crucible::test::ScratchDir const& scratch) {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    fixy::Path<fixy::tags::source::Sanitized> const scratch_path =
        fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(text_of(scratch)))
            .expect("the scratch path passes the sanitizer");
    fixy::fs::Dirfd const scratch_dir = fixy::fs::open_dirfd(ctx, scratch_path).expect("the scratch directory opens");
    if (fixy::fs::make_directory_at(ctx, scratch_dir, "unreadable").is_err()) {
        fixy::report(fixy::Sink::Err, "the unreadable directory was not made\n");
        return 1;
    }
    PathBuffer const root = joined(text_of(scratch), "/absolute/first/second");
    if (!enter_unreadable(scratch, "unreadable")) {
        fixy::report(fixy::Sink::Err, "the test did not enter the unreadable directory\n");
        return 1;
    }
    crucible::Cipher const cipher = open_cipher(root.c_text().view());
    bool const is_left = leave(scratch, "unreadable");
    if (!cipher.is_open() || !has_objects(root)) {
        fixy::report(fixy::Sink::Err, "the Cipher did not make its new absolute root\n");
        return 1;
    }
    if (!is_left) {
        fixy::report(fixy::Sink::Err, "the test did not leave the unreadable directory\n");
        return 1;
    }
    return 0;
}

// A new relative root of three levels opens under the current directory.
[[nodiscard]] int relative_root_opens_under_the_current_directory(crucible::test::ScratchDir const& scratch) {
    if (!enter(scratch, ".")) {
        fixy::report(fixy::Sink::Err, "the test did not enter the scratch directory\n");
        return 1;
    }
    crucible::Cipher const cipher = open_cipher("relative/first/./second");
    bool const is_left = leave(scratch, ".");
    if (!cipher.is_open() || !has_objects(joined(text_of(scratch), "/relative/first/second"))) {
        fixy::report(fixy::Sink::Err, "the Cipher did not make its new relative root\n");
        return 1;
    }
    if (!is_left) {
        fixy::report(fixy::Sink::Err, "the test did not leave the scratch directory\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    crucible::test::ScratchDir const scratch{"crucible-cipher-root"};
    if (!scratch.is_ready()) {
        fixy::report(fixy::Sink::Err, "[skipped] cannot make a scratch directory under /tmp\n");
        return 0;
    }
    if (int const rc = absolute_root_opens_from_an_unreadable_directory(scratch); rc != 0) return rc;
    if (int const rc = relative_root_opens_under_the_current_directory(scratch); rc != 0) return rc;
    return 0;
}
