// The file surface and the three durable writers, run against a real
// directory.
//
// Neither fixy/os/Fs.h nor fixy/os/CipherDurable.h, nor the two headers
// they were ported from, ever had a positive test.  Sixteen
// negative-compile fixtures stood over the old ones, saying what the
// gates refuse and nothing about what the mints do — which is how the
// cold writer shipped pinned to a commit that returned ENOSYS on every
// call.  The second case here is that path, and it now has to succeed.
//
// Everything runs in a directory mkdtemp made under /tmp and removes on
// the way out.  What the kernel can refuse — an unwritable /tmp — is
// skipped rather than failed.

#include <fixy/OwnedFile.h>
#include <fixy/Path.h>
#include <fixy/os/CipherDurable.h>
#include <fixy/os/Fs.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace durable = fixy::cipher::durable;
namespace src = fixy::tags::source;

namespace {

using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

// Every path the surface takes is Path<Sanitized>, and the only way to
// one is through the sanitizer: mint the external form, then launder it.
[[nodiscard]] fixy::Path<src::Sanitized> sanitized(const std::string& raw) {
    auto laundered = fixy::sanitize::path_traversal::sanitize_path_no_dotdot(
        fixy::mint_tagged<src::External>(std::filesystem::path{raw}));
    if (!laundered) {
        std::fprintf(stderr, "a test path failed to sanitize: %s\n", raw.c_str());
        std::abort();
    }
    return std::move(*laundered);
}

// The scratch directory, removed with everything in it when the test
// ends, whichever way it ends.
class ScratchDir final {
    std::string path_;

public:
    ScratchDir() {
        char pattern[] = "/tmp/fixy-fs-test-XXXXXX";
        if (const char* made = ::mkdtemp(pattern); made != nullptr) path_ = made;
    }
    ~ScratchDir() {
        if (path_.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;

    [[nodiscard]] bool is_ready() const noexcept { return !path_.empty(); }
    [[nodiscard]] std::string file(const char* name) const { return path_ + "/" + name; }
    [[nodiscard]] const std::string& dir() const noexcept { return path_; }
};

inline constexpr std::size_t kPayloadBytes = 4096;

void fill_pattern(std::uint8_t* out, std::uint8_t salt) {
    for (std::size_t index = 0; index < kPayloadBytes; ++index) {
        out[index] = static_cast<std::uint8_t>((index * 7u + salt) & 0xFFu);
    }
}

[[nodiscard]] bool write_all(int fd, const std::uint8_t* bytes) {
    std::size_t done = 0;
    while (done < kPayloadBytes) {
        const ::ssize_t n = ::write(fd, bytes + done, kPayloadBytes - done);
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

// Reads a file back through OwnedFile, the stdio handle, so the two
// handle families meet at the byte level.  The handle comes through
// open_path, the door that does the fopen itself; a value means the
// stream is open.
[[nodiscard]] bool file_holds(const std::string& path, const std::uint8_t* expected) {
    auto in = fixy::OwnedFile::open_path(path.c_str(), "rb");
    if (!in) return false;
    std::uint8_t readback[kPayloadBytes];
    if (std::fread(readback, 1, kPayloadBytes, in->get()) != kPayloadBytes) return false;
    return std::memcmp(readback, expected, kPayloadBytes) == 0;
}

[[nodiscard]] int mint_file_writes_syncs_and_commits(const ScratchDir& scratch) {
    IoBlockCtx ctx{eff::testing::test()};
    std::uint8_t pattern[kPayloadBytes];
    fill_pattern(pattern, 1);

    const std::string tmp = scratch.file("plain.tmp");
    const std::string target = scratch.file("plain");

    auto opened = fs::mint_file<fixy::atom::fs::mode<fs::open_mode::WriteCreate>>(ctx, sanitized(tmp));
    if (!opened) {
        std::fprintf(stderr, "mint_file WriteCreate failed (%s)\n", opened.error().message().c_str());
        return 1;
    }
    if (!opened->peek().is_open()) {
        std::fprintf(stderr, "a successful mint handed back a closed descriptor\n");
        return 1;
    }
    if (!write_all(opened->peek().get(), pattern)) {
        std::fprintf(stderr, "writing through the minted descriptor failed (errno %d)\n", errno);
        return 1;
    }
    if (auto synced = fs::sync<fs::sync_op::Fdatasync>(ctx, opened->peek()); !synced) {
        std::fprintf(stderr, "fdatasync failed (%s)\n", synced.error().message().c_str());
        return 1;
    }
    // Dropping the Linear closes the descriptor; the commit renames the
    // closed file into place.
    { auto closed = std::move(*opened).consume(); (void)closed; }

    if (auto committed = fs::commit_atomic<fs::atomicity::Rename>(ctx, sanitized(tmp), sanitized(target));
        !committed) {
        std::fprintf(stderr, "commit_atomic<Rename> failed (%s)\n", committed.error().message().c_str());
        return 1;
    }
    if (!file_holds(target, pattern)) {
        std::fprintf(stderr, "the committed file does not hold the bytes that were written\n");
        return 1;
    }
    if (::access(tmp.c_str(), F_OK) == 0) {
        std::fprintf(stderr, "the temporary name still exists after the commit\n");
        return 1;
    }
    return 0;
}

// The path that returned ENOSYS on every call.  cold_writer_stance
// pinned LinkAtomic, and the old commit body returned ENOSYS for it
// before consulting a filesystem.  The stance pins Rename now, and this
// leg is what says the whole sequence — open with O_SYNC, write, fsync,
// commit — reaches the target.
[[nodiscard]] int cold_writer_commit_succeeds_where_it_was_enosys(const ScratchDir& scratch) {
    IoBlockCtx ctx{eff::testing::test()};
    std::uint8_t pattern[kPayloadBytes];
    fill_pattern(pattern, 2);

    const std::string tmp = scratch.file("cold.tmp");
    const std::string target = scratch.file("cold");

    auto writer = durable::mint_cold_writer<>(ctx, sanitized(tmp));
    if (!writer) {
        std::fprintf(stderr, "mint_cold_writer failed (%s)\n", writer.error().message().c_str());
        return 1;
    }
    if (!write_all(writer->peek().get(), pattern)) {
        std::fprintf(stderr, "writing through the cold writer failed (errno %d)\n", errno);
        return 1;
    }
    if (auto synced = writer->peek().sync(ctx); !synced) {
        std::fprintf(stderr, "the cold writer's fsync failed (%s)\n", synced.error().message().c_str());
        return 1;
    }
    const auto committed = writer->peek().commit_atomic(ctx, sanitized(tmp), sanitized(target));
    if (!committed) {
        if (committed.error().value() == ENOSYS) {
            std::fprintf(stderr, "the cold writer's commit is still ENOSYS: the path this port exists to close\n");
        } else {
            std::fprintf(stderr, "the cold writer's commit failed (%s)\n", committed.error().message().c_str());
        }
        return 1;
    }
    if (!file_holds(target, pattern)) {
        std::fprintf(stderr, "the cold-committed file does not hold the bytes that were written\n");
        return 1;
    }
    return 0;
}

// RenameAt2NoReplace refuses an existing target, and that refusal is a
// promise the caller relied on.  This leg pins both halves: EEXIST when
// the target exists, success when it does not.  A filesystem without
// the flag reports EINVAL, which would be passed through too — a
// fallback to plain rename would overwrite what the caller promised
// not to.
[[nodiscard]] int no_replace_commit_refuses_an_existing_target(const ScratchDir& scratch) {
    IoBlockCtx ctx{eff::testing::test()};
    std::uint8_t pattern[kPayloadBytes];
    fill_pattern(pattern, 3);

    const std::string tmp = scratch.file("warm.tmp");
    const std::string occupied = scratch.file("plain");  // committed by the first leg
    const std::string fresh = scratch.file("warm");

    auto writer = durable::mint_warm_writer<>(ctx, sanitized(tmp));
    if (!writer) {
        std::fprintf(stderr, "mint_warm_writer failed (%s)\n", writer.error().message().c_str());
        return 1;
    }
    if (!write_all(writer->peek().get(), pattern)) {
        std::fprintf(stderr, "writing through the warm writer failed (errno %d)\n", errno);
        return 1;
    }
    if (auto synced = writer->peek().sync(ctx); !synced) {
        std::fprintf(stderr, "the warm writer's fdatasync failed (%s)\n", synced.error().message().c_str());
        return 1;
    }

    const auto refused = writer->peek().commit_atomic(ctx, sanitized(tmp), sanitized(occupied));
    if (refused) {
        std::fprintf(stderr, "a no-replace commit overwrote an existing target\n");
        return 1;
    }
    if (refused.error().value() == EINVAL) {
        std::fprintf(stderr, "[skipped] this filesystem has no RENAME_NOREPLACE; the errno is passed through as designed\n");
        return 0;
    }
    if (refused.error().value() != EEXIST) {
        std::fprintf(stderr, "a no-replace commit onto an existing target failed with %s rather than EEXIST\n",
                     refused.error().message().c_str());
        return 1;
    }

    if (auto committed = writer->peek().commit_atomic(ctx, sanitized(tmp), sanitized(fresh)); !committed) {
        std::fprintf(stderr, "a no-replace commit onto a fresh target failed (%s)\n",
                     committed.error().message().c_str());
        return 1;
    }
    if (!file_holds(fresh, pattern)) {
        std::fprintf(stderr, "the warm-committed file does not hold the bytes that were written\n");
        return 1;
    }
    return 0;
}

// The directory descriptor is its own type, opened through the one
// door, and fsync on it is the call that flushes an entry.
[[nodiscard]] int dirfd_opens_and_flushes_the_entry(const ScratchDir& scratch) {
    IoBlockCtx ctx{eff::testing::test()};

    auto dir = fs::open_dirfd(ctx, sanitized(scratch.dir()));
    if (!dir) {
        std::fprintf(stderr, "open_dirfd on the scratch directory failed (%s)\n", dir.error().message().c_str());
        return 1;
    }
    if (!dir->is_open()) {
        std::fprintf(stderr, "a successful open_dirfd handed back a closed handle\n");
        return 1;
    }
    if (auto flushed = fs::sync<fs::sync_op::FsyncParentDir>(ctx, *dir); !flushed) {
        std::fprintf(stderr, "fsync on the directory descriptor failed (%s)\n", flushed.error().message().c_str());
        return 1;
    }

    // A regular file is refused by O_DIRECTORY, so the door only opens
    // onto what the type claims.
    auto not_a_dir = fs::open_dirfd(ctx, sanitized(scratch.file("plain")));
    if (not_a_dir) {
        std::fprintf(stderr, "open_dirfd opened a regular file as a directory\n");
        return 1;
    }
    if (not_a_dir.error().value() != ENOTDIR) {
        std::fprintf(stderr, "open_dirfd on a file failed with %s rather than ENOTDIR\n",
                     not_a_dir.error().message().c_str());
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    ScratchDir scratch;
    if (!scratch.is_ready()) {
        std::fprintf(stderr, "[skipped] cannot make a scratch directory under /tmp (errno %d)\n", errno);
        return 0;
    }
    if (const int rc = mint_file_writes_syncs_and_commits(scratch); rc != 0) return rc;
    if (const int rc = cold_writer_commit_succeeds_where_it_was_enosys(scratch); rc != 0) return rc;
    if (const int rc = no_replace_commit_refuses_an_existing_target(scratch); rc != 0) return rc;
    if (const int rc = dirfd_opens_and_flushes_the_entry(scratch); rc != 0) return rc;
    return 0;
}
