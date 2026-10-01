// The compile-time checks of fixy/os/Socket.h.

#include <fixy/os/Socket.h>

namespace fixy::net::detail::socket_surface_invariants {

static_assert(socket_triple_of(^^socket_kind::NetlinkRoute).domain == AF_NETLINK);
static_assert(socket_triple_of(^^socket_kind::NetlinkRoute).type == SOCK_RAW);
static_assert(socket_triple_of(^^socket_kind::NetlinkRoute).protocol == NETLINK_ROUTE);

struct NotASocketKind final {};
static_assert(MappedSocketKind<socket_kind::NetlinkRoute>);
static_assert(!MappedSocketKind<NotASocketKind>, "a kind with no row must have no triple.");
static_assert(!MappedSocketKind<void>);
static_assert(!MappedSocketKind<const socket_kind::NetlinkRoute>, "a kind with a cv-qualifier is another type.");

// The row that the catalog gives to socket, stated here as a pin.
static_assert(std::is_same_v<socket_row_t, eff::Row<eff::Effect::IO, eff::Effect::Block>>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

static_assert(CtxFitsSocketMint<IoBlockCtx, socket_kind::NetlinkRoute>);
static_assert(!CtxFitsSocketMint<IoOnlyCtx, socket_kind::NetlinkRoute>,
              "a context without Block must not open a socket: the catalog puts socket in a family that can park.");
static_assert(!CtxFitsSocketMint<IoBlockCtx, NotASocketKind>, "a kind with no triple must be refused.");
static_assert(!std::is_default_constructible_v<SocketDoor> && !std::is_copy_constructible_v<SocketDoor>
                  && !std::is_move_constructible_v<SocketDoor>,
              "No object of the socket door exists.  Its private member is the only call to ::socket.");

}  // namespace fixy::net::detail::socket_surface_invariants
