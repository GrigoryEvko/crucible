#pragma once

// The socket syscall.  A kind tag names one socket, and the tag is read
// twice: once for the domain, type and protocol that ::socket takes, and
// once for the effect row that the calling context must admit.
//
// mint_socket below makes the call through SocketDoor, the one door to
// ::socket, so a socket handle holds a descriptor that the kernel
// returned to a gated mint.
//
// One kind is declared, because one production site opens a socket.
// A new kind is a tag in fixy::net::socket_kind and a row of socket_table.
// The walk at the foot of this header fails if the tag has no row.

#include <fixy/Qtt.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/os/AtomPack.h>
#include <fixy/os/Fs.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <linux/netlink.h>
#include <sys/socket.h>

#include <cerrno>
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

namespace detail {

// The three arguments of ::socket for one kind.
struct socket_triple final {
    int domain = -1;
    int type = -1;
    int protocol = -1;
};

}  // namespace detail

// The domain, type and protocol of each kind.  A kind reaches ::socket
// only through a row of this table, and fixy/os/AtomPack.h says why a
// table is closed: a class template map takes a specialization for a
// class of the caller, and the gated mint can then open a raw packet
// socket as a known kind.  A kind with no row has no triple, so the mint refuses
// it and ::socket never sees a guessed triple.
inline constexpr ::fixy::atom_pack::tag_row<detail::socket_triple> socket_table[] = {
    {^^socket_kind::NetlinkRoute, {AF_NETLINK, SOCK_RAW, NETLINK_ROUTE}},
};

template <typename Kind>
concept MappedSocketKind = ::fixy::atom_pack::has_row(socket_table, ^^Kind);

// The triple of a kind that has a row.  The lookup is a function and not
// a variable template, because a caller can specialize a variable
// template for one kind and give it a triple of its own.
[[nodiscard]] consteval detail::socket_triple socket_triple_of(std::meta::info kind_tag) noexcept {
    return ::fixy::atom_pack::value_for(socket_table, kind_tag);
}

// The row comes from the syscall atom, so the gate and the catalog in
// fixy/atoms/Syscall.h cannot disagree.  The catalog puts socket in the
// network family, which lifts to IO and Block.
using socket_row_t = eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::socket>>;

template <typename Ctx, typename Kind>
concept CtxFitsSocketMint = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, socket_row_t> && MappedSocketKind<Kind>;

// The Kind parameter precedes Ctx because the caller names it explicitly
// and Ctx deduces from the argument.
//
// Declared here and defined below SocketDoor, because SocketDoor names it
// as its friend, and a friend must already have been declared.
//
// §XXI carve-out: cx=alloc — opening a socket invokes the kernel.
template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketMint<Ctx, Kind>
[[nodiscard]] inline std::expected<Linear<::fixy::fs::OwnedFd>, std::error_code> mint_socket(Ctx const&) noexcept;

// The door to ::socket.  No object of it exists.  Its member is private,
// and its one friend is mint_socket.  OwnedFd befriends this class, so a
// socket handle comes only from mint_socket, after its gate.  The door
// reads the triple of the kind, so no caller gives it a domain, a type or
// a protocol.
class SocketDoor final : ::foundation::NoObject<SocketDoor> {
    template <typename FriendKind, eff::IsExecCtx FriendCtx>
        requires CtxFitsSocketMint<FriendCtx, FriendKind>
    friend auto mint_socket(FriendCtx const&) noexcept -> std::expected<Linear<::fixy::fs::OwnedFd>, std::error_code>;

    // SOCK_CLOEXEC is folded in, because a descriptor that survives execve
    // leaks into every child process.  Returns the errno on failure and no
    // handle.
    template <MappedSocketKind Kind>
    [[nodiscard]] static std::expected<::fixy::fs::OwnedFd, int> open_() noexcept {
        constexpr detail::socket_triple triple = socket_triple_of(^^Kind);
        const int fd = ::socket(
            triple.domain, triple.type | SOCK_CLOEXEC,
            triple
                .protocol);  // SYSCALL-CAP-OK: SocketDoor::open_, sole caller mint_socket ctx-gate (CtxFitsSocketMint)
        if (fd < 0) {
            return std::unexpected{errno};
        }
        return ::fixy::fs::OwnedFd{fd};
    }
};

template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketMint<Ctx, Kind>
[[nodiscard]] inline std::expected<Linear<::fixy::fs::OwnedFd>, std::error_code> mint_socket(Ctx const&) noexcept {
    auto fd = SocketDoor::open_<Kind>();
    if (!fd) {
        return std::unexpected{std::error_code{fd.error(), std::system_category()}};
    }
    return mint_linear<::fixy::fs::OwnedFd>(std::move(*fd));
}

}  // namespace fixy::net

namespace fixy::net::detail::socket_surface_invariants {

// Every tag in fixy::net::socket_kind has a row.  The walk is the check
// that a new tag also has its row.
//
// The walk stays in this header, and the other checks are in its check
// file.  The walk calls the lookup of the table in each translation unit
// that includes this header.  fixy/os/AtomPack.h says why that call must
// come before the code of the includer.
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::net::socket_kind,
                                                        [](std::meta::info kind_tag) consteval {
                                                          return ::fixy::atom_pack::has_row(socket_table, kind_tag);
                                                        }>(),
              "fixy/os/Socket.h: a tag in fixy::net::socket_kind has no row in socket_table.");

}  // namespace fixy::net::detail::socket_surface_invariants
