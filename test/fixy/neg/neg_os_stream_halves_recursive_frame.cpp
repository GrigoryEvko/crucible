// A function template opens a frame, and its body calls the same function
// with the read half of the frame.  The second call opens a frame at the same
// site, and its body gives back the read half of the outer frame in place of
// its own.  The parameters of the second call name the type of the outer half,
// so the second frame is another instantiation of the door, with another
// scope brand.  The gate of the inner frame refuses the body, and no join
// takes a half of another split.

#include <fixy/Core.h>
#include <fixy/os/Socket.h>

#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using UnixSocket = net::SocketHandle<net::socket_kind::UnixStream>;

template <class... Outer>
auto split_again(IoBlockCtx const& ctx, Outer&... outer) noexcept {
    return net::with_stream_halves(ctx, UnixSocket{}, [&ctx, &outer...](auto read, auto write) noexcept {
        if constexpr (sizeof...(Outer) == 0) {
            static_cast<void>(split_again(ctx, read));
            return fixy::Record<decltype(read), decltype(write)>{std::move(read), std::move(write)};
        } else {
            auto read_end = std::move(read).close();
            static_cast<void>(read_end);
            using OuterRead = std::remove_reference_t<decltype(outer...[0])>;
            return fixy::Record<OuterRead, decltype(write)>{std::move(outer...[0]), std::move(write)};
        }
    });
}
}  // namespace

int main() {
    return split_again(IoBlockCtx{eff::testing::test()})
        .match([](auto) noexcept { return 0; }, [](auto) noexcept { return 1; });
}
