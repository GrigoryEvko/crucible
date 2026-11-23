// The Cipher makes a new root one name at a time: from "/" for an absolute
// root, and from the current directory for a relative root.
//
// The case of an absolute root runs in a current directory that the process
// can search and cannot read, so an open of "." fails.  The make of an
// absolute root does not read the current directory, so the Cipher opens.
// A process of the root user reads each directory, so for it the case
// passes with no unreadable directory.  The case of a relative root makes
// the root under a current directory that the process can read.
//
// The open follows the link of a parent of the root, as /var/run is a link
// to /run.  A root that the open cannot make or open gives its reason, and
// the process goes on.  A name below the root still opens beneath the root.

#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <foundation/Quarantine.h>
#include <foundation/os/PathText.h>

#include "scratch_dir.h"

#include <utility>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the test sets the current directory, the modes and the links of a root")
#include <sys/stat.h>
#include <unistd.h>

namespace {

using crucible::test::make_scratch_path;
using crucible::test::ScratchPath;

// The modes that the test gives a directory: the search permission alone,
// the read and the search permissions, and each permission of the owner.
enum class DirectoryMode : unsigned {
    search_only = 0100,
    read_and_search = 0500,
    owner_all = 0700,
};

// The two names of a link: the entry that it points to, and its own name.
struct LinkTargetRole final {};
struct LinkNameRole final {};
using LinkTarget = fixy::Side<LinkTargetRole, fixy::TextView>;
using LinkName = fixy::Side<LinkNameRole, fixy::TextView>;

// Gives the directory name of the scratch directory the mode.
[[nodiscard]] bool set_mode(fixy::fs::TemporaryDirectory const& scratch, fixy::TextView name,
                            DirectoryMode mode) noexcept {
    ScratchPath const path = make_scratch_path(scratch, name);
    return ::chmod(::foundation::core::foreign_c_str(path.c_text()), std::to_underlying(mode)) == 0;
}

// Makes the directory name of the scratch directory the current directory.
[[nodiscard]] bool enter(fixy::fs::TemporaryDirectory const& scratch, fixy::TextView name) noexcept {
    ScratchPath const path = make_scratch_path(scratch, name);
    return ::chdir(::foundation::core::foreign_c_str(path.c_text())) == 0;
}

// Makes the directory name of the scratch directory the current directory,
// with the search permission and no read permission.
[[nodiscard]] bool enter_unreadable(fixy::fs::TemporaryDirectory const& scratch, fixy::TextView name) noexcept {
    return enter(scratch, name) && set_mode(scratch, name, DirectoryMode::search_only);
}

// Gives the directory name of the scratch directory its permissions again,
// and makes "/" the current directory.
[[nodiscard]] bool leave(fixy::fs::TemporaryDirectory const& scratch, fixy::TextView name) noexcept {
    return set_mode(scratch, name, DirectoryMode::owner_all) && ::chdir("/") == 0;
}

// True when the process is the root user, which may make a name in each
// directory.
[[nodiscard]] bool is_root_user() noexcept { return ::geteuid() == 0; }

// Makes the link name in the scratch directory to the entry target of the
// scratch directory.  The target need not exist.
[[nodiscard]] bool make_link(fixy::fs::TemporaryDirectory const& scratch, LinkTarget const& target,
                             LinkName const& name) noexcept {
    ScratchPath const target_path = make_scratch_path(scratch, target.value());
    ScratchPath const link_path = make_scratch_path(scratch, name.value());
    return ::symlink(::foundation::core::foreign_c_str(target_path.c_text()),
                     ::foundation::core::foreign_c_str(link_path.c_text()))
        == 0;
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

[[nodiscard]] crucible::CipherOpenResult open_cipher(fixy::TextView root) {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    return crucible::Cipher::open(ctx, ctx.cap().alloc, fixy::mint_tagged<fixy::tags::source::External>(root));
}

// Opens a store at the path below the scratch directory.
[[nodiscard]] crucible::CipherOpenResult open_below(fixy::fs::TemporaryDirectory const& scratch, fixy::TextView below) {
    PathBuffer const root = joined(scratch.path().view(), below);
    return open_cipher(root.c_text().view());
}

// Makes the empty file name under dir, and closes it.
[[nodiscard]] bool make_file(fixy::TestRunnerCtx const& ctx, fixy::fs::DirectoryHandle const& dir, fixy::CText name) {
    using WriteTruncate = fixy::atom::fs::mode<fixy::fs::open_mode::WriteTruncate>;
    return fixy::fs::mint_file_at<WriteTruncate>(ctx, dir.ref(), name)
        .match(
            [](fixy::Linear<fixy::fs::FileOf<WriteTruncate>> opened) {
                fixy::fs::FileOf<WriteTruncate> const file = std::move(opened).consume();
                return file.is_open();
            },
            [](foundation::os::Errno) { return false; });
}

// True when the open gives a store.
[[nodiscard]] bool opens(crucible::CipherOpenResult const& opened) { return opened.is_ok(); }

// True when the open gives no store, with the reason.
[[nodiscard]] bool refuses_with(crucible::CipherOpenResult const& opened, crucible::Cipher::OpenError reason) {
    return opened.match([](crucible::Cipher const&) { return false; },
                        [reason](crucible::Cipher::OpenError given) { return given == reason; });
}

// True when the Cipher made the object directory of its root at path.
[[nodiscard]] bool has_objects(PathBuffer const& root) {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    PathBuffer const objects = joined(root.c_text().view(), "/objects");
    return fixy::fs::has_entry(ctx, objects.c_text()).value_or(false);
}

// A new absolute root of three levels opens in a current directory that
// does not open.
[[nodiscard]] int absolute_root_opens_from_an_unreadable_directory(fixy::fs::TemporaryDirectory const& scratch) {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    fixy::Path<fixy::tags::source::Sanitized> const scratch_path =
        fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(scratch.path().view()))
            .expect("the scratch path passes the sanitizer");
    fixy::fs::DirectoryHandle const scratch_dir =
        fixy::fs::open_directory(ctx, scratch_path).expect("the scratch directory opens");
    if (fixy::fs::make_directory_at(ctx, scratch_dir.ref(), "unreadable").is_err()) {
        fixy::report(fixy::Sink::Err, "the unreadable directory was not made\n");
        return 1;
    }
    PathBuffer const root = joined(scratch.path().view(), "/absolute/first/second");
    if (!enter_unreadable(scratch, "unreadable")) {
        fixy::report(fixy::Sink::Err, "the test did not enter the unreadable directory\n");
        return 1;
    }
    crucible::CipherOpenResult const opened = open_cipher(root.c_text().view());
    bool const is_left = leave(scratch, "unreadable");
    if (!opens(opened) || !has_objects(root)) {
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
[[nodiscard]] int relative_root_opens_under_the_current_directory(fixy::fs::TemporaryDirectory const& scratch) {
    if (!enter(scratch, ".")) {
        fixy::report(fixy::Sink::Err, "the test did not enter the scratch directory\n");
        return 1;
    }
    crucible::CipherOpenResult const opened = open_cipher("relative/first/./second");
    bool const is_left = leave(scratch, ".");
    if (!opens(opened) || !has_objects(joined(scratch.path().view(), "/relative/first/second"))) {
        fixy::report(fixy::Sink::Err, "the Cipher did not make its new relative root\n");
        return 1;
    }
    if (!is_left) {
        fixy::report(fixy::Sink::Err, "the test did not leave the scratch directory\n");
        return 1;
    }
    return 0;
}

// A new root under a parent that is a link, as /var/run is a link to /run,
// opens, and the open makes the root in the target of the link.
[[nodiscard]] int root_opens_under_a_linked_parent(fixy::fs::TemporaryDirectory const& scratch) {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    fixy::Path<fixy::tags::source::Sanitized> const scratch_path =
        fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(scratch.path().view()))
            .expect("the scratch path passes the sanitizer");
    fixy::fs::DirectoryHandle const scratch_dir =
        fixy::fs::open_directory(ctx, scratch_path).expect("the scratch directory opens");
    if (fixy::fs::make_directory_at(ctx, scratch_dir.ref(), "target").is_err()
        || !make_link(scratch, LinkTarget{"target"}, LinkName{"linked"})) {
        fixy::report(fixy::Sink::Err, "the test did not make the linked parent\n");
        return 1;
    }
    crucible::CipherOpenResult const opened = open_below(scratch, "/linked/new/store");
    if (!opens(opened) || !has_objects(joined(scratch.path().view(), "/target/new/store"))) {
        fixy::report(fixy::Sink::Err, "the Cipher did not make its new root under a linked parent\n");
        return 1;
    }
    return 0;
}

// A root that exists opens under a parent that the process can search and
// cannot read, because the open of the root reads the root alone.  For the
// root user the mode changes nothing, and the case also passes.
[[nodiscard]] int existing_root_opens_under_a_search_only_parent(fixy::fs::TemporaryDirectory const& scratch) {
    if (!opens(open_below(scratch, "/search_only/store"))) {
        fixy::report(fixy::Sink::Err, "the Cipher did not make the root under the search-only parent\n");
        return 1;
    }
    if (!set_mode(scratch, "search_only", DirectoryMode::search_only)) return 1;
    bool const is_open = opens(open_below(scratch, "/search_only/store"));
    if (!set_mode(scratch, "search_only", DirectoryMode::owner_all) || !is_open) {
        fixy::report(fixy::Sink::Err, "an existing root under a search-only parent does not open\n");
        return 1;
    }
    return 0;
}

// A relative root that exists opens in a current directory that the process
// can search and cannot read.
[[nodiscard]] int existing_relative_root_opens_in_a_search_only_directory(fixy::fs::TemporaryDirectory const& scratch) {
    if (!opens(open_below(scratch, "/search_only_cwd/store"))) {
        fixy::report(fixy::Sink::Err, "the Cipher did not make the root in the search-only directory\n");
        return 1;
    }
    if (!enter_unreadable(scratch, "search_only_cwd")) {
        fixy::report(fixy::Sink::Err, "the test did not enter the search-only directory\n");
        return 1;
    }
    crucible::CipherOpenResult const opened = open_cipher("store");
    bool const is_left = leave(scratch, "search_only_cwd");
    if (!opens(opened) || !is_left) {
        fixy::report(fixy::Sink::Err, "an existing relative root in a search-only directory does not open\n");
        return 1;
    }
    return 0;
}

// Each root that the open cannot make or open gives its reason, and the
// process goes on.
[[nodiscard]] int refused_roots_give_their_reasons(fixy::fs::TemporaryDirectory const& scratch) {
    using crucible::Cipher;
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    fixy::Path<fixy::tags::source::Sanitized> const scratch_path =
        fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(scratch.path().view()))
            .expect("the scratch path passes the sanitizer");
    fixy::fs::DirectoryHandle const scratch_dir =
        fixy::fs::open_directory(ctx, scratch_path).expect("the scratch directory opens");

    // A part of the path is a link to a name that does not exist.
    if (!make_link(scratch, LinkTarget{"absent"}, LinkName{"dangling"})) return 1;
    if (!refuses_with(open_below(scratch, "/dangling/store"), Cipher::OpenError::missing)) {
        fixy::report(fixy::Sink::Err, "a root through a dangling link does not give missing\n");
        return 1;
    }

    // A part of the path is a file.
    if (!make_file(ctx, scratch_dir, "plain")) return 1;
    if (!refuses_with(open_below(scratch, "/plain/store"), Cipher::OpenError::not_a_directory)) {
        fixy::report(fixy::Sink::Err, "a root under a file does not give not_a_directory\n");
        return 1;
    }

    // The root itself is a link.
    if (fixy::fs::make_directory_at(ctx, scratch_dir.ref(), "real_root").is_err()
        || !make_link(scratch, LinkTarget{"real_root"}, LinkName{"root_link"})) {
        return 1;
    }
    if (!refuses_with(open_below(scratch, "/root_link"), Cipher::OpenError::not_a_directory)) {
        fixy::report(fixy::Sink::Err, "a root that is a link does not give not_a_directory\n");
        return 1;
    }

    // The sanitizer refuses a ".." part.
    if (!refuses_with(open_below(scratch, "/a/../b"), Cipher::OpenError::path_refused)) {
        fixy::report(fixy::Sink::Err, "a root with a .. part does not give path_refused\n");
        return 1;
    }

    // The process may not make a name in the parent.  The root user may.
    if (!is_root_user()) {
        if (fixy::fs::make_directory_at(ctx, scratch_dir.ref(), "locked").is_err()
            || !set_mode(scratch, "locked", DirectoryMode::read_and_search)) {
            return 1;
        }
        const bool is_refused = refuses_with(open_below(scratch, "/locked/store"), Cipher::OpenError::no_permission);
        if (!set_mode(scratch, "locked", DirectoryMode::owner_all) || !is_refused) {
            fixy::report(fixy::Sink::Err, "a root in a directory that the process may not write does not give "
                                          "no_permission\n");
            return 1;
        }
    }
    return 0;
}

// A name below the root still opens beneath the root: a link at objects to a
// directory outside the root takes no object, and the store gives no hash.
[[nodiscard]] int name_below_the_root_cannot_escape(fixy::fs::TemporaryDirectory const& scratch) {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    fixy::Path<fixy::tags::source::Sanitized> const scratch_path =
        fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(scratch.path().view()))
            .expect("the scratch path passes the sanitizer");
    fixy::fs::DirectoryHandle const scratch_dir =
        fixy::fs::open_directory(ctx, scratch_path).expect("the scratch directory opens");
    if (fixy::fs::make_directory_at(ctx, scratch_dir.ref(), "escape_root").is_err()
        || fixy::fs::make_directory_at(ctx, scratch_dir.ref(), "outside").is_err()
        || !make_link(scratch, LinkTarget{"outside"}, LinkName{"escape_root/objects"})) {
        fixy::report(fixy::Sink::Err, "the test did not make the escaping link\n");
        return 1;
    }
    crucible::Cipher cipher = open_below(scratch, "/escape_root").expect("a root whose objects is a link still opens");
    const auto view = cipher.mint_open_view(ctx);
    fixy::BumpArena<crucible::DagArena> arena =
        fixy::mint_bump_arena<crucible::DagArena>(fixy::Quantity<fixy::unit::Byte, std::size_t>{std::size_t{1} << 12});
    const auto ops_run = arena.array<crucible::TraceEntry>(ctx.cap().alloc, 1, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    const fixy::View<crucible::TraceEntry> ops = ops_run.view();
    ops.get(0).expect("the region holds one op")->schema_hash = crucible::SchemaHash{0xE5C};
    const crucible::ContentHash stored = cipher.store(
        ctx, view, crucible::Cipher::content_addressed(crucible::make_region(ctx.cap().alloc, arena, ops)));
    if (static_cast<bool>(stored)) {
        fixy::report(fixy::Sink::Err, "a store through a link at objects gave a hash\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    fixy::Result<fixy::fs::TemporaryDirectory, foundation::os::Errno> made =
        crucible::test::make_scratch_directory(ctx);
    if (made.is_err()) {
        fixy::report(fixy::Sink::Err, "[skipped] cannot make a scratch directory under /tmp\n");
        return 0;
    }
    fixy::fs::TemporaryDirectory const scratch = std::move(made).expect("the scratch directory is made");
    if (int const rc = absolute_root_opens_from_an_unreadable_directory(scratch); rc != 0) return rc;
    if (int const rc = relative_root_opens_under_the_current_directory(scratch); rc != 0) return rc;
    if (int const rc = root_opens_under_a_linked_parent(scratch); rc != 0) return rc;
    if (int const rc = existing_root_opens_under_a_search_only_parent(scratch); rc != 0) return rc;
    if (int const rc = existing_relative_root_opens_in_a_search_only_directory(scratch); rc != 0) return rc;
    if (int const rc = refused_roots_give_their_reasons(scratch); rc != 0) return rc;
    if (int const rc = name_below_the_root_cannot_escape(scratch); rc != 0) return rc;
    return 0;
}
