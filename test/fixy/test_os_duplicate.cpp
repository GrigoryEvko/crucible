// The duplicate of a handle is a second handle of one open file
// (fixy/os/Handle.h).  test_os_socket has the cases of the rights and the
// options of a socket.
//
// The single-thread cases duplicate a file, sealed memory and a directory with
// the rights that move no position, and use the duplicate after the first
// handle closed.  A borrow that is not open gives ebadf.  The rule view-escape
// refuses a read of a borrow after an assignment of its Handle at compile time,
// so the test reads it in an opt-out region of the class PROBE.
//
// In the thread case, one thread holds a duplicate of a socket and reads its
// congestion control, while the other thread closes the first handle and opens
// later sockets, which take its number again and again.  Each handle goes to
// its thread through a transit.  Each read of the duplicate gives the name of
// the first socket, and none gives ebadf.  With a borrow of the first handle in
// place of the duplicate, the first thread could act on a later socket, and the
// borrow stays on its thread, so that form does not compile.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Handle.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/Path.h>
#include <fixy/os/Socket.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/Quarantine.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace perm = foundation::permissions;
namespace src = fixy::tags::source;

namespace {

using foundation::os::Errno;
using fixy::os::RightSet;

// The context of the test, which each check of the Check family takes.
constexpr fixy::TestRunnerCtx kTest{eff::testing::test()};

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;

using TcpSocket = fixy::net::SocketHandle<fixy::net::socket_kind::TcpStream>;
// A duplicate of a stream socket holds no direction, so it holds the options
// right only (the shape of a socket kind of fixy/os/Handle.h).
using TcpOptions = fixy::os::Handle<fixy::os::kind::Socket<fixy::net::socket_kind::TcpStream>, RightSet::ioctl_socket>;
using CongestionName = fixy::FixedText<fixy::net::congestion_name_bytes>;

// The region of the mapping of a file.
struct FileRegion final {
    using permission_row = eff::Row<>;
};

using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;

inline constexpr std::size_t kPageBytes = 4096;

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

// The handle of a mint, or an empty handle when the mint failed.
template <class Handle>
[[nodiscard]] Handle handle_of(fixy::Result<fixy::Linear<Handle>, Errno> minted) noexcept {
    return std::move(minted).match([](fixy::Linear<Handle> made) -> Handle { return std::move(made).consume(); },
                                   [](Errno) -> Handle { return Handle{}; });
}

// The handle of a duplicate, or an empty handle when the mint failed.
template <class Kind, RightSet Rights>
[[nodiscard]] fixy::os::Handle<Kind, Rights>
duplicate_of(fixy::Result<fixy::os::Handle<Kind, Rights>, Errno> minted) noexcept {
    using Handle = fixy::os::Handle<Kind, Rights>;
    return std::move(minted).match([](Handle made) -> Handle { return made; },
                                   [](Errno) -> Handle { return Handle{}; });
}

// The error of a duplicate, or no error.  A handle that the mint gave closes at
// once.
template <class Kind, RightSet Rights>
[[nodiscard]] Errno error_of(fixy::Result<fixy::os::Handle<Kind, Rights>, Errno> minted) noexcept {
    return minted.err().value_or(Errno{});
}

// The name of the congestion control of a socket, or the empty name when the
// read fails.
[[nodiscard]] CongestionName congestion_of(IoBlockCtx const& ctx, TcpOptions const& socket) noexcept {
    return fixy::net::read_tcp_congestion(ctx, socket.ref()).value_or(CongestionName{});
}

// A duplicate with the map right of sealed memory maps its page after the first
// handle closed.
void test_a_duplicate_of_sealed_memory_maps() {
    IoBlockCtx const ctx{eff::testing::test()};
    fs::SealedMemory memory =
        handle_of(fs::mint_sealed_memory(ctx, fs::FileOffset{static_cast<std::int64_t>(kPageBytes)}));
    if (!memory.is_open()) {
        fixy::report(fixy::Sink::Out, "[skipped] the kernel gives no sealed memory\n");
        return;
    }
    auto const mapper = duplicate_of(fixy::os::mint_duplicate<RightSet::map>(ctx, memory.ref()));
    memory = fs::SealedMemory{};
    CRUCIBLE_TEST_ASSERT(kTest, mapper.is_open() && mapper.ref().is_open());
    auto owner = perm::mint_permission_root<FileRegion>();
    {
        auto mapped = fixy::mmap::mint_mmap<ReadOnly, Shared>(ctx, owner, mapper.ref(), kPageBytes);
        CRUCIBLE_TEST_ASSERT(kTest, mapped.is_ok());
        static_cast<void>(std::move(mapped).expect("the map of the duplicate succeeded").consume());
    }
    foundation::linear::drop(std::move(owner));
    fixy::check::pass(kTest, "  test_a_duplicate_of_sealed_memory_maps: PASSED\n");
}

// A duplicate with the lookup right of a directory opens a file beneath it
// after the first handle closed, and a duplicate of a file with the sync right
// syncs it.
void test_a_duplicate_of_a_directory_and_a_file_works() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto opened = fs::open_directory(ctx, sanitized("/proc"));
    CRUCIBLE_TEST_ASSERT(kTest, opened.is_ok());
    fs::DirectoryHandle directory = std::move(opened).expect("/proc opens");
    auto const lookup = duplicate_of(fixy::os::mint_duplicate<RightSet::lookup>(ctx, directory.ref()));
    directory = fs::DirectoryHandle{};
    CRUCIBLE_TEST_ASSERT(kTest, lookup.ref().is_open());
    CRUCIBLE_TEST_ASSERT(kTest, handle_of(fs::mint_file_at<fs::read_only>(ctx, lookup.ref(), "uptime")).is_open());

    auto temporary = fs::open_directory(ctx, sanitized("/tmp"));
    CRUCIBLE_TEST_ASSERT(kTest, temporary.is_ok());
    fs::DirectoryHandle const parent = std::move(temporary).expect("/tmp opens");
    fs::TemporaryFile const file = handle_of(fs::mint_temporary_file_at(ctx, parent.ref()));
    CRUCIBLE_TEST_ASSERT(kTest, file.is_open());
    auto const synced = duplicate_of(fixy::os::mint_duplicate<RightSet::sync>(ctx, file.ref()));
    CRUCIBLE_TEST_ASSERT(kTest, fs::sync<fs::sync_op::Fsync>(ctx, synced.ref()).is_ok());
    fixy::check::pass(kTest, "  test_a_duplicate_of_a_directory_and_a_file_works: PASSED\n");
}

// A borrow of an empty handle and a borrow whose handle closed give ebadf.  A
// later socket takes the number of the closed one, and the closed borrow gives
// no duplicate of it.
void test_a_borrow_that_is_not_open_gives_ebadf() {
    IoBlockCtx const ctx{eff::testing::test()};
    TcpSocket const empty{};
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(fixy::os::mint_duplicate<RightSet::ioctl_socket>(ctx, empty.ref())) == Errno::ebadf);
    TcpSocket closed = handle_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    CRUCIBLE_TEST_ASSERT(kTest, closed.is_open());
    auto const borrow = closed.ref();
    auto const closed_identity = fixy::net::share_socket(borrow).identity();
    closed = TcpSocket{};
    TcpSocket const later = handle_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    CRUCIBLE_TEST_ASSERT(kTest, later.is_open());
    CRUCIBLE_TEST_ASSERT(kTest, fixy::net::share_socket(later.ref()).identity() == closed_identity);
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a borrow from before the end of the descriptor reads its stale generation")
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(fixy::os::mint_duplicate<RightSet::ioctl_socket>(ctx, borrow)) == Errno::ebadf);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    fixy::check::pass(kTest, "  test_a_borrow_that_is_not_open_gives_ebadf: PASSED\n");
}

// The turns of each thread of the thread case.
inline constexpr std::uint32_t kTurns = 2000;

// The duplicate of a second thread names the first socket through each close
// and each later open of the first thread.
void test_a_duplicate_of_a_second_thread_keeps_its_socket() {
    IoBlockCtx const ctx{eff::testing::test()};
    TcpSocket first = handle_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    CRUCIBLE_TEST_ASSERT(kTest, first.is_open());
    CRUCIBLE_TEST_ASSERT(kTest, fixy::net::set_tcp_congestion(ctx, first.ref(), "reno").is_ok());
    TcpOptions second = duplicate_of(fixy::os::mint_duplicate<RightSet::ioctl_socket>(ctx, first.ref()));
    CRUCIBLE_TEST_ASSERT(kTest, second.is_open());
    foundation::core::Atomic<std::uint32_t> wrong{0};
    // Each socket goes to its thread through a transit (fixy/os/Handle.h).
    auto reader = [moving = std::move(second).depart(), &wrong]() mutable noexcept {
        TcpOptions const socket = std::move(moving).arrive();
        IoBlockCtx const reader_ctx{eff::testing::test()};
        for (std::uint32_t turn = 0; turn < kTurns; ++turn) {
            auto const name = congestion_of(reader_ctx, socket);
            if (!(name.view() == fixy::TextView{"reno"})) static_cast<void>(wrong.fetch_add_acq_rel(1));
        }
    };
    auto closer = [moving = std::move(first).depart()]() mutable noexcept {
        TcpSocket socket = std::move(moving).arrive();
        IoBlockCtx const closer_ctx{eff::testing::test()};
        for (std::uint32_t turn = 0; turn < kTurns; ++turn) {
            socket = TcpSocket{};
            socket = handle_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(closer_ctx));
        }
    };
    // A refused start gives the two bodies back with their transits, and the
    // test runs them on this thread, so each transit arrives.  The once door
    // takes a local that the frame gives up.
    using Refused = fixy::thread::RefusedStart<decltype(reader), decltype(closer)>;
    bool const is_started = fixy::thread::run_joined(fixy::BgLoadCtx{eff::testing::bg()}, fixy::thread::ThreadOptions{},
                                                     std::move(reader), std::move(closer))
                                .match([](foundation::core::Unit) noexcept { return true; },
                                       [](Refused refusal) noexcept {
                                           auto [unrun_reader, unrun_closer] = std::move(refusal.parts);
                                           auto reader_here = std::move(unrun_reader);
                                           auto closer_here = std::move(unrun_closer);
                                           foundation::linear::call_once(std::move(reader_here));
                                           foundation::linear::call_once(std::move(closer_here));
                                           return false;
                                       });
    CRUCIBLE_TEST_ASSERT(kTest, is_started);
    CRUCIBLE_TEST_ASSERT(kTest, wrong.load_acquire() == 0);
    fixy::check::pass(kTest, "  test_a_duplicate_of_a_second_thread_keeps_its_socket: PASSED\n");
}

}  // namespace

int main() {
    fixy::report(fixy::Sink::Out, "test_os_duplicate:\n");
    test_a_duplicate_of_sealed_memory_maps();
    test_a_duplicate_of_a_directory_and_a_file_works();
    test_a_borrow_that_is_not_open_gives_ebadf();
    test_a_duplicate_of_a_second_thread_keeps_its_socket();
    fixy::check::pass(kTest, "test_os_duplicate: all PASSED\n");
    return 0;
}
