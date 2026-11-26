// A body of a thread takes the write half by move.  A borrow of the half on
// the first thread would stay current while the other thread ends the half,
// and the kernel can give its number to a later open.  A half stays on its
// thread (thread_bound{} of foundation/Transfer.h), so the thread door refuses
// the body.  depart() of the half moves it and makes each borrow stale first.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Socket.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>

#include <utility>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using UnixSocket = net::SocketHandle<net::socket_kind::UnixStream>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    return net::with_stream_halves(
               ctx, UnixSocket{},
               [](auto read, auto write) noexcept {
                   auto writer = [moved = std::move(write)]() mutable noexcept {
                       auto write_end = std::move(moved).shut_down_and_close();
                       static_cast<void>(write_end);
                   };
                   auto started = fixy::thread::run_joined(fixy::BgLoadCtx{eff::testing::bg()},
                                                           fixy::thread::ThreadOptions{}, std::move(writer));
                   int const ran = std::move(started).match([](auto) noexcept { return 0; },
                                                            [](auto refusal) noexcept {
                                                                auto [unrun] = std::move(refusal.parts);
                                                                auto here = std::move(unrun);
                                                                ::foundation::linear::call_once(std::move(here));
                                                                return 1;
                                                            });
                   static_cast<void>(ran);
                   return fixy::Record<decltype(read), decltype(write)>{std::move(read), std::move(write)};
               })
        .match([](auto) noexcept { return 0; }, [](auto) noexcept { return 1; });
}
