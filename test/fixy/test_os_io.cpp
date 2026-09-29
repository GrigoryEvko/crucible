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

#include <fixy/Path.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Io.h>

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
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
        char pattern[] = "/tmp/fixy-io-test-XXXXXX";
        if (const char* made = ::mkdtemp(pattern); made != nullptr) path_ = made;
    }
    ~ScratchDir() {
        if (path_.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    ScratchDir(const ScratchDir&) = delete("a scratch directory is removed one time");
    ScratchDir& operator=(const ScratchDir&) = delete("a scratch directory is removed one time");

    [[nodiscard]] bool is_ready() const noexcept { return !path_.empty(); }
    [[nodiscard]] std::string file(const char* name) const { return path_ + "/" + name; }
};

// The source and the destination of one transfer: two files in a scratch
// directory, the source filled with a pattern.  An empty handle in either
// means that a file could not be made.
struct TransferFiles final {
    fs::OwnedFd source;
    fs::OwnedFd destination;
};

[[nodiscard]] TransferFiles make_transfer_files(IoBlockCtx const& ctx, ScratchDir const& scratch,
                                                std::span<const std::uint8_t> pattern) {
    TransferFiles files{};
    auto source = fs::mint_file<fixy::atom::fs::mode<fs::open_mode::ReadWrite>>(ctx, sanitized(scratch.file("src")));
    auto destination =
        fs::mint_file<fixy::atom::fs::mode<fs::open_mode::ReadWrite>>(ctx, sanitized(scratch.file("dst")));
    if (!source || !destination) return files;
    files.source = std::move(*source).consume();
    files.destination = std::move(*destination).consume();
    if (!fs::write_full(ctx, files.source, std::as_bytes(pattern))) {
        files.source = fs::OwnedFd{};
    }
    return files;
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

    auto ring = fixy::io::mint_io_uring_ring<Engine, Sq8, Cq16>(ctx);
    if (!ring) {
        // ENOSYS: no io_uring in this kernel.  EPERM: disabled by
        // sysctl or refused by a seccomp filter.
        std::fprintf(stderr, "[skipped] io_uring_setup is unavailable here (%s)\n", ring.error().message().c_str());
        return 0;
    }

    const auto& handle = ring->peek();
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
    if (handle.sq_ring() == MAP_FAILED || handle.sq_ring() == nullptr) {
        std::fprintf(stderr, "the submission ring is not mapped\n");
        return 1;
    }
    if (handle.cq_ring() == MAP_FAILED || handle.cq_ring() == nullptr) {
        std::fprintf(stderr, "the completion ring is not mapped\n");
        return 1;
    }
    if (handle.sqes() == MAP_FAILED || handle.sqes() == nullptr) {
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

    // Consuming out of the Linear leaves the ring owning the descriptor
    // and the mappings, so the release happens once, when this returns.
    auto owned = std::move(*ring).consume();
    if (!owned.is_valid()) {
        std::fprintf(stderr, "the ring lost its descriptor on the way out of the Linear\n");
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

    ScratchDir scratch;
    if (!scratch.is_ready()) {
        std::fprintf(stderr, "[skipped] no scratch directory under /tmp (errno %d)\n", errno);
        return 0;
    }
    TransferFiles files = make_transfer_files(ctx, scratch, pattern);
    if (!files.source.is_open() || !files.destination.is_open()) {
        std::fprintf(stderr, "making the two transfer files failed (errno %d)\n", errno);
        return 1;
    }

    const auto moved = fixy::io::zerocopy_transfer<Sendfile>(ctx, files.source, files.destination, kPayloadBytes);
    if (!moved) {
        std::fprintf(stderr, "sendfile failed (%s)\n", moved.error().message().c_str());
        return 1;
    }
    if (*moved != kPayloadBytes) {
        std::fprintf(stderr, "sendfile moved %zu bytes of the %zu asked for\n", *moved, kPayloadBytes);
        return 1;
    }
    if (!destination_holds(files.destination, pattern)) {
        std::fprintf(stderr, "the destination does not hold the source bytes\n");
        return 1;
    }

    // A length past the end of the source moves the whole source and
    // stops there.  The count says where the source ended.
    const auto past_end =
        fixy::io::zerocopy_transfer<Sendfile>(ctx, files.source, files.destination, 2 * kPayloadBytes);
    if (!past_end || *past_end != kPayloadBytes) {
        std::fprintf(stderr, "a transfer past the end of the source did not stop at the end\n");
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

    ScratchDir scratch;
    if (!scratch.is_ready()) {
        std::fprintf(stderr, "[skipped] no scratch directory under /tmp (errno %d)\n", errno);
        return 0;
    }
    TransferFiles files = make_transfer_files(ctx, scratch, pattern);
    if (!files.source.is_open() || !files.destination.is_open()) {
        std::fprintf(stderr, "making the two transfer files failed (errno %d)\n", errno);
        return 1;
    }

    const auto moved = fixy::io::zerocopy_transfer<CopyFileRange>(ctx, files.source, files.destination, kPayloadBytes);
    if (!moved) {
        // copy_file_range refuses some filesystem pairs outright, and
        // which pairs it refuses has changed across kernels.
        std::fprintf(stderr, "[skipped] copy_file_range refused this pair (%s)\n", moved.error().message().c_str());
        return 0;
    }
    if (*moved != kPayloadBytes) {
        std::fprintf(stderr, "copy_file_range moved %zu bytes of the %zu asked for\n", *moved, kPayloadBytes);
        return 1;
    }
    if (!destination_holds(files.destination, pattern)) {
        std::fprintf(stderr, "the destination does not hold the source bytes\n");
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
