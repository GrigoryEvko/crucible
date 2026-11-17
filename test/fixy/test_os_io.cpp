// The io_uring and zero-copy surfaces, run against the kernel.
//
// The negative-compile fixtures in test/fixy/neg say what the gates
// refuse and nothing about what the ring mint and the transfer do, so
// every case here sets up a real ring or moves real bytes.
//
// The bit folds, the power-of-two rule and the gate answers stay in the
// header, where they fire wherever the surface is used.  What is here is
// what only a running kernel can answer: that the ring the setup call
// returns is mapped and sized as asked, and that a zero-copy transfer
// puts the source bytes in the destination and stops at the end of the
// source.
//
// Legs the kernel refuses are skipped rather than failed.  io_uring can
// be off entirely (ENOSYS), disabled by sysctl or seccomp (EPERM), and
// copy_file_range refuses some filesystem pairs.  None of those is a
// defect in this code, and a test that fails on them reports the
// environment rather than the tree.

#include <fixy/Core.h>
#include <fixy/os/Path.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Io.h>

#include "../scratch_dir.h"

#include <linux/io_uring.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace src = fixy::tags::source;

namespace {

using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

using Engine = fixy::atom::io::engine<fixy::io::engine::IoUring>;
using Sq8 = fixy::atom::io::sq_entries<8>;
using Cq16 = fixy::atom::io::cq_entries<16>;
using Sendfile = fixy::atom::io::zerocopy<fixy::io::zerocopy::Sendfile>;
using CopyFileRange = fixy::atom::io::zerocopy<fixy::io::zerocopy::CopyFileRange>;

inline constexpr std::size_t kPayloadBytes = 4096;

// Every path the file surface takes is Path<Sanitized>, and the only way
// to one is through the sanitizer.
[[nodiscard]] fixy::Path<src::Sanitized> sanitized(const std::string& raw) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(fixy::mint_foreign_text(raw.data(), raw.size())))
        .expect("each test path passes the sanitizer");
}

using crucible::test::ScratchDir;

// The prefix of the scratch directories of this test under /tmp.
inline constexpr std::string_view kScratchPrefix = "fixy-io-test";

// The source and the destination of one transfer: two files in a scratch
// directory, the source filled with a pattern.
struct TransferFiles final {
    fs::OwnedFd source;
    fs::OwnedFd destination;
};

using MadeFd = fixy::Result<fs::OwnedFd, foundation::os::Errno>;
using MadeFiles = fixy::Result<TransferFiles, foundation::os::Errno>;
using MadeRing = fixy::Result<fixy::io::IoUringRing, foundation::os::Errno>;

// The descriptor of a mint, or the Errno of the open that failed.  The door
// gives the Errno in its Result and does not set errno.
[[nodiscard]] MadeFd descriptor_of(fixy::Result<fixy::Linear<fs::OwnedFd>, foundation::os::Errno> minted) {
    return std::move(minted).match(
        [](fixy::Linear<fs::OwnedFd> opened) -> MadeFd { return std::move(opened).consume(); },
        [](foundation::os::Errno error) -> MadeFd { return fixy::err(error); });
}

// The ring of a mint, out of its Linear, or the Errno of the setup.
[[nodiscard]] MadeRing ring_of(fixy::Result<fixy::Linear<fixy::io::IoUringRing>, foundation::os::Errno> minted) {
    CRUCIBLE_TRY(made, std::move(minted));
    return std::move(made).consume();
}

// The two files of a transfer, or the Errno of the first open or write of
// the doors that failed.
[[nodiscard]] MadeFiles make_transfer_files(IoBlockCtx const& ctx, ScratchDir const& scratch,
                                            const std::uint8_t (&pattern)[kPayloadBytes]) {
    using ReadWrite = fixy::atom::fs::mode<fs::open_mode::ReadWrite>;
    CRUCIBLE_TRY(source, descriptor_of(fs::mint_file<ReadWrite>(ctx, sanitized(scratch.file("src")))));
    CRUCIBLE_TRY(destination, descriptor_of(fs::mint_file<ReadWrite>(ctx, sanitized(scratch.file("dst")))));
    CRUCIBLE_TRY(_, fs::write_full(ctx, source, fixy::bytes(pattern)));
    return TransferFiles{std::move(source), std::move(destination)};
}

// Whether the destination holds the pattern at its start.
[[nodiscard]] bool destination_holds(fs::OwnedFd const& destination, std::span<const std::uint8_t> pattern) {
    std::uint8_t readback[kPayloadBytes];
    const auto count = static_cast<::ssize_t>(pattern.size());
    return ::pread(destination.get(), readback, pattern.size(), 0) == count
        && std::memcmp(readback, pattern.data(), pattern.size()) == 0;
}

[[nodiscard]] int ring_setup_hands_back_a_mapped_ring() {
    IoBlockCtx ctx{eff::testing::test()};

    // The ring leaves the Linear in ring_of, and it keeps the descriptor
    // and the mappings, so the release happens once, when this returns.
    MadeRing ring = ring_of(fixy::io::mint_io_uring_ring<Engine, Sq8, Cq16>(ctx));
    if (ring.is_err()) {
        // ENOSYS: no io_uring in this kernel.  EPERM: disabled by
        // sysctl or refused by a seccomp filter.
        fixy::report(fixy::Sink::Err, "[skipped] io_uring_setup is unavailable here (errno {})\n",
                     foundation::os::error_number_of(ring));
        return 0;
    }
    fixy::io::IoUringRing owned = std::move(ring).expect("the setup gave a ring");

    const auto& handle = owned;
    if (!handle.is_valid()) {
        std::fprintf(stderr, "a successful setup handed back a ring that reports itself invalid\n");
        return 1;
    }
    if (handle.ring_fd() < 0) {
        std::fprintf(stderr, "the ring carries no descriptor\n");
        return 1;
    }

    // The kernel rounds the submission count up to a power of two and
    // may give more than asked.  Fewer would mean the ring cannot hold
    // what the caller sized it for.
    if (handle.sq_entries() < 8) {
        std::fprintf(stderr, "the ring reports %u submission entries, fewer than the 8 asked for\n",
                     handle.sq_entries());
        return 1;
    }
    if (handle.cq_entries() < handle.sq_entries()) {
        std::fprintf(stderr, "the completion queue is smaller than the submission queue\n");
        return 1;
    }

    // All three mappings have to be real, and the submission-entry array
    // has to be big enough to hold the entries the ring claims.
    if (handle.sq_ring() == nullptr) {
        std::fprintf(stderr, "the submission ring is not mapped\n");
        return 1;
    }
    if (handle.cq_ring() == nullptr) {
        std::fprintf(stderr, "the completion ring is not mapped\n");
        return 1;
    }
    if (handle.sqes() == nullptr) {
        std::fprintf(stderr, "the submission-entry array is not mapped\n");
        return 1;
    }
    if (handle.sqes_size() < static_cast<std::size_t>(handle.sq_entries()) * sizeof(::io_uring_sqe)) {
        std::fprintf(stderr, "the submission-entry array is too small for the entries the ring claims\n");
        return 1;
    }

    // The mapped rings are readable.  A byte touched here would fault if
    // the mapping were not there, which is what makes this more than a
    // pointer comparison.
    volatile const std::uint8_t first_sq_byte = *static_cast<const std::uint8_t*>(handle.sq_ring());
    volatile const std::uint8_t first_sqe_byte = *static_cast<const std::uint8_t*>(handle.sqes());
    (void)first_sq_byte;
    (void)first_sqe_byte;

    // A zero cq_ring_size is the SINGLE_MMAP marker and means the
    // completion ring aliases the submission one, so the destructor must
    // unmap it once rather than twice.
    if (handle.cq_ring_size() == 0 && handle.cq_ring() != handle.sq_ring()) {
        std::fprintf(stderr, "a zero completion-ring size marks the aliased mapping, but the two differ\n");
        return 1;
    }

    // A move takes the descriptor and the three mappings, and each accessor
    // of the ring that the move emptied gives the empty value.
    fixy::io::IoUringRing const moved = std::move(owned);
    bool const is_emptied = !owned.is_valid() && owned.ring_fd() < 0 && owned.sq_ring() == nullptr
                         && owned.cq_ring() == nullptr && owned.sqes() == nullptr && owned.sq_ring_size() == 0
                         && owned.cq_ring_size() == 0 && owned.sqes_size() == 0 && owned.sq_entries() == 0
                         && owned.cq_entries() == 0;
    if (!is_emptied || !moved.is_valid()) {
        fixy::report(fixy::Sink::Err, "a moved-from ring answered an accessor with a part of the ring\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int sendfile_moves_the_source_bytes() {
    IoBlockCtx ctx{eff::testing::test()};

    std::uint8_t pattern[kPayloadBytes];
    for (std::size_t index = 0; index < kPayloadBytes; ++index) {
        pattern[index] = static_cast<std::uint8_t>((index * 13u + 5u) & 0xFFu);
    }

    const ScratchDir scratch{kScratchPrefix};
    if (!scratch.is_ready()) {
        std::fprintf(stderr, "[skipped] no scratch directory under /tmp (errno %d)\n", errno);
        return 0;
    }
    MadeFiles made = make_transfer_files(ctx, scratch, pattern);
    if (made.is_err()) {
        fixy::report(fixy::Sink::Err, "making the two transfer files failed (errno {})\n",
                     foundation::os::error_number_of(made));
        return 1;
    }
    TransferFiles files = std::move(made).expect("the doors made the two transfer files");

    auto moved = fixy::io::zerocopy_transfer<Sendfile>(ctx, files.source, files.destination, kPayloadBytes);
    if (moved.is_err()) {
        fixy::report(fixy::Sink::Err, "sendfile failed (errno {})\n", foundation::os::error_number_of(moved));
        return 1;
    }
    std::size_t const moved_count = std::move(moved).expect("sendfile gave its count");
    if (moved_count != kPayloadBytes) {
        fixy::report(fixy::Sink::Err, "sendfile moved {} bytes of the {} asked for\n", moved_count, kPayloadBytes);
        return 1;
    }
    if (!destination_holds(files.destination, pattern)) {
        std::fprintf(stderr, "the destination does not hold the source bytes\n");
        return 1;
    }

    // A length past the end of the source moves the whole source and
    // stops there.  The count says where the source ended.
    auto past_end = fixy::io::zerocopy_transfer<Sendfile>(ctx, files.source, files.destination, 2 * kPayloadBytes);
    if (std::move(past_end).value_or(0) != kPayloadBytes) {
        std::fprintf(stderr, "a transfer past the end of the source did not stop at the end\n");
        return 1;
    }

    // A source offset starts the read at that byte, so a transfer from the
    // middle of the source moves the bytes of its second half.
    std::size_t const half = kPayloadBytes / 2;
    auto from_middle = fixy::io::zerocopy_transfer<Sendfile>(
        ctx, files.source, files.destination, kPayloadBytes,
        fixy::io::SourceOffsetOf{fs::FileOffset{static_cast<std::int64_t>(half)}});
    if (std::move(from_middle).value_or(0) != half) {
        std::fprintf(stderr, "a transfer from the middle of the source did not stop at its end\n");
        return 1;
    }
    std::uint8_t readback[kPayloadBytes];
    bool const holds_second_half =
        ::pread(files.destination.get(), readback, half, static_cast<::off_t>(2 * kPayloadBytes))
            == static_cast<::ssize_t>(half)
        && std::memcmp(readback, pattern + half, half) == 0;
    if (!holds_second_half) {
        std::fprintf(stderr, "a transfer from the middle of the source did not move its second half\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int copy_file_range_moves_the_source_bytes() {
    IoBlockCtx ctx{eff::testing::test()};

    std::uint8_t pattern[kPayloadBytes];
    for (std::size_t index = 0; index < kPayloadBytes; ++index) {
        pattern[index] = static_cast<std::uint8_t>((index * 31u + 17u) & 0xFFu);
    }

    const ScratchDir scratch{kScratchPrefix};
    if (!scratch.is_ready()) {
        std::fprintf(stderr, "[skipped] no scratch directory under /tmp (errno %d)\n", errno);
        return 0;
    }
    MadeFiles made = make_transfer_files(ctx, scratch, pattern);
    if (made.is_err()) {
        fixy::report(fixy::Sink::Err, "making the two transfer files failed (errno {})\n",
                     foundation::os::error_number_of(made));
        return 1;
    }
    TransferFiles files = std::move(made).expect("the doors made the two transfer files");

    auto moved = fixy::io::zerocopy_transfer<CopyFileRange>(ctx, files.source, files.destination, kPayloadBytes);
    if (moved.is_err()) {
        // copy_file_range refuses some filesystem pairs outright, and
        // which pairs it refuses has changed across kernels.
        fixy::report(fixy::Sink::Err, "[skipped] copy_file_range refused this pair (errno {})\n",
                     foundation::os::error_number_of(moved));
        return 0;
    }
    std::size_t const moved_count = std::move(moved).expect("copy_file_range gave its count");
    if (moved_count != kPayloadBytes) {
        fixy::report(fixy::Sink::Err, "copy_file_range moved {} bytes of the {} asked for\n", moved_count,
                     kPayloadBytes);
        return 1;
    }
    if (!destination_holds(files.destination, pattern)) {
        std::fprintf(stderr, "the destination does not hold the source bytes\n");
        return 1;
    }

    // The two offsets have two roles: the copy reads the second half of the
    // source, and it writes those bytes at the destination offset.
    std::size_t const half = kPayloadBytes / 2;
    std::size_t const placed_at = 4 * kPayloadBytes;
    auto placed = fixy::io::zerocopy_transfer<CopyFileRange>(
        ctx, files.source, files.destination, half,
        fixy::io::SourceOffsetOf{fs::FileOffset{static_cast<std::int64_t>(half)}},
        fixy::io::DestinationOffsetOf{fs::FileOffset{static_cast<std::int64_t>(placed_at)}});
    if (std::move(placed).value_or(0) != half) {
        std::fprintf(stderr, "a copy with two offsets did not move the second half of the source\n");
        return 1;
    }
    std::uint8_t readback[kPayloadBytes];
    bool const holds_second_half = ::pread(files.destination.get(), readback, half, static_cast<::off_t>(placed_at))
                                    == static_cast<::ssize_t>(half)
                                && std::memcmp(readback, pattern + half, half) == 0;
    if (!holds_second_half) {
        std::fprintf(stderr, "a copy with two offsets did not write at the destination offset\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = ring_setup_hands_back_a_mapped_ring(); rc != 0) return rc;
    if (const int rc = sendfile_moves_the_source_bytes(); rc != 0) return rc;
    if (const int rc = copy_file_range_moves_the_source_bytes(); rc != 0) return rc;
    return 0;
}
