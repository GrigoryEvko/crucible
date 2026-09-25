#pragma once

// The socket syscall.  A kind tag names one socket, and the tag is read
// twice: once for the domain, type and protocol that ::socket takes, and
// once for the effect row that the calling context must admit.
//
// The old tree had no socket factory.  src/cntp/Pacing.cpp wraps the
// return value of ::socket in the old FileHandle, whose public
// constructor adopts any integer.  mint_socket below makes the call
// itself, through OwnedFd::open_socket, so a socket handle holds a
// descriptor that the kernel returned.
//
// One kind is declared, because one production site opens a socket.
// A new kind is a tag in fixy::net::socket_kind and a specialization of
// socket_triple.  The walk at the foot of this header fails if the tag
// has no specialization.

#include <fixy/Qtt.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/os/Fs.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <linux/netlink.h>
#include <sys/socket.h>

#include <concepts>
#include <expected>
#include <meta>
#include <system_error>
#include <type_traits>
#include <utility>

namespace fixy::net {

namespace eff = ::foundation::effects;

namespace socket_kind {

// A netlink socket to the routing subsystem of the kernel.  It sends a
// dump request and reads the reply, for example the queueing discipline
// of an interface.
struct NetlinkRoute final {};

}  // namespace socket_kind

// The primary has no members.  A kind with no specialization has no
// domain, so the mint refuses it and ::socket never sees a guessed triple.
template <typename Kind>
struct socket_triple {};

template <>
struct socket_triple<socket_kind::NetlinkRoute> {
    static constexpr int domain = AF_NETLINK;
    static constexpr int type = SOCK_RAW;
    static constexpr int protocol = NETLINK_ROUTE;
};

template <typename Kind>
concept MappedSocketKind = requires {
    { socket_triple<Kind>::domain } -> std::convertible_to<int>;
    { socket_triple<Kind>::type } -> std::convertible_to<int>;
    { socket_triple<Kind>::protocol } -> std::convertible_to<int>;
};

// The row comes from the syscall atom, so the gate and the catalog in
// fixy/atoms/Syscall.h cannot disagree.  The catalog puts socket in the
// network family, which lifts to IO and Block.
using socket_row_t = eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::socket>>;

template <typename Ctx, typename Kind>
concept CtxFitsSocketMint = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, socket_row_t> && MappedSocketKind<Kind>;

// The Kind parameter precedes Ctx because the caller names it explicitly
// and Ctx deduces from the argument.
//
// §XXI carve-out: cx=alloc — opening a socket invokes the kernel.
template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketMint<Ctx, Kind>
[[nodiscard]] inline std::expected<Linear<::fixy::fs::OwnedFd>, std::error_code> mint_socket(Ctx const&) noexcept {
    using triple = socket_triple<Kind>;
    auto fd = ::fixy::fs::OwnedFd::open_socket(triple::domain, triple::type, triple::protocol);
    if (!fd) {
        return std::unexpected{std::error_code{fd.error(), std::system_category()}};
    }
    return mint_linear<::fixy::fs::OwnedFd>(std::move(*fd));
}

}  // namespace fixy::net

namespace fixy::net::detail::socket_surface_invariants {

static_assert(socket_triple<socket_kind::NetlinkRoute>::domain == AF_NETLINK);
static_assert(socket_triple<socket_kind::NetlinkRoute>::type == SOCK_RAW);
static_assert(socket_triple<socket_kind::NetlinkRoute>::protocol == NETLINK_ROUTE);

struct NotASocketKind final {};
static_assert(MappedSocketKind<socket_kind::NetlinkRoute>);
static_assert(!MappedSocketKind<NotASocketKind>, "a kind with no specialization must have no triple.");
static_assert(!MappedSocketKind<void>);

// The row that the catalog gives to socket, stated here as a pin.
static_assert(std::is_same_v<socket_row_t, eff::Row<eff::Effect::IO, eff::Effect::Block>>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

static_assert(CtxFitsSocketMint<IoBlockCtx, socket_kind::NetlinkRoute>);
static_assert(!CtxFitsSocketMint<IoOnlyCtx, socket_kind::NetlinkRoute>,
              "a context without Block must not open a socket: the catalog puts socket in a family that can park.");
static_assert(!CtxFitsSocketMint<IoBlockCtx, NotASocketKind>, "a kind with no triple must be refused.");

// Every tag in fixy::net::socket_kind has a triple.  The walk is the
// check that a new tag also has its specialization.
[[nodiscard]] consteval bool every_socket_kind_is_mapped_() noexcept {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^::fixy::net::socket_kind, std::meta::access_context::current()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            using T = [:member:];
            if (!MappedSocketKind<T>) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_socket_kind_is_mapped_(),
              "fixy/os/Socket.h: a tag in fixy::net::socket_kind has no specialization of socket_triple.");

}  // namespace fixy::net::detail::socket_surface_invariants
