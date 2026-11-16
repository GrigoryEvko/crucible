// A moved-from owner of fixy/os refuses each operation.
//
// A move takes the resource of an owner and leaves the owner empty.  Each
// door that takes the empty owner gives an error, or the empty value of the
// accessor, and no door reads the empty owner as a resource: a descriptor of
// -1 is no clock id, and an empty Dirfd is no anchor for an absolute path.
// The case of each owner moves a live owner, calls each operation on the
// empty one, and calls one operation on the owner that the move filled.  A
// leg that the host refuses, such as a PTP device that the host does not
// have, is skipped.
//
// This test holds the owners of a descriptor: OwnedFd, Dirfd and
// PtpClockReader.  The tests of the other owners of fixy/os hold the same
// check, on an owner that they mint: test_os_io for IoUringRing and
// test_owned_mmap for OwnedMmap.
//
// The files of the cases are entries of /proc, which each Linux host has.
// The cases read them, and each call that would make, rename or remove an
// entry names an entry of /proc that does not exist.  So the test changes
// no file, also on code that does not refuse the call.

#include <fixy/Core.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Io.h>
#include <fixy/os/Path.h>
#include <fixy/os/PerfEvent.h>
#include <fixy/os/Socket.h>
#include <fixy/os/Time.h>
#include <foundation/core/Array.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace src = fixy::tags::source;

namespace {

using foundation::os::Errno;

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

// The descriptor of a mint, or an empty handle when the open failed.
[[nodiscard]] fs::OwnedFd descriptor_of(fixy::Result<fixy::Linear<fs::OwnedFd>, Errno> minted) {
    return std::move(minted).match(
        [](fixy::Linear<fs::OwnedFd> opened) -> fs::OwnedFd { return std::move(opened).consume(); },
        [](Errno) -> fs::OwnedFd { return fs::OwnedFd{}; });
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

// True when result holds the error expected.
template <typename T>
[[nodiscard]] bool refuses_with(fixy::Result<T, Errno> const& result, Errno expected) {
    return result.err().value_or(Errno{}) == expected;
}

// Reports the operation that answered on a moved-from owner.
[[nodiscard]] int answered(fixy::TextView owner, fixy::TextView operation) {
    fixy::report(fixy::Sink::Err, "a moved-from {} answered {}\n", owner, operation);
    return 1;
}

// OwnedFd: each door of the file, socket, transfer and perf headers that
// takes a descriptor gives ebadf.
[[nodiscard]] int moved_from_descriptor_refuses() {
    using Sendfile = fixy::atom::io::zerocopy<fixy::io::zerocopy::Sendfile>;
    using CopyFileRange = fixy::atom::io::zerocopy<fixy::io::zerocopy::CopyFileRange>;
    fixy::TextView const name{"descriptor"};
    IoBlockCtx ctx{eff::testing::test()};
    fs::OwnedFd source = descriptor_of(fs::mint_file<fs::read_only>(ctx, sanitized("/proc/self/status")));
    if (!source.is_open()) return answered(name, "nothing: the open of /proc/self/status failed");
    fs::OwnedFd const live = std::move(source);

    foundation::core::FixedArray<std::byte, 4> bytes{};
    foundation::core::View<std::byte const> const sent{bytes.view()};
    if (source.is_open() || source.get() >= 0) return answered(name, "is_open or get");
    if (!refuses_with(fs::sync<fs::sync_op::Fdatasync>(ctx, source), Errno::ebadf)) return answered(name, "Fdatasync");
    if (!refuses_with(fs::sync<fs::sync_op::Fsync>(ctx, source), Errno::ebadf)) return answered(name, "Fsync");
    if (!refuses_with(fs::read_full(ctx, source, bytes.view()), Errno::ebadf)) return answered(name, "read_full");
    if (!refuses_with(fs::write_full(ctx, source, sent), Errno::ebadf)) return answered(name, "write_full");
    if (!refuses_with(fs::write_full(ctx, source, fixy::TextView{"text"}), Errno::ebadf)) {
        return answered(name, "write_full of a text");
    }
    if (!refuses_with(fs::file_size(ctx, source), Errno::ebadf)) return answered(name, "file_size");
    if (!refuses_with(fixy::net::send_bytes(ctx, source, sent), Errno::ebadf)) return answered(name, "send_bytes");
    if (!refuses_with(fixy::net::receive_bytes(ctx, source, bytes.view()), Errno::ebadf)) {
        return answered(name, "receive_bytes");
    }
    if (!refuses_with(fixy::net::bind_netlink(ctx, source), Errno::ebadf)) return answered(name, "bind_netlink");
    if (!refuses_with(fixy::perf::read_count(ctx, source), Errno::ebadf)) return answered(name, "read_count");
    // A transfer gives a std::expected, and each refusal of one is an error.
    if (fixy::io::zerocopy_transfer<Sendfile>(ctx, source, live, 1)
        || fixy::io::zerocopy_transfer<Sendfile>(ctx, live, source, 1)) {
        return answered(name, "a sendfile transfer");
    }
    if (fixy::io::zerocopy_transfer<CopyFileRange>(ctx, source, live, 1)
        || fixy::io::zerocopy_transfer<CopyFileRange>(ctx, live, source, 1)) {
        return answered(name, "a copy_file_range transfer");
    }
    if (source.release() >= 0) return answered(name, "release");
    if (fs::read_full(ctx, live, bytes.view()).is_err()) return answered(name, "nothing, and its new owner refused");
    return 0;
}

// Dirfd: each door under a directory gives ebadf, also for an absolute path,
// which the kernel reads with no regard to the descriptor.
[[nodiscard]] int moved_from_directory_refuses() {
    fixy::TextView const name{"directory"};
    IoBlockCtx ctx{eff::testing::test()};
    eff::Alloc const alloc = eff::testing::test().alloc;
    auto opened = fs::open_dirfd(ctx, sanitized("/proc"));
    if (opened.is_err()) return answered(name, "nothing: the open of /proc failed");
    fs::Dirfd source = std::move(opened).expect("/proc opened");
    fs::Dirfd const live = std::move(source);

    foundation::core::GrowText text = foundation::core::mint_grow_text(64);
    if (source.is_open() || source.get() >= 0) return answered(name, "is_open or get");
    if (!refuses_with(fs::sync<fs::sync_op::FsyncParentDir>(ctx, source), Errno::ebadf)) {
        return answered(name, "FsyncParentDir");
    }
    if (open_error_of(fs::mint_file_at<fs::read_only>(ctx, source, "/proc/self/status")) != Errno::ebadf) {
        return answered(name, "mint_file_at of an absolute path");
    }
    if (open_error_of(fs::mint_file_at<fs::read_only>(ctx, source, "uptime")) != Errno::ebadf) {
        return answered(name, "mint_file_at of a relative path");
    }
    if (!refuses_with(fs::open_dirfd_at(ctx, source, "/proc"), Errno::ebadf)) return answered(name, "open_dirfd_at");
    if (!refuses_with(fs::commit_atomic_at<fs::atomicity::Rename>(ctx, source, "/proc/no-entry", "/proc/no-target"),
                      Errno::ebadf)) {
        return answered(name, "commit_atomic_at");
    }
    if (!refuses_with(fs::unlink_at(ctx, source, "/proc/no-entry"), Errno::ebadf)) return answered(name, "unlink_at");
    if (!refuses_with(fs::make_directory_at(ctx, source, "/proc/no-entry"), Errno::ebadf)) {
        return answered(name, "make_directory_at");
    }
    if (!refuses_with(fs::has_entry_at(ctx, source, "/proc/self/status"), Errno::ebadf)) {
        return answered(name, "has_entry_at");
    }
    if (!refuses_with(fs::read_whole_at(ctx, alloc, source, "/proc/self/status", text), Errno::ebadf)) {
        return answered(name, "read_whole_at");
    }
    if (!fs::has_entry_at(ctx, live, "self").value_or(false))
        return answered(name, "nothing, and its new owner refused");
    return 0;
}

// PtpClockReader: the read of descriptor -1 gives ebadf.  Its clock id is
// CLOCK_THREAD_CPUTIME_ID, so a read with no check gives the CPU time of the
// thread as a PTP reading.  A host with /dev/ptp0 also moves a real reader.
[[nodiscard]] int moved_from_clock_reader_refuses() {
    fixy::TextView const name{"PTP clock reader"};
    if (!refuses_with(fixy::time::detail::read_ptp_clock_nanos(-1), Errno::ebadf)) {
        return answered(name, "the read of descriptor -1");
    }
    IoBlockCtx ctx{eff::testing::test()};
    std::uint16_t first_index = 0;
    auto minted = fixy::time::mint_ptp_clock_reader(
        ctx, fixy::mint_refined<fixy::bounded_above<std::uint16_t{255}>>(first_index));
    if (minted.is_err()) {
        fixy::report(fixy::Sink::Err, "[skipped] no /dev/ptp0 here, so no real PTP reader moves\n");
        return 0;
    }
    fixy::time::PtpClockReader source = std::move(minted).expect("the PTP mint gave a reader");
    fixy::time::PtpClockReader const live = std::move(source);
    eff::cap::Nondet const nondet = eff::testing::test().nondet;
    if (!refuses_with(source.read(nondet), Errno::ebadf)) return answered(name, "read");
    if (!refuses_with(source.caps(), Errno::ebadf)) return answered(name, "caps");
    if (refuses_with(live.read(nondet), Errno::ebadf)) return answered(name, "nothing, and its new owner refused");
    return 0;
}

}  // namespace

int main() {
    if (int const rc = moved_from_descriptor_refuses(); rc != 0) return rc;
    if (int const rc = moved_from_directory_refuses(); rc != 0) return rc;
    if (int const rc = moved_from_clock_reader_refuses(); rc != 0) return rc;
    return 0;
}
