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
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Io.h>
#include <fixy/os/Process.h>
#include <fixy/os/Socket.h>
#include <foundation/Quarantine.h>

#include "../scratch_dir.h"

#include <linux/io_uring.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
[[nodiscard]] fixy::Path<src::Sanitized> sanitized(crucible::test::ScratchPath const& path) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(path.c_text().view()))
        .expect("each test path passes the sanitizer");
}

using ReadWriteFile = fs::FileOf<fixy::atom::fs::mode<fs::open_mode::ReadWrite>>;
using ReadOnlyFile = fs::FileOf<fs::read_only>;

// The source and the destination of one transfer: two files in a scratch
// directory, the source filled with a pattern.
struct TransferFiles final {
    ReadWriteFile source;
    ReadWriteFile destination;
};

using MadeFiles = fixy::Result<TransferFiles, foundation::os::Errno>;
using MadeRing = fixy::Result<fixy::io::IoUringRing, foundation::os::Errno>;

// The file of a mint, or the Errno of the open that failed.  The door gives
// the Errno in its Result and does not set errno.
template <class File>
[[nodiscard]] fixy::Result<File, foundation::os::Errno>
descriptor_of(fixy::Result<fixy::Linear<File>, foundation::os::Errno> minted) {
    using Made = fixy::Result<File, foundation::os::Errno>;
    return std::move(minted).match([](fixy::Linear<File> opened) -> Made { return std::move(opened).consume(); },
                                   [](foundation::os::Errno error) -> Made { return fixy::err(error); });
}

// The ring of a mint, out of its Linear, or the Errno of the setup.
[[nodiscard]] MadeRing ring_of(fixy::Result<fixy::Linear<fixy::io::IoUringRing>, foundation::os::Errno> minted) {
    CRUCIBLE_TRY(made, std::move(minted));
    return std::move(made).consume();
}

// The two files of a transfer, or the Errno of the first open or write of
// the doors that failed.
[[nodiscard]] MadeFiles make_transfer_files(IoBlockCtx const& ctx, fs::TemporaryDirectory const& scratch,
                                            const std::uint8_t (&pattern)[kPayloadBytes]) {
    using ReadWrite = fixy::atom::fs::mode<fs::open_mode::ReadWrite>;
    CRUCIBLE_TRY(source, descriptor_of(fs::mint_file<ReadWrite>(
                             ctx, sanitized(crucible::test::make_scratch_path(scratch, "src")))));
    CRUCIBLE_TRY(destination, descriptor_of(fs::mint_file<ReadWrite>(
                                  ctx, sanitized(crucible::test::make_scratch_path(scratch, "dst")))));
    CRUCIBLE_TRY(_, fs::write_full(ctx, source.ref(), fixy::bytes(pattern)));
    return TransferFiles{std::move(source), std::move(destination)};
}

// The bytes that the destination file of the scratch directory holds at
// offset, read through a second descriptor of the file door, equal expected.
[[nodiscard]] bool destination_holds_at(IoBlockCtx const& ctx, fs::TemporaryDirectory const& scratch,
                                        std::size_t offset, fixy::View<std::byte const> expected) {
    fixy::Result<ReadOnlyFile, foundation::os::Errno> opened =
        descriptor_of(fs::mint_file<fs::read_only>(ctx, sanitized(crucible::test::make_scratch_path(scratch, "dst"))));
    if (opened.is_err()) return false;
    ReadOnlyFile const reader = std::move(opened).expect("the destination opened for a read");
    auto const [count, whole] = fixy::build_fixed_array<std::byte, 5 * kPayloadBytes>(
        [&ctx, &reader](auto run) noexcept { return fs::read_full(ctx, reader.ref(), std::move(run)).value_or(0); });
    if (count < offset + expected.size()) return false;
    return fixy::equal(whole.view().window(offset, expected.size()).expect("the window is inside the bytes read"),
                       expected);
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

    fixy::TestRunnerCtx const test{eff::testing::test()};
    fixy::Result<fs::TemporaryDirectory, foundation::os::Errno> scratch_made =
        crucible::test::make_scratch_directory(test);
    if (scratch_made.is_err()) {
        fixy::report(fixy::Sink::Err, "[skipped] no scratch directory under /tmp (errno {})\n",
                     foundation::os::error_number_of(scratch_made));
        return 0;
    }
    fs::TemporaryDirectory const scratch = std::move(scratch_made).expect("the scratch directory is made");
    MadeFiles made = make_transfer_files(ctx, scratch, pattern);
    if (made.is_err()) {
        fixy::report(fixy::Sink::Err, "making the two transfer files failed (errno {})\n",
                     foundation::os::error_number_of(made));
        return 1;
    }
    TransferFiles files = std::move(made).expect("the doors made the two transfer files");

    auto moved = fixy::io::zerocopy_transfer<Sendfile>(ctx, files.source.ref(), files.destination.ref(), kPayloadBytes);
    if (moved.is_err()) {
        fixy::report(fixy::Sink::Err, "sendfile failed (errno {})\n", foundation::os::error_number_of(moved));
        return 1;
    }
    std::size_t const moved_count = std::move(moved).expect("sendfile gave its count");
    if (moved_count != kPayloadBytes) {
        fixy::report(fixy::Sink::Err, "sendfile moved {} bytes of the {} asked for\n", moved_count, kPayloadBytes);
        return 1;
    }
    if (!destination_holds_at(ctx, scratch, 0, fixy::bytes(pattern))) {
        fixy::report(fixy::Sink::Err, "the destination does not hold the source bytes\n");
        return 1;
    }

    // A length past the end of the source moves the whole source and
    // stops there.  The count says where the source ended.
    auto past_end =
        fixy::io::zerocopy_transfer<Sendfile>(ctx, files.source.ref(), files.destination.ref(), 2 * kPayloadBytes);
    if (std::move(past_end).value_or(0) != kPayloadBytes) {
        std::fprintf(stderr, "a transfer past the end of the source did not stop at the end\n");
        return 1;
    }

    // A source offset starts the read at that byte, so a transfer from the
    // middle of the source moves the bytes of its second half.
    std::size_t const half = kPayloadBytes / 2;
    auto from_middle = fixy::io::zerocopy_transfer<Sendfile>(
        ctx, files.source.ref(), files.destination.ref(), kPayloadBytes,
        fixy::io::SourceOffsetOf{fs::FileOffset{static_cast<std::int64_t>(half)}});
    if (std::move(from_middle).value_or(0) != half) {
        fixy::report(fixy::Sink::Err, "a transfer from the middle of the source did not stop at its end\n");
        return 1;
    }
    fixy::View<std::byte const> const second_half =
        fixy::View<std::byte const>{fixy::bytes(pattern)}.window(half, half).expect("the half is in the pattern");
    if (!destination_holds_at(ctx, scratch, 2 * kPayloadBytes, second_half)) {
        fixy::report(fixy::Sink::Err, "a transfer from the middle of the source did not move its second half\n");
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

    fixy::TestRunnerCtx const test{eff::testing::test()};
    fixy::Result<fs::TemporaryDirectory, foundation::os::Errno> scratch_made =
        crucible::test::make_scratch_directory(test);
    if (scratch_made.is_err()) {
        fixy::report(fixy::Sink::Err, "[skipped] no scratch directory under /tmp (errno {})\n",
                     foundation::os::error_number_of(scratch_made));
        return 0;
    }
    fs::TemporaryDirectory const scratch = std::move(scratch_made).expect("the scratch directory is made");
    MadeFiles made = make_transfer_files(ctx, scratch, pattern);
    if (made.is_err()) {
        fixy::report(fixy::Sink::Err, "making the two transfer files failed (errno {})\n",
                     foundation::os::error_number_of(made));
        return 1;
    }
    TransferFiles files = std::move(made).expect("the doors made the two transfer files");

    auto moved =
        fixy::io::zerocopy_transfer<CopyFileRange>(ctx, files.source.ref(), files.destination.ref(), kPayloadBytes);
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
    if (!destination_holds_at(ctx, scratch, 0, fixy::bytes(pattern))) {
        fixy::report(fixy::Sink::Err, "the destination does not hold the source bytes\n");
        return 1;
    }

    // The two offsets have two roles: the copy reads the second half of the
    // source, and it writes those bytes at the destination offset.
    std::size_t const half = kPayloadBytes / 2;
    std::size_t const placed_at = 4 * kPayloadBytes;
    auto placed = fixy::io::zerocopy_transfer<CopyFileRange>(
        ctx, files.source.ref(), files.destination.ref(), half,
        fixy::io::SourceOffsetOf{fs::FileOffset{static_cast<std::int64_t>(half)}},
        fixy::io::DestinationOffsetOf{fs::FileOffset{static_cast<std::int64_t>(placed_at)}});
    if (std::move(placed).value_or(0) != half) {
        fixy::report(fixy::Sink::Err, "a copy with two offsets did not move the second half of the source\n");
        return 1;
    }
    fixy::View<std::byte const> const second_half =
        fixy::View<std::byte const>{fixy::bytes(pattern)}.window(half, half).expect("the half is in the pattern");
    if (!destination_holds_at(ctx, scratch, placed_at, second_half)) {
        fixy::report(fixy::Sink::Err, "a copy with two offsets did not write at the destination offset\n");
        return 1;
    }
    return 0;
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a sendfile to a socket with no peer raises no SIGPIPE in its thread")
#include <pthread.h>
#include <signal.h>

namespace {

// Gives SIGPIPE its default action, and a mask of the thread that does not
// block it.  A runner of the test can start it with SIGPIPE ignored, so the
// child sets the default first, and a SIGPIPE then ends the child.
[[nodiscard]] bool use_default_pipe_action() noexcept {
    struct ::sigaction default_action{};
    default_action.sa_handler = SIG_DFL;
    static_cast<void>(::sigemptyset(&default_action.sa_mask));
    ::sigset_t pipe{};
    static_cast<void>(::sigemptyset(&pipe));
    static_cast<void>(::sigaddset(&pipe, SIGPIPE));
    return ::sigaction(SIGPIPE, &default_action, nullptr) == 0 && ::pthread_sigmask(SIG_UNBLOCK, &pipe, nullptr) == 0;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// A sendfile to a TCP stream socket that is not connected gives epipe, and the
// thread gets no SIGPIPE, as each write door of the base does.  The transfer
// runs in a child, so a SIGPIPE that ends the process shows as a child that
// does not exit with 0.  The source is a file with no name in /tmp.
[[nodiscard]] static int sendfile_to_a_socket_with_no_peer_gives_epipe() {
    using SocketCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                       eff::Effect::Block, eff::Effect::Nondet>>;
    using TcpStream = fixy::net::socket_kind::TcpStream;
    const fixy::TestRunnerCtx test_ctx{eff::testing::test()};
    const fixy::process::ChildReport report =
        fixy::process::wait(
            fixy::process::spawn_child(
                test_ctx,
                [] noexcept {
                    if (!use_default_pipe_action()) fixy::fatal("the child does not set the default SIGPIPE action");
                    SocketCtx const ctx{eff::testing::test()};
                    fs::DirectoryHandle const tmp =
                        fs::open_directory(ctx,
                                           fixy::sanitize_path(fixy::mint_tagged<src::External>(fixy::TextView{"/tmp"}))
                                               .expect("/tmp passes the sanitizer"))
                            .expect("/tmp opens");
                    fs::TemporaryFile const source =
                        fs::mint_temporary_file_at(ctx, tmp.ref()).expect("the file of the source opens").consume();
                    fixy::FixedArray<std::byte, 64> bytes{};
                    bytes.fill(std::byte{0x5A});
                    if (fs::write_full(ctx, source.ref(), bytes.view()).is_err()) {
                        fixy::fatal("the test does not write the source of the sendfile");
                    }
                    fixy::net::SocketHandle<TcpStream> const socket =
                        fixy::net::mint_socket<TcpStream>(ctx).expect("the test makes a socket").consume();
                    auto const moved = fixy::io::zerocopy_transfer<Sendfile>(ctx, source.ref(), socket.ref(), 64);
                    if (moved.err().value_or(foundation::os::Errno{}) != foundation::os::Errno::epipe) {
                        fixy::fatal("a sendfile to a socket with no peer did not give epipe");
                    }
                })
                .expect("the test starts a child"))
            .expect("the test waits for its child");
    if (!report.is_exit_with(0)) {
        fixy::report(fixy::Sink::Err,
                     "a sendfile to a socket with no peer ended its process, and not with epipe and exit 0\n");
        return 1;
    }
    return 0;
}

int main() {
    if (const int rc = ring_setup_hands_back_a_mapped_ring(); rc != 0) return rc;
    if (const int rc = sendfile_moves_the_source_bytes(); rc != 0) return rc;
    if (const int rc = sendfile_to_a_socket_with_no_peer_gives_epipe(); rc != 0) return rc;
    if (const int rc = copy_file_range_moves_the_source_bytes(); rc != 0) return rc;
    return 0;
}
