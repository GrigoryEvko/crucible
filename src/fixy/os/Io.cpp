// The io_uring door of fixy (fixy/os/Io.h): the setup of a ring, its maps
// and its release, and the two transfers between descriptors.  It issues
// the raw primitive of foundation/os/Syscall.h, and it is the one includer of
// <linux/io_uring.h>.  It includes no header of the C library, because some
// declarations of the two kinds of header conflict.

#include <fixy/os/Io.h>

#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>
#include <foundation/os/MapWords.h>
#include <foundation/os/Syscall.h>

#include <linux/io_uring.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace fixy::io {

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

// The bits of the ring flags of fixy/os/Io.h are the bits of the kernel
// header.
static_assert(detail::ring_setup_io_poll == IORING_SETUP_IOPOLL,
              "ring_setup_io_poll is not IORING_SETUP_IOPOLL of <linux/io_uring.h>");
static_assert(detail::ring_setup_sq_poll == IORING_SETUP_SQPOLL,
              "ring_setup_sq_poll is not IORING_SETUP_SQPOLL of <linux/io_uring.h>");
static_assert(detail::ring_setup_coop_taskrun == IORING_SETUP_COOP_TASKRUN,
              "ring_setup_coop_taskrun is not IORING_SETUP_COOP_TASKRUN of <linux/io_uring.h>");
static_assert(detail::ring_setup_single_issuer == IORING_SETUP_SINGLE_ISSUER,
              "ring_setup_single_issuer is not IORING_SETUP_SINGLE_ISSUER of <linux/io_uring.h>");
static_assert(detail::ring_setup_defer_taskrun == IORING_SETUP_DEFER_TASKRUN,
              "ring_setup_defer_taskrun is not IORING_SETUP_DEFER_TASKRUN of <linux/io_uring.h>");

// The protection and the flags of each map of a ring: the kernel and the
// process share the pages, and the kernel fills the page tables at the map.
constexpr int kRingProtection = os::kProtectReadWrite;
constexpr int kRingFlags = os::kMapShared | os::kMapPopulate;

// The address of a map of the part at OFFSET of the ring DESCRIPTOR, or the
// error of the map.
[[nodiscard]] core::Result<void*, os::Errno> map_part(int descriptor, std::size_t bytes,
                                                      std::uint64_t offset) noexcept {
    CRUCIBLE_TRY(address, os::result_of(os::raw_syscall(os::Syscall::mmap, os::Word{}, bytes, kRingProtection,
                                                        kRingFlags, descriptor, offset)));
    return __builtin_bit_cast(void*, address);
}

void unmap_part(void* address, std::size_t bytes) noexcept {
    static_cast<void>(os::raw_syscall(os::Syscall::munmap, address, bytes));
}

void close_ring(int descriptor) noexcept { static_cast<void>(os::raw_syscall(os::Syscall::close, descriptor)); }

// One step of a transfer: the count that it moved, zero at the end of the
// source, or the error of the kernel.
using Step = core::Result<std::uint64_t, os::Errno>;

// The loop that the two transfers share.  STEP moves at most the count that
// it takes.  Complexity: one call of STEP for each part that the kernel
// moves, and one more call at the end of the source.
template <typename MakeStep>
[[nodiscard]] core::Result<std::size_t, os::Errno> transfer(std::size_t length, MakeStep make_step) noexcept {
    std::size_t moved = 0;
    bool is_at_end = false;
    bool has_failed = false;
    os::Errno failure = os::Errno::eio;
    while (!is_at_end && !has_failed && moved < length) {
        make_step(length - moved)
            .match(
                [&](std::uint64_t taken) {
                    is_at_end = taken == 0;
                    moved += static_cast<std::size_t>(taken);
                },
                [&](os::Errno error) {
                    if (error != os::Errno::eintr) {
                        failure = error;
                        has_failed = true;
                    }
                });
    }
    if (has_failed) {
        return core::err(failure);
    }
    return moved;
}

}  // namespace

core::Result<detail::RingParts, os::Errno> detail::set_up_ring_(std::uint32_t sq_entries, std::uint32_t cq_entries,
                                                                std::uint32_t flags) noexcept {
    ::io_uring_params params{};
    params.flags = flags;
    if (cq_entries != 0) {
        params.cq_entries = cq_entries;
        params.flags |= IORING_SETUP_CQSIZE;
    }
    auto made =
        os::result_of(os::raw_syscall(os::Syscall::io_uring_setup, sq_entries, static_cast<void const*>(&params)));
    if (made.is_err()) {
        return core::err(made.err().expect("the setup of the ring gave an error"));
    }
    int const descriptor = static_cast<int>(std::move(made).expect("the setup of the ring gave its descriptor"));

    std::size_t const sq_ring_bytes = static_cast<std::size_t>(params.sq_off.array)
                                    + static_cast<std::size_t>(params.sq_entries) * sizeof(std::uint32_t);
    std::size_t const cq_ring_bytes = static_cast<std::size_t>(params.cq_off.cqes)
                                    + static_cast<std::size_t>(params.cq_entries) * sizeof(::io_uring_cqe);
    std::size_t const sqes_bytes = static_cast<std::size_t>(params.sq_entries) * sizeof(::io_uring_sqe);

    auto sq_ring = map_part(descriptor, sq_ring_bytes, IORING_OFF_SQ_RING);
    if (sq_ring.is_err()) {
        close_ring(descriptor);
        return core::err(sq_ring.err().expect("the map of the submission ring gave an error"));
    }
    void* const sq_address = std::move(sq_ring).expect("the map of the submission ring gave its address");

    // Linux 5.4 and later hand back one allocation that both rings live in,
    // and aliasing them is the documented contract.  A zero size tells the
    // release not to unmap the second one.
    void* cq_address = sq_address;
    std::size_t cq_bytes_to_unmap = 0;
    if ((params.features & IORING_FEAT_SINGLE_MMAP) == 0) {
        auto cq_ring = map_part(descriptor, cq_ring_bytes, IORING_OFF_CQ_RING);
        if (cq_ring.is_err()) {
            unmap_part(sq_address, sq_ring_bytes);
            close_ring(descriptor);
            return core::err(cq_ring.err().expect("the map of the completion ring gave an error"));
        }
        cq_address = std::move(cq_ring).expect("the map of the completion ring gave its address");
        cq_bytes_to_unmap = cq_ring_bytes;
    }

    auto sqes = map_part(descriptor, sqes_bytes, IORING_OFF_SQES);
    if (sqes.is_err()) {
        if (cq_bytes_to_unmap != 0) {
            unmap_part(cq_address, cq_bytes_to_unmap);
        }
        unmap_part(sq_address, sq_ring_bytes);
        close_ring(descriptor);
        return core::err(sqes.err().expect("the map of the submission entries gave an error"));
    }
    void* const sqes_address = std::move(sqes).expect("the map of the submission entries gave its address");

    return RingParts{.descriptor = descriptor,
                     .sq_ring = sq_address,
                     .sq_size = sq_ring_bytes,
                     .cq_ring = cq_address,
                     .cq_size = cq_bytes_to_unmap,
                     .sqes = sqes_address,
                     .sqes_size = sqes_bytes,
                     .sq_entries = params.sq_entries,
                     .cq_entries = params.cq_entries};
}

void IoUringRing::release_() noexcept {
    if (parts_.sqes != nullptr) {
        unmap_part(parts_.sqes, parts_.sqes_size);
    }
    if (parts_.cq_size != 0 && parts_.cq_ring != nullptr) {
        unmap_part(parts_.cq_ring, parts_.cq_size);
    }
    if (parts_.sq_ring != nullptr) {
        unmap_part(parts_.sq_ring, parts_.sq_size);
    }
    if (parts_.descriptor >= 0) {
        close_ring(parts_.descriptor);
    }
    parts_ = detail::RingParts{};
}

namespace detail {

core::Result<std::size_t, os::Errno> send_file_(int destination, int source, std::int64_t source_offset,
                                                std::size_t length) noexcept {
    std::int64_t offset = source_offset;
    return transfer(length, [&](std::size_t rest) -> Step {
        return os::result_of(
            os::raw_syscall(os::Syscall::sendfile, destination, source, static_cast<void const*>(&offset), rest));
    });
}

core::Result<std::size_t, os::Errno> copy_file_range_(int source, std::int64_t source_offset, int destination,
                                                      std::int64_t destination_offset, std::size_t length) noexcept {
    std::int64_t in_offset = source_offset;
    std::int64_t out_offset = destination_offset;
    return transfer(length, [&](std::size_t rest) -> Step {
        return os::result_of(os::raw_syscall(os::Syscall::copy_file_range, source, static_cast<void const*>(&in_offset),
                                             destination, static_cast<void const*>(&out_offset), rest, 0));
    });
}

}  // namespace detail

}  // namespace fixy::io
