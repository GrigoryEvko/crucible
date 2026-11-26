// A Handle goes to another thread only through a transit (fixy/os/Handle.h).
//
// depart() on the thread of the Handle advances the generation of its number,
// so each borrow and each foreign ref of that thread is stale, and a door
// through one gives ebadf and makes no call.  arrive() gives the Handle on the
// other thread, and its doors act on the same socket.  In the thread case, the
// thread that took the Handle closes it and opens later sockets, which take its
// number again and again, while the first thread reads through the borrow that
// it kept.  Each read gives ebadf, and none reads a later socket.  A transit of
// an empty Handle gives an empty Handle.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Handle.h>
#include <fixy/os/Socket.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>

#include <cstdint>
#include <utility>

namespace eff = foundation::effects;

namespace {

using foundation::os::Errno;

// The context of the test, which each check of the Check family takes.
constexpr fixy::TestRunnerCtx kTest{eff::testing::test()};

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using StartCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

using TcpSocket = fixy::net::SocketHandle<fixy::net::socket_kind::TcpStream>;
using TcpRef = fixy::net::SocketRef<fixy::net::socket_kind::TcpStream, fixy::net::socket_rights>;
using CongestionName = fixy::FixedText<fixy::net::congestion_name_bytes>;

// The turns of the thread case.
inline constexpr std::uint32_t kTurns = 2000;

// The handle of a mint, or an empty handle when the mint failed.
[[nodiscard]] TcpSocket open_socket(IoBlockCtx const& ctx) noexcept {
    return fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx).match(
        [](fixy::Linear<TcpSocket> made) -> TcpSocket { return std::move(made).consume(); },
        [](Errno) -> TcpSocket { return TcpSocket{}; });
}

// A socket whose congestion control is reno, or an empty handle when a call
// failed.
[[nodiscard]] TcpSocket reno_socket(IoBlockCtx const& ctx) noexcept {
    TcpSocket socket = open_socket(ctx);
    if (!socket.is_open() || fixy::net::set_tcp_congestion(ctx, socket.ref(), "reno").is_err()) return TcpSocket{};
    return socket;
}

// True when the door reads the name reno through the borrow.
[[nodiscard]] bool reads_reno(IoBlockCtx const& ctx, TcpRef borrow) noexcept {
    return fixy::net::read_tcp_congestion(ctx, borrow)
        .match([](CongestionName name) noexcept { return name.view() == fixy::TextView{"reno"}; },
               [](Errno) noexcept { return false; });
}

// The error of the door through the borrow, or no error.
[[nodiscard]] Errno error_of(IoBlockCtx const& ctx, TcpRef borrow) noexcept {
    return fixy::net::read_tcp_congestion(ctx, borrow).err().value_or(Errno{});
}

// The departure makes the borrow and the foreign ref of the thread stale, and
// the Handle of the arrive reads the same socket.
void test_a_departure_makes_each_borrow_stale() {
    IoBlockCtx const ctx{eff::testing::test()};
    TcpSocket socket = reno_socket(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, socket.is_open());
    TcpRef const borrow = socket.ref();
    auto const shared = fixy::net::share_socket(borrow);
    CRUCIBLE_TEST_ASSERT(kTest, reads_reno(ctx, borrow) && shared.is_current());
    auto transit = std::move(socket).depart();
    CRUCIBLE_TEST_ASSERT(kTest, !borrow.is_open() && !shared.is_current());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(ctx, borrow) == Errno::ebadf);
    TcpSocket const arrived = std::move(transit).arrive();
    CRUCIBLE_TEST_ASSERT(kTest, arrived.is_open() && reads_reno(ctx, arrived.ref()));
    // The arrive makes no borrow of the sender current again.
    CRUCIBLE_TEST_ASSERT(kTest, error_of(ctx, borrow) == Errno::ebadf);
    fixy::check::pass(kTest, "  test_a_departure_makes_each_borrow_stale: PASSED\n");
}

// The transit of an empty Handle gives an empty Handle.
void test_an_empty_handle_arrives_empty() {
    TcpSocket empty{};
    auto transit = std::move(empty).depart();
    TcpSocket const arrived = std::move(transit).arrive();
    CRUCIBLE_TEST_ASSERT(kTest, !arrived.is_open() && !arrived.ref().is_open());
    fixy::check::pass(kTest, "  test_an_empty_handle_arrives_empty: PASSED\n");
}

// The Handle arrives on a second thread, which reads the socket, then closes
// it and opens later sockets on its number.  The first thread reads through
// the borrow that it kept, beside the second thread.  Each of its reads gives
// ebadf, and none reads a later socket.
void test_a_kept_borrow_fails_closed_beside_the_other_thread() {
    IoBlockCtx const ctx{eff::testing::test()};
    TcpSocket socket = reno_socket(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, socket.is_open());
    TcpRef const borrow = socket.ref();
    foundation::core::Atomic<std::uint32_t> arrived_reno{0};
    auto closer = [moving = std::move(socket).depart(), &arrived_reno]() mutable noexcept {
        IoBlockCtx const closer_ctx{eff::testing::test()};
        TcpSocket owned = std::move(moving).arrive();
        if (reads_reno(closer_ctx, owned.ref())) arrived_reno.store_release(1);
        for (std::uint32_t turn = 0; turn < kTurns; ++turn) {
            owned = TcpSocket{};
            owned = open_socket(closer_ctx);
        }
    };
    auto reader = [&borrow, &ctx]() noexcept -> std::uint32_t {
        std::uint32_t not_stale = 0;
        for (std::uint32_t turn = 0; turn < kTurns; ++turn) {
            if (error_of(ctx, borrow) != Errno::ebadf) ++not_stale;
        }
        return not_stale;
    };
    // A refused start gives the two bodies back, and the test runs them on this
    // thread, so the transit arrives.  The once door takes a local that the
    // frame gives up.
    using Refused = fixy::thread::RefusedStart<decltype(closer), decltype(reader)>;
    std::uint32_t not_stale = 0;
    bool const is_started = fixy::thread::run_beside(StartCtx{eff::testing::bg()}, fixy::thread::ThreadOptions{},
                                                     std::move(closer), std::move(reader))
                                .match(
                                    [&not_stale](auto values) noexcept {
                                        not_stale = values.at(fixy::index<1>());
                                        return true;
                                    },
                                    [&not_stale](Refused refusal) noexcept {
                                        auto [unrun_closer, unrun_reader] = std::move(refusal.parts);
                                        auto closer_here = std::move(unrun_closer);
                                        auto reader_here = std::move(unrun_reader);
                                        foundation::linear::call_once(std::move(closer_here));
                                        not_stale = foundation::linear::call_once(std::move(reader_here));
                                        return false;
                                    });
    CRUCIBLE_TEST_ASSERT(kTest, is_started);
    CRUCIBLE_TEST_ASSERT(kTest, not_stale == 0);
    CRUCIBLE_TEST_ASSERT(kTest, arrived_reno.load_acquire() == 1);
    fixy::check::pass(kTest, "  test_a_kept_borrow_fails_closed_beside_the_other_thread: PASSED\n");
}

}  // namespace

int main() {
    fixy::report(fixy::Sink::Out, "test_os_handle_transit:\n");
    test_a_departure_makes_each_borrow_stale();
    test_an_empty_handle_arrives_empty();
    test_a_kept_borrow_fails_closed_beside_the_other_thread();
    fixy::check::pass(kTest, "test_os_handle_transit: all PASSED\n");
    return 0;
}
