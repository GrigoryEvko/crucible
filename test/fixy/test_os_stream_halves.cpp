// The frame of the two halves of a stream socket (with_stream_halves of
// fixy/os/Socket.h), on the ends of a socket pair of the local domain.
//
// The split gives a read half and a write half of one socket.  The bytes that
// the peer writes, the read half reads, and the bytes that the write half
// writes, the peer reads.  A borrow and a foreign ref from before the split
// give ebadf.  The join gives the Handle with its two directions back, and a
// borrow of a half is stale after it.  The end of the write half ends the
// stream for the peer while the read half lives, and the frame then gives the
// Handle of the read half.  Two ends give Unit and the value of the body.  A
// half that departs makes its borrow stale.  The halves go to two threads, and
// a third thread echoes each frame at the peer.  Each frame comes back whole
// and in order, and the end of the write half on its thread ends the stream
// for the echo.  The split of an empty Handle gives the Handle back with ebadf.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Handle.h>
#include <fixy/os/Socket.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {

using foundation::os::Errno;

// The context of the test, which each check of the Check family takes.
constexpr fixy::TestRunnerCtx kTest{eff::testing::test()};

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using StartCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

using UnixSocket = net::SocketHandle<net::socket_kind::UnixStream>;
using UnixPair = net::SocketPair<net::socket_kind::UnixStream>;
using UnixKind = fixy::os::kind::Socket<net::socket_kind::UnixStream>;
using Ends = fixy::Record<UnixSocket, UnixSocket>;
using ReadSocket = fixy::os::Handle<UnixKind, fixy::os::RightSet::read | fixy::os::RightSet::ioctl_socket>;
using WriteSocket = fixy::os::Handle<UnixKind, fixy::os::RightSet::write | fixy::os::RightSet::ioctl_socket>;
using Refused = net::StreamHalvesRefused<UnixKind, net::socket_rights>;
using Four = fixy::FixedArray<std::byte, 4>;

// The bytes of a frame of the thread case, and the count of its frames.
inline constexpr std::size_t kFrameBytes = 64;
inline constexpr std::uint32_t kFrames = 200;
using Frame = fixy::FixedArray<std::byte, kFrameBytes>;
// The count of the frames that the echo gave back, and of the frames that the
// reader found wrong.
using Counts = fixy::Record<std::uint32_t, std::uint32_t>;

// The two ends of a new pair, or two empty handles when the open failed.
[[nodiscard]] Ends open_pair(IoBlockCtx const& ctx) noexcept {
    return net::mint_socket_pair<net::socket_kind::UnixStream>(ctx).match(
        [](UnixPair pair) -> Ends {
            auto [first, second] = std::move(pair);
            return Ends{std::move(first).consume(), std::move(second).consume()};
        },
        [](Errno) -> Ends { return Ends{UnixSocket{}, UnixSocket{}}; });
}

// True when a full read of four bytes through the borrow gives expected.
template <class Kind, fixy::os::RightSet Rights>
[[nodiscard]] bool reads_four(IoBlockCtx const& ctx, fixy::os::HandleRef<Kind, Rights> borrow,
                              Four const& expected) noexcept {
    auto const [count, read] = fixy::build_fixed_array<std::byte, 4>(
        [&ctx, borrow](auto run) noexcept { return fixy::fs::read_full(ctx, borrow, std::move(run)).value_or(0); });
    return count == 4 && read == expected;
}

// True when a full read through the borrow finds the end of the stream: it
// reads no byte.
template <class Kind, fixy::os::RightSet Rights>
[[nodiscard]] bool reads_the_end(IoBlockCtx const& ctx, fixy::os::HandleRef<Kind, Rights> borrow) noexcept {
    auto const [count, unused] = fixy::build_fixed_array<std::byte, 4>(
        [&ctx, borrow](auto run) noexcept { return fixy::fs::read_full(ctx, borrow, std::move(run)).value_or(9); });
    static_cast<void>(unused);
    return count == 0;
}

// True when a receive that does not wait finds the end of the stream through
// the borrow: it fills no byte and gives no error.  A socket whose peer did
// not end the stream gives eagain, so the check does not wait for an end
// that does not come.
template <fixy::os::RightSet Rights>
[[nodiscard]] bool finds_the_end_now(IoBlockCtx const& ctx,
                                     net::SocketRef<net::socket_kind::UnixStream, Rights> borrow) noexcept {
    auto const [is_end, unused] = fixy::build_fixed_array<std::byte, 4>([&ctx, borrow](auto run) noexcept {
        auto const received = net::receive_bytes(ctx, borrow, std::move(run));
        return received.match([](auto const& part) noexcept { return part.size() == 0; },
                              [](Errno) noexcept { return false; });
    });
    static_cast<void>(unused);
    return is_end;
}

// The error of a full read through the borrow, or no error.
template <class Kind, fixy::os::RightSet Rights>
[[nodiscard]] Errno read_error_of(IoBlockCtx const& ctx, fixy::os::HandleRef<Kind, Rights> borrow) noexcept {
    auto const [error, unused] = fixy::build_fixed_array<std::byte, 4>([&ctx, borrow](auto run) noexcept {
        return fixy::fs::read_full(ctx, borrow, std::move(run)).err().value_or(Errno{});
    });
    static_cast<void>(unused);
    return error;
}

// The frame of the index: each byte holds the index.
[[nodiscard]] Frame frame_of(std::uint32_t index) noexcept {
    return fixy::build_fixed_array<std::byte, kFrameBytes>(
        [index](auto run) noexcept { fixy::fill(std::move(run), std::byte{static_cast<std::uint8_t>(index)}); });
}

// The split divides the directions, the old borrows are stale, and the join
// gives the Handle back.  The foreign ref of the read half that the body gives
// back is stale after the join.
void test_a_split_divides_the_directions_and_joins() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto [mine, peer] = open_pair(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, mine.is_open() && peer.is_open());
    Four const ping{std::byte{'p'}, std::byte{'i'}, std::byte{'n'}, std::byte{'g'}};
    Four const pong{std::byte{'p'}, std::byte{'o'}, std::byte{'n'}, std::byte{'g'}};
    Four const full{std::byte{'f'}, std::byte{'u'}, std::byte{'l'}, std::byte{'l'}};
    Four const back{std::byte{'b'}, std::byte{'a'}, std::byte{'c'}, std::byte{'k'}};
    auto const before = mine.ref();
    auto const shared_before = net::share_socket(before);
    bool is_old_borrow_stale = false;
    bool is_each_direction_whole = false;
    bool is_joined_whole = false;
    bool is_half_borrow_stale = false;
    bool const is_joined =
        net::with_stream_halves(ctx, std::move(mine),
                                [&ctx, &peer, &ping, &pong, before, &is_old_borrow_stale,
                                 &is_each_direction_whole](auto read, auto write) noexcept {
                                    is_old_borrow_stale = read_error_of(ctx, before) == Errno::ebadf;
                                    bool const is_out =
                                        fixy::fs::write_full(ctx, write.ref(), fixy::TextView{"ping"}).is_ok()
                                        && reads_four(ctx, peer.ref(), ping);
                                    bool const is_in =
                                        fixy::fs::write_full(ctx, peer.ref(), fixy::TextView{"pong"}).is_ok()
                                        && reads_four(ctx, read.ref(), pong);
                                    is_each_direction_whole = is_out && is_in;
                                    auto read_shared = net::share_socket(read.ref());
                                    return fixy::Record<decltype(read), decltype(write), decltype(read_shared)>{
                                        std::move(read), std::move(write), read_shared};
                                })
            .match(
                [&ctx, &peer, &full, &back, &is_joined_whole, &is_half_borrow_stale](auto joined) noexcept {
                    auto [socket, read_shared] = std::move(joined);
                    is_half_borrow_stale = !read_shared.is_current();
                    is_joined_whole = fixy::fs::write_full(ctx, socket.ref(), fixy::TextView{"full"}).is_ok()
                                   && reads_four(ctx, peer.ref(), full)
                                   && fixy::fs::write_full(ctx, peer.ref(), fixy::TextView{"back"}).is_ok()
                                   && reads_four(ctx, socket.ref(), back);
                    return socket.is_open();
                },
                [](Refused) noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_joined);
    CRUCIBLE_TEST_ASSERT(kTest, is_old_borrow_stale && !shared_before.is_current());
    CRUCIBLE_TEST_ASSERT(kTest, is_each_direction_whole);
    CRUCIBLE_TEST_ASSERT(kTest, is_joined_whole);
    CRUCIBLE_TEST_ASSERT(kTest, is_half_borrow_stale);
    fixy::check::pass(kTest, "  test_a_split_divides_the_directions_and_joins: PASSED\n");
}

// The end of the write half ends the stream for the peer while the read half
// lives, and the frame gives the Handle of the read half.
void test_the_end_of_the_write_half_ends_the_stream() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto [mine, peer] = open_pair(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, mine.is_open() && peer.is_open());
    Four const last{std::byte{'l'}, std::byte{'a'}, std::byte{'s'}, std::byte{'t'}};
    Four const tail{std::byte{'t'}, std::byte{'a'}, std::byte{'i'}, std::byte{'l'}};
    bool is_end_seen = false;
    ReadSocket const read_only =
        net::with_stream_halves(
            ctx, std::move(mine),
            [&ctx, &peer, &last, &is_end_seen](auto read, auto write) noexcept {
                bool const is_written = fixy::fs::write_full(ctx, write.ref(), fixy::TextView{"last"}).is_ok();
                auto ended = std::move(write).shut_down_and_close();
                is_end_seen = is_written && reads_four(ctx, peer.ref(), last) && finds_the_end_now(ctx, peer.ref());
                return fixy::Record<decltype(read), decltype(ended)>{std::move(read), std::move(ended)};
            })
            .match([](ReadSocket kept) noexcept { return kept; }, [](Refused) noexcept { return ReadSocket{}; });
    bool const is_read_kept =
        fixy::fs::write_full(ctx, peer.ref(), fixy::TextView{"tail"}).is_ok() && reads_four(ctx, read_only.ref(), tail);
    CRUCIBLE_TEST_ASSERT(kTest, is_end_seen);
    CRUCIBLE_TEST_ASSERT(kTest, is_read_kept);
    fixy::check::pass(kTest, "  test_the_end_of_the_write_half_ends_the_stream: PASSED\n");
}

// The end of the read half keeps the write half, and the frame gives the
// Handle of the write half.  The peer reads its bytes.
void test_the_end_of_the_read_half_keeps_the_write_half() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto [mine, peer] = open_pair(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, mine.is_open() && peer.is_open());
    Four const kept{std::byte{'k'}, std::byte{'e'}, std::byte{'p'}, std::byte{'t'}};
    WriteSocket const write_only =
        net::with_stream_halves(ctx, std::move(mine), [](auto read, auto write) noexcept {
            auto ended = std::move(read).close();
            return fixy::Record<decltype(ended), decltype(write)>{ended, std::move(write)};
        }).match([](WriteSocket held) noexcept { return held; }, [](Refused) noexcept { return WriteSocket{}; });
    bool const is_write_kept = fixy::fs::write_full(ctx, write_only.ref(), fixy::TextView{"kept"}).is_ok()
                            && reads_four(ctx, peer.ref(), kept);
    CRUCIBLE_TEST_ASSERT(kTest, is_write_kept);
    fixy::check::pass(kTest, "  test_the_end_of_the_read_half_keeps_the_write_half: PASSED\n");
}

// Two ends give Unit and the value of the body, and the peer reads the end of
// the stream.
void test_two_ends_give_a_value() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto [mine, peer] = open_pair(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, mine.is_open() && peer.is_open());
    std::uint32_t const value =
        net::with_stream_halves(ctx, std::move(mine),
                                [](auto read, auto write) noexcept {
                                    auto read_end = std::move(read).close();
                                    auto write_end = std::move(write).shut_down_and_close();
                                    return fixy::Record<decltype(read_end), decltype(write_end), std::uint32_t>{
                                        read_end, write_end, 7};
                                })
            .match([](fixy::Record<fixy::Unit, std::uint32_t> ended) noexcept { return ended.at(fixy::index<1>()); },
                   [](Refused) noexcept { return std::uint32_t{0}; });
    CRUCIBLE_TEST_ASSERT(kTest, value == 7);
    CRUCIBLE_TEST_ASSERT(kTest, finds_the_end_now(ctx, peer.ref()));
    fixy::check::pass(kTest, "  test_two_ends_give_a_value: PASSED\n");
}

// A half that departs makes its borrow on this thread stale, and the half of
// the arrive reads the socket.
void test_a_half_that_departs_makes_its_borrow_stale() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto [mine, peer] = open_pair(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, mine.is_open() && peer.is_open());
    Four const next{std::byte{'n'}, std::byte{'e'}, std::byte{'x'}, std::byte{'t'}};
    bool is_stale = false;
    bool is_read_after_arrive = false;
    bool const is_joined =
        net::with_stream_halves(
            ctx, std::move(mine),
            [&ctx, &peer, &next, &is_stale, &is_read_after_arrive](auto read, auto write) noexcept {
                auto const borrow = read.ref();
                auto transit = std::move(read).depart();
                is_stale = !borrow.is_open() && read_error_of(ctx, borrow) == Errno::ebadf;
                auto arrived = std::move(transit).arrive();
                is_read_after_arrive = fixy::fs::write_full(ctx, peer.ref(), fixy::TextView{"next"}).is_ok()
                                    && reads_four(ctx, arrived.ref(), next);
                return fixy::Record<decltype(arrived), decltype(write)>{std::move(arrived), std::move(write)};
            })
            .match([](UnixSocket joined) noexcept { return joined.is_open(); }, [](Refused) noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_joined);
    CRUCIBLE_TEST_ASSERT(kTest, is_stale && is_read_after_arrive);
    fixy::check::pass(kTest, "  test_a_half_that_departs_makes_its_borrow_stale: PASSED\n");
}

// The halves go to two threads inside the frame: one writes the frames and
// then ends its half, and one reads them back after a third thread echoed
// each one at the peer.  The end of the write half ends the stream for the
// echo, and the close of the peer ends it for the reader.  Each thread ends
// its half and gives the token back.
void test_the_halves_go_to_two_threads() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto [mine, peer] = open_pair(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, mine.is_open() && peer.is_open());
    bool is_started = false;
    Counts const counts =
        net::with_stream_halves(
            ctx, std::move(mine),
            [&peer, &is_started](auto read, auto write) noexcept {
                auto writer = [moving = std::move(write).depart()]() mutable noexcept {
                    IoBlockCtx const writer_ctx{eff::testing::test()};
                    auto half = std::move(moving).arrive();
                    for (std::uint32_t index = 0; index < kFrames; ++index) {
                        Frame const frame = frame_of(index);
                        static_cast<void>(fixy::fs::write_full(writer_ctx, half.ref(), frame.view()));
                    }
                    return std::move(half).shut_down_and_close();
                };
                auto echo = [moving = std::move(peer).depart()]() mutable noexcept {
                    IoBlockCtx const echo_ctx{eff::testing::test()};
                    UnixSocket const socket = std::move(moving).arrive();
                    std::uint32_t echoed = 0;
                    for (std::uint32_t index = 0; index <= kFrames; ++index) {
                        auto const [count, frame] =
                            fixy::build_fixed_array<std::byte, kFrameBytes>([&echo_ctx, &socket](auto run) noexcept {
                                return fixy::fs::read_full(echo_ctx, socket.ref(), std::move(run)).value_or(0);
                            });
                        if (count != kFrameBytes) break;
                        if (fixy::fs::write_full(echo_ctx, socket.ref(), frame.view()).is_ok()) ++echoed;
                    }
                    return echoed;
                };
                auto reader = [moving = std::move(read).depart()]() mutable noexcept {
                    IoBlockCtx const reader_ctx{eff::testing::test()};
                    auto half = std::move(moving).arrive();
                    auto const borrow = half.ref();
                    std::uint32_t wrong = 0;
                    for (std::uint32_t index = 0; index < kFrames; ++index) {
                        auto const [count, frame] =
                            fixy::build_fixed_array<std::byte, kFrameBytes>([&reader_ctx, borrow](auto run) noexcept {
                                return fixy::fs::read_full(reader_ctx, borrow, std::move(run)).value_or(0);
                            });
                        if (count != kFrameBytes || !(frame == frame_of(index))) ++wrong;
                    }
                    if (!reads_the_end(reader_ctx, borrow)) ++wrong;
                    auto ended = std::move(half).close();
                    return fixy::Record<decltype(ended), std::uint32_t>{ended, wrong};
                };
                using Values = fixy::Record<decltype(writer()), decltype(echo()), decltype(reader())>;
                using RefusedStart = fixy::thread::RefusedStart<decltype(writer), decltype(echo), decltype(reader)>;
                // A refused start gives the three bodies back, and they run on
                // this thread in the order of the stream, which needs no second
                // thread: the frames fit in the buffers of the socket.
                Values values = fixy::thread::run_joined(StartCtx{eff::testing::bg()}, fixy::thread::ThreadOptions{},
                                                         std::move(writer), std::move(echo), std::move(reader))
                                    .match(
                                        [&is_started](Values joined) noexcept {
                                            is_started = true;
                                            return joined;
                                        },
                                        [](RefusedStart refusal) noexcept {
                                            auto [unrun_writer, unrun_echo, unrun_reader] = std::move(refusal.parts);
                                            auto writer_here = std::move(unrun_writer);
                                            auto echo_here = std::move(unrun_echo);
                                            auto reader_here = std::move(unrun_reader);
                                            auto written = foundation::linear::call_once(std::move(writer_here));
                                            auto echoed = foundation::linear::call_once(std::move(echo_here));
                                            auto read_back = foundation::linear::call_once(std::move(reader_here));
                                            return Values{written, echoed, read_back};
                                        });
                auto [write_end, echoed, read_back] = values;
                auto [read_end, wrong] = read_back;
                return fixy::Record<decltype(read_end), decltype(write_end), Counts>{read_end, write_end,
                                                                                     Counts{echoed, wrong}};
            })
            .match([](fixy::Record<fixy::Unit, Counts> ended) noexcept { return ended.at(fixy::index<1>()); },
                   [](Refused) noexcept { return Counts{0, kFrames}; });
    CRUCIBLE_TEST_ASSERT(kTest, is_started);
    CRUCIBLE_TEST_ASSERT(kTest, counts.at(fixy::index<0>()) == kFrames);
    CRUCIBLE_TEST_ASSERT(kTest, counts.at(fixy::index<1>()) == 0);
    fixy::check::pass(kTest, "  test_the_halves_go_to_two_threads: PASSED\n");
}

// The split of an empty Handle gives the Handle back with ebadf, and the body
// does not run.
void test_a_refused_split_gives_the_handle_back() {
    IoBlockCtx const ctx{eff::testing::test()};
    bool is_body_run = false;
    Errno const error =
        net::with_stream_halves(ctx, UnixSocket{},
                                [&is_body_run](auto read, auto write) noexcept {
                                    is_body_run = true;
                                    return fixy::Record<decltype(read), decltype(write)>{std::move(read),
                                                                                         std::move(write)};
                                })
            .match([](UnixSocket) noexcept { return Errno{}; },
                   [](Refused refusal) noexcept { return refusal.socket.is_open() ? Errno{} : refusal.error; });
    CRUCIBLE_TEST_ASSERT(kTest, error == Errno::ebadf);
    CRUCIBLE_TEST_ASSERT(kTest, !is_body_run);
    fixy::check::pass(kTest, "  test_a_refused_split_gives_the_handle_back: PASSED\n");
}

}  // namespace

int main() {
    fixy::report(fixy::Sink::Out, "test_os_stream_halves:\n");
    test_a_split_divides_the_directions_and_joins();
    test_the_end_of_the_write_half_ends_the_stream();
    test_the_end_of_the_read_half_keeps_the_write_half();
    test_two_ends_give_a_value();
    test_a_half_that_departs_makes_its_borrow_stale();
    test_the_halves_go_to_two_threads();
    test_a_refused_split_gives_the_handle_back();
    fixy::check::pass(kTest, "test_os_stream_halves: all PASSED\n");
    return 0;
}
