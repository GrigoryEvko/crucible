// The durable writers of the Cipher, run against a real directory.
//
// The negative-compile fixtures of crucible/cipher/CipherDurable.h say what
// the gates refuse and nothing about what the mints do.  This file runs the
// mints.  A cold writer whose commit returned ENOSYS on every call would
// pass each fixture, and the first case here refuses it.
//
// Everything runs in a scratch directory under /tmp (scratch_dir.h), which
// the test removes at the end.  When the kernel refuses the scratch
// directory, the test reports a skip and does not fail.

#include <crucible/cipher/CipherDurable.h>

#include <fixy/Core.h>
#include <fixy/FixedArray.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>

#include "scratch_dir.h"
#include "test_assert.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace durable = crucible::cipher::durable;
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
        ::fixy::fatal("a test path failed to sanitize\n");
    }
    return std::move(*laundered);
}

using crucible::test::ScratchDir;

inline constexpr std::size_t kPayloadBytes = 4096;

using Payload = ::fixy::FixedArray<std::uint8_t, kPayloadBytes>;

[[nodiscard]] Payload pattern_of(std::uint8_t salt) {
    Payload pattern{};
    for (std::size_t index = 0; index < kPayloadBytes; ++index) {
        pattern[index] = static_cast<std::uint8_t>((index * 7u + salt) & 0xFFu);
    }
    return pattern;
}

// Takes the linear payload out of the result of a mint.  The match gives
// the payload to its consumer on the value path, so no local of a leg holds
// a linear value on an early return.
template <typename Handle>
[[nodiscard]] fixy::Result<Handle, foundation::os::Errno>
opened(fixy::Result<fixy::Linear<Handle>, foundation::os::Errno> minted) noexcept {
    using Opened = fixy::Result<Handle, foundation::os::Errno>;
    return std::move(minted).match(
        [](fixy::Linear<Handle> handle) noexcept -> Opened { return std::move(handle).consume(); },
        [](foundation::os::Errno error) noexcept -> Opened { return fixy::err(error); });
}

// The error number of a result, or zero when it holds a value.
template <typename T>
[[nodiscard]] int errno_of(fixy::Result<T, foundation::os::Errno> const& result) noexcept {
    return std::to_underlying(result.err().value_or(foundation::os::Errno{}));
}

// Reads a file back through the file door of fixy/os/Fs.h, and compares
// each byte with the bytes that a writer put.
[[nodiscard]] bool file_holds(const std::string& path, const Payload& expected) {
    IoBlockCtx ctx{eff::testing::test()};
    auto reader = opened(fs::mint_file<fixy::atom::fs::mode<fs::open_mode::ReadOnly>>(ctx, sanitized(path)));
    if (reader.is_err()) return false;
    fs::OwnedFd const file = std::move(reader).expect("the reader opened");
    Payload readback{};
    // A failed read gives the count zero, so it returns false too.
    if (fs::read_full(ctx, file, std::as_writable_bytes(readback.as_span())).value_or(0) != kPayloadBytes) return false;
    for (std::size_t index = 0; index < kPayloadBytes; ++index) {
        if (readback[index] != expected[index]) return false;
    }
    return true;
}

// The cold writer, end to end.  cold_writer_stance pins Rename, and this
// leg is what says the whole sequence — open with O_SYNC, write, fsync,
// commit — reaches the target.  A commit body that returned ENOSYS
// before it consulted a filesystem fails this leg.
[[nodiscard]] int cold_writer_commit_succeeds(const ScratchDir& scratch) {
    IoBlockCtx ctx{eff::testing::test()};
    const Payload pattern = pattern_of(2);

    const std::string tmp = scratch.file("cold.tmp");
    const std::string target = scratch.file("cold");

    auto opened_writer = opened(durable::mint_cold_writer<>(ctx, sanitized(tmp)));
    if (opened_writer.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "mint_cold_writer failed (errno {})\n", errno_of(opened_writer));
        return 1;
    }
    auto const writer = std::move(opened_writer).expect("the cold writer opened");
    if (auto written = fs::write_full(ctx, writer.handle(), std::as_bytes(pattern.as_span())); written.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "writing through the cold writer failed (errno {})\n", errno_of(written));
        return 1;
    }
    if (auto synced = writer.sync(ctx); synced.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "the fsync of the cold writer failed (errno {})\n", errno_of(synced));
        return 1;
    }
    const auto committed = writer.commit_atomic(ctx, sanitized(tmp), sanitized(target));
    if (committed.is_err()) {
        if (errno_of(committed) == ENOSYS) {
            ::fixy::report(::fixy::Sink::Err,
                           "the commit of the cold writer returned ENOSYS, so it did not reach a filesystem\n");
        } else {
            ::fixy::report(::fixy::Sink::Err, "the commit of the cold writer failed (errno {})\n", errno_of(committed));
        }
        return 1;
    }
    if (!file_holds(target, pattern)) {
        ::fixy::report(::fixy::Sink::Err, "the cold-committed file does not hold the bytes that were written\n");
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
    const Payload pattern = pattern_of(3);

    const std::string tmp = scratch.file("warm.tmp");
    const std::string occupied = scratch.file("occupied");
    const std::string fresh = scratch.file("warm");

    // The occupied target exists before the commit.  The descriptor closes
    // at the end of the block.
    {
        auto const occupant =
            opened(fs::mint_file<fixy::atom::fs::mode<fs::open_mode::WriteCreate>>(ctx, sanitized(occupied)));
        if (occupant.is_err()) {
            ::fixy::report(::fixy::Sink::Err, "the occupied target could not be made\n");
            return 1;
        }
    }

    auto opened_writer = opened(durable::mint_warm_writer<>(ctx, sanitized(tmp)));
    if (opened_writer.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "mint_warm_writer failed (errno {})\n", errno_of(opened_writer));
        return 1;
    }
    auto const writer = std::move(opened_writer).expect("the warm writer opened");
    if (auto written = fs::write_full(ctx, writer.handle(), std::as_bytes(pattern.as_span())); written.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "writing through the warm writer failed (errno {})\n", errno_of(written));
        return 1;
    }
    if (auto synced = writer.sync(ctx); synced.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "the fdatasync of the warm writer failed (errno {})\n", errno_of(synced));
        return 1;
    }

    const auto refused = writer.commit_atomic(ctx, sanitized(tmp), sanitized(occupied));
    if (refused.is_ok()) {
        ::fixy::report(::fixy::Sink::Err, "a no-replace commit overwrote an existing target\n");
        return 1;
    }
    if (errno_of(refused) == EINVAL) {
        crucible::test::pass(
            "[skipped] this filesystem has no RENAME_NOREPLACE; the errno is passed through as designed\n");
        return 0;
    }
    if (errno_of(refused) != EEXIST) {
        ::fixy::report(::fixy::Sink::Err,
                       "a no-replace commit onto an existing target failed with errno {} and not with EEXIST\n",
                       errno_of(refused));
        return 1;
    }

    if (auto committed = writer.commit_atomic(ctx, sanitized(tmp), sanitized(fresh)); committed.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "a no-replace commit onto a fresh target failed (errno {})\n",
                       errno_of(committed));
        return 1;
    }
    if (!file_holds(fresh, pattern)) {
        ::fixy::report(::fixy::Sink::Err, "the warm-committed file does not hold the bytes that were written\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    const ScratchDir scratch{"cipher-durable-test"};
    if (!scratch.is_ready()) {
        ::fixy::report(::fixy::Sink::Err, "[skipped] cannot make a scratch directory under /tmp\n");
        return 0;
    }
    if (const int rc = cold_writer_commit_succeeds(scratch); rc != 0) return rc;
    if (const int rc = no_replace_commit_refuses_an_existing_target(scratch); rc != 0) return rc;
    return 0;
}
