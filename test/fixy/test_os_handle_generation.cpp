// A borrow of a descriptor fails closed after its Handle closes the descriptor.
//
// A borrow holds the number of its descriptor and the generation of the
// number (the stamp of a descriptor of fixy/os/Fd.h), and no address of its
// Handle.  Each case keeps a borrow past the close of its descriptor, and each
// door through the borrow gives ebadf.  The rule view-escape refuses a read of
// the borrow after an assignment of its Handle at compile time, so those cases
// read it in an opt-out region of the class PROBE:
//   - a copy that a helper keeps past the end of the Handle, and a later open
//     that takes the number again
//   - a move assignment over the Handle of the borrow
//   - the end of a Box that holds the Handle
//   - a close of the Handle and a new open into the same Handle, which takes
//     the number again
// A borrow taken before a move of its Handle stays open, because the move
// keeps the generation.  The last case gives a low limit of the slots of the
// generations to a real open: the door closes the number and gives emfile.
//
// The files are files with no name under /tmp, so the test leaves no file.

#include <fixy/Core.h>
#include <fixy/os/Fd.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/Quarantine.h>
#include <foundation/os/OpenFlags.h>

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

using FileRef = fs::FileRef<fs::temporary_file_rights>;

// A table that keeps a borrow.  A helper stores the borrow, so the store of
// the borrow of a local Handle is not in the frame of that Handle.
struct KeptBorrow {
    fixy::Option<FileRef> borrow{};
};

void keep(KeptBorrow& kept, FileRef borrow) noexcept { kept.borrow = fixy::some(borrow); }

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

// A file with no name under the directory, or an empty handle when the open
// failed.
[[nodiscard]] fs::TemporaryFile temporary_file(IoBlockCtx const& ctx, fs::DirectoryHandle const& directory) {
    return fs::mint_temporary_file_at(ctx, directory.ref())
        .match([](fixy::Linear<fs::TemporaryFile> opened) -> fs::TemporaryFile { return std::move(opened).consume(); },
               [](Errno) -> fs::TemporaryFile { return fs::TemporaryFile{}; });
}

// True when result holds the error expected.
template <typename T>
[[nodiscard]] bool refuses_with(fixy::Result<T, Errno> const& result, Errno expected) {
    return result.err().value_or(Errno{}) == expected;
}

// The size of the file of a Handle, or -1 when the read of the size failed.
[[nodiscard]] std::int64_t size_of(IoBlockCtx const& ctx, fs::TemporaryFile const& file) {
    return fs::file_size(ctx, file.ref()).value_or(-1);
}

// Reports the case and the door that answered through a borrow of a closed
// descriptor.
[[nodiscard]] int answered(fixy::TextView route, fixy::TextView door) {
    fixy::report(fixy::Sink::Err, "a borrow after {} answered {}\n", route, door);
    return 1;
}

// Each door of a file refuses the borrow: is_open, read_full, write_full,
// sync and file_size.
[[nodiscard]] int refuses_each_door(IoBlockCtx const& ctx, FileRef const& borrow, fixy::TextView route) {
    if (borrow.is_open()) return answered(route, "is_open");
    fixy::HeapArray<std::byte> buffer = fixy::mint_heap_array(eff::testing::test().alloc, std::size_t{4}, std::byte{0});
    [[maybe_unused]] auto [is_refused, returned] =
        fixy::with_mut(std::move(buffer), [&ctx, &borrow](auto run) noexcept {
            return refuses_with(fs::read_full(ctx, borrow, std::move(run)), Errno::ebadf);
        });
    if (!is_refused) return answered(route, "read_full");
    if (!refuses_with(fs::write_full(ctx, borrow, fixy::TextView{"text"}), Errno::ebadf)) {
        return answered(route, "write_full");
    }
    if (!refuses_with(fs::sync<fs::sync_op::Fsync>(ctx, borrow), Errno::ebadf)) return answered(route, "sync");
    if (!refuses_with(fs::file_size(ctx, borrow), Errno::ebadf)) return answered(route, "file_size");
    return 0;
}

// A copy of the borrow that a helper keeps outlives its Handle.  A later open
// takes the number of the closed descriptor, and the copy writes no byte into
// the later file.
[[nodiscard]] int kept_copy_refuses(IoBlockCtx const& ctx, fs::DirectoryHandle const& directory) {
    fixy::TextView const route{"the end of its Handle"};
    KeptBorrow kept{};
    {
        fs::TemporaryFile const file = temporary_file(ctx, directory);
        if (!file.is_open()) return answered(route, "nothing: the open of a file under /tmp failed");
        keep(kept, file.ref());
    }
    fs::TemporaryFile const later = temporary_file(ctx, directory);
    if (!later.is_open()) return answered(route, "nothing: the second open of a file under /tmp failed");
    return kept.borrow.match(
        [&ctx, &later, route](FileRef const stale) -> int {
            if (int const rc = refuses_each_door(ctx, stale, route); rc != 0) return rc;
            if (size_of(ctx, later) != 0) return answered(route, "write_full into the file that took the number");
            return 0;
        },
        [route]() -> int { return answered(route, "nothing: no borrow was kept"); });
}

// A move assignment closes the descriptor of the borrow and gives the Handle
// another descriptor.  The borrow writes no byte into the file of the new
// descriptor.
[[nodiscard]] int move_assignment_refuses(IoBlockCtx const& ctx, fs::DirectoryHandle const& directory) {
    fixy::TextView const route{"a move assignment over its Handle"};
    fs::TemporaryFile target = temporary_file(ctx, directory);
    fs::TemporaryFile other = temporary_file(ctx, directory);
    if (!target.is_open() || !other.is_open()) return answered(route, "nothing: an open under /tmp failed");
    FileRef const borrow = target.ref();
    target = std::move(other);
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a borrow from before the end of the descriptor reads its stale generation")
    if (!refuses_with(fs::write_full(ctx, borrow, fixy::TextView{"x"}), Errno::ebadf)) {
        return answered(route, "write_full");
    }
    if (size_of(ctx, target) != 0) return answered(route, "write_full into the file of the new descriptor");
    return refuses_each_door(ctx, borrow, route);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

// The end of a Box ends the Handle that it holds.  The borrow reads no byte of
// the freed Handle.
[[nodiscard]] int box_end_refuses(IoBlockCtx const& ctx, fs::DirectoryHandle const& directory) {
    fixy::TextView const route{"the end of the Box of its Handle"};
    eff::Alloc const alloc = eff::testing::test().alloc;
    fixy::Box<fs::TemporaryFile> owner = fixy::mint_box<fs::TemporaryFile>(alloc, temporary_file(ctx, directory));
    bool const is_open =
        ::foundation::core::with_view(owner, [](fs::TemporaryFile const& file) noexcept { return file.is_open(); });
    if (!is_open) return answered(route, "nothing: the open of a file under /tmp failed");
    FileRef const borrow = owner->ref();
    owner = fixy::mint_box<fs::TemporaryFile>(alloc, temporary_file(ctx, directory));
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a borrow from before the end of the descriptor reads its stale generation")
    return refuses_each_door(ctx, borrow, route);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

// A close of the Handle and a new open into the same Handle: the kernel gives
// the lowest free number, so the new open takes the number of the borrow
// again.  The borrow writes no byte into the new file.
[[nodiscard]] int reused_number_refuses(IoBlockCtx const& ctx, fs::DirectoryHandle const& directory) {
    fixy::TextView const route{"a close and an open that takes the number again"};
    fs::TemporaryFile slot = temporary_file(ctx, directory);
    if (!slot.is_open()) return answered(route, "nothing: the open of a file under /tmp failed");
    FileRef const borrow = slot.ref();
    slot = fs::TemporaryFile{};
    slot = temporary_file(ctx, directory);
    if (!slot.is_open()) return answered(route, "nothing: the second open of a file under /tmp failed");
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a borrow from before the end of the descriptor reads its stale generation")
    if (!refuses_with(fs::write_full(ctx, borrow, fixy::TextView{"x"}), Errno::ebadf)) {
        return answered(route, "write_full");
    }
    if (size_of(ctx, slot) != 0) return answered(route, "write_full into the file that took the number");
    return refuses_each_door(ctx, borrow, route);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

// A move of the Handle keeps the generation, so a borrow taken before the move
// stays open and writes into the file of the moved Handle.
[[nodiscard]] int borrow_before_move_stays_open(IoBlockCtx const& ctx, fs::DirectoryHandle const& directory) {
    fs::TemporaryFile first = temporary_file(ctx, directory);
    if (!first.is_open()) return answered("a move of its Handle", "nothing: the open of a file under /tmp failed");
    FileRef const borrow = first.ref();
    fs::TemporaryFile const moved = std::move(first);
    bool const is_written = borrow.is_open() && fs::write_full(ctx, borrow, fixy::TextView{"x"}).is_ok();
    if (!is_written || size_of(ctx, moved) != 1) {
        fixy::report(fixy::Sink::Err, "a borrow taken before a move of its Handle did not write its file\n");
        return 1;
    }
    return 0;
}

// The door of an open refuses a number at or past the slots of the
// generations: it closes the number and gives emfile.  A real number of the
// test is at or past 3, the number after the three standard streams, so a
// limit of 3 refuses it.  The next open takes the number again, which shows
// the close.  An error of the open passes through the door.
[[nodiscard]] int number_past_the_slots_refuses() {
    namespace detail = fixy::fs::detail;
    constexpr int flags = foundation::os::kOpenReadOnly | foundation::os::kOpenCloseOnExec;
    int const number = detail::open_descriptor_("/proc/self/status", flags, 0).value_or(-1);
    if (number < 3) {
        fixy::report(fixy::Sink::Err, "the open of /proc/self/status gave no number at or past 3\n");
        return 1;
    }
    if (!refuses_with(detail::OpenedDescriptorDoor::admit_below<3>(number), Errno::emfile)) {
        fixy::report(fixy::Sink::Err, "the door of an open admitted a number past its limit\n");
        return 1;
    }
    int const again = detail::open_descriptor_("/proc/self/status", flags, 0).value_or(-1);
    if (again >= 0) detail::close_descriptor_(again);
    if (again != number) {
        fixy::report(fixy::Sink::Err, "the door of an open kept a number past its limit open\n");
        return 1;
    }
    fixy::Result<int, Errno> const refused{fixy::err(Errno::eacces)};
    auto const passed = detail::OpenedDescriptorDoor::admit<fs::OpenedType::regular_file>(
        refused, [](fs::OpenedDescriptor<fs::OpenedType::regular_file> opened) noexcept { return opened.number(); });
    if (!refuses_with(passed, Errno::eacces)) {
        fixy::report(fixy::Sink::Err, "the door of an open changed the error of the open\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto opened = fs::open_directory(ctx, sanitized("/tmp"));
    if (opened.is_err()) {
        fixy::report(fixy::Sink::Err, "the open of /tmp failed\n");
        return 1;
    }
    fs::DirectoryHandle const directory = std::move(opened).expect("/tmp opened");
    if (int const rc = kept_copy_refuses(ctx, directory); rc != 0) return rc;
    if (int const rc = move_assignment_refuses(ctx, directory); rc != 0) return rc;
    if (int const rc = box_end_refuses(ctx, directory); rc != 0) return rc;
    if (int const rc = reused_number_refuses(ctx, directory); rc != 0) return rc;
    if (int const rc = borrow_before_move_stays_open(ctx, directory); rc != 0) return rc;
    if (int const rc = number_past_the_slots_refuses(); rc != 0) return rc;
    return 0;
}
