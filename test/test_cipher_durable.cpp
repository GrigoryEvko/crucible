// The durable writers of the Cipher, run against a real directory.
//
// The negative-compile fixtures of crucible/cipher/CipherDurable.h say what
// the gates refuse and nothing about what the mints do.  This file runs the
// mints.  A cold writer whose commit returned ENOSYS on every call would
// pass each fixture, and the first case here refuses it.
//
// Everything runs in a scratch directory of the base (scratch_dir.h), whose
// owner removes it at the end.  When the kernel refuses the scratch
// directory, the test reports a skip and does not fail.

#include <crucible/cipher/CipherDurable.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/effects/Effect.h>

#include "scratch_dir.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace durable = crucible::cipher::durable;
namespace src = fixy::tags::source;

namespace {

using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

using crucible::test::ScratchPath;

// Every path the surface takes is Path<Sanitized>, and the only way to
// one is through the sanitizer: tag the text External, then launder it.
[[nodiscard]] fixy::Path<src::Sanitized> sanitized(ScratchPath const& path) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(path.c_text().view()))
        .expect("each test path passes the sanitizer");
}

inline constexpr std::size_t kPayloadBytes = 4096;

using Payload = ::fixy::FixedArray<std::byte, kPayloadBytes>;

[[nodiscard]] Payload pattern_of(std::uint8_t salt) {
    Payload pattern{};
    for (::fixy::Index<kPayloadBytes> const index : pattern.indices()) {
        std::byte const value{static_cast<std::uint8_t>((index.raw() * 7u + salt) & 0xFFu)};
        pattern.with_element(index, [value](std::byte& element) noexcept { element = value; });
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

// The error of a result, or the error number zero when it holds a value.
template <typename T>
[[nodiscard]] foundation::os::Errno error_of(fixy::Result<T, foundation::os::Errno> const& result) noexcept {
    return result.err().value_or(foundation::os::Errno{});
}

// The error number of a result, or zero when it holds a value.
template <typename T>
[[nodiscard]] int errno_of(fixy::Result<T, foundation::os::Errno> const& result) noexcept {
    return std::to_underlying(error_of(result));
}

// Reads a file back through the file door of fixy/os/Fs.h, and compares
// each byte with the bytes that a writer put.
[[nodiscard]] bool file_holds(ScratchPath const& path, const Payload& expected) {
    IoBlockCtx ctx{eff::testing::test()};
    auto reader = opened(fs::mint_file<fs::read_only>(ctx, sanitized(path)));
    if (reader.is_err()) return false;
    fs::FileOf<fs::read_only> const file = std::move(reader).expect("the reader opened");
    Payload readback{};
    // A failed read gives the count zero, so it returns false too.
    auto const read_count = readback.with_mut(
        [&ctx, &file](auto run) noexcept { return fs::read_full(ctx, file.ref(), run.view()).value_or(0); });
    if (read_count != kPayloadBytes) return false;
    return readback == expected;
}

// The cold writer, end to end.  cold_writer_stance pins Rename, and this
// leg is what says the whole sequence — open with O_SYNC, write, fsync,
// commit — reaches the target.  A commit body that returned ENOSYS
// before it consulted a filesystem fails this leg.
[[nodiscard]] int cold_writer_commit_succeeds(fs::TemporaryDirectory const& scratch) {
    IoBlockCtx ctx{eff::testing::test()};
    const Payload pattern = pattern_of(2);

    const ScratchPath tmp = crucible::test::make_scratch_path(scratch, "cold.tmp");
    const ScratchPath target = crucible::test::make_scratch_path(scratch, "cold");

    auto opened_writer = opened(durable::mint_cold_writer<>(ctx, sanitized(tmp)));
    if (opened_writer.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "mint_cold_writer failed (errno {})\n", errno_of(opened_writer));
        return 1;
    }
    auto const writer = std::move(opened_writer).expect("the cold writer opened");
    if (auto written = fs::write_full(ctx, writer.file(), pattern.view()); written.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "writing through the cold writer failed (errno {})\n", errno_of(written));
        return 1;
    }
    if (auto synced = writer.sync(ctx); synced.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "the fsync of the cold writer failed (errno {})\n", errno_of(synced));
        return 1;
    }
    const auto committed = writer.commit_atomic(ctx, sanitized(tmp), sanitized(target));
    if (committed.is_err()) {
        if (error_of(committed) == foundation::os::Errno::enosys) {
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
[[nodiscard]] int no_replace_commit_refuses_an_existing_target(fs::TemporaryDirectory const& scratch) {
    IoBlockCtx ctx{eff::testing::test()};
    const Payload pattern = pattern_of(3);

    const ScratchPath tmp = crucible::test::make_scratch_path(scratch, "warm.tmp");
    const ScratchPath occupied = crucible::test::make_scratch_path(scratch, "occupied");
    const ScratchPath fresh = crucible::test::make_scratch_path(scratch, "warm");

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
    if (auto written = fs::write_full(ctx, writer.file(), pattern.view()); written.is_err()) {
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
    if (error_of(refused) == foundation::os::Errno::einval) {
        ::fixy::check::pass(
            kTest, "[skipped] this filesystem has no RENAME_NOREPLACE; the errno is passed through as designed\n");
        return 0;
    }
    if (error_of(refused) != foundation::os::Errno::eexist) {
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
    fixy::Result<fs::TemporaryDirectory, foundation::os::Errno> made = crucible::test::make_scratch_directory(kTest);
    if (made.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "[skipped] cannot make a scratch directory under /tmp\n");
        return 0;
    }
    const fs::TemporaryDirectory scratch = std::move(made).expect("the scratch directory is made");
    if (const int rc = cold_writer_commit_succeeds(scratch); rc != 0) return rc;
    if (const int rc = no_replace_commit_refuses_an_existing_target(scratch); rc != 0) return rc;
    return 0;
}
