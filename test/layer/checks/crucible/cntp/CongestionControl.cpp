// The compile-time checks of crucible/cntp/CongestionControl.h.

#include <crucible/cntp/CongestionControl.h>

namespace crucible::cntp {

// The descriptor and the generation of its number, and no other byte.
static_assert(sizeof(SocketFd) == sizeof(int) + sizeof(std::uint32_t));
static_assert(std::is_trivially_copy_constructible_v<KernelCcName>);
static_assert(std::is_trivially_destructible_v<KernelCcName>);
static_assert(!std::is_aggregate_v<KernelCcName>,
              "KernelCcName must not be an aggregate: from() is the only path that may set the length");

namespace detail::socket_option_invariants {

// The row that the catalog gives to the network family, stated here as
// a pin.  mint_socket reads the row of ::socket, which the family covers.
static_assert(std::is_same_v<socket_option_row_t, fe::Row<fe::Effect::IO, fe::Effect::Block, fe::Effect::Nondet>>);
static_assert(fe::Subrow<fe::lift_row_t<::fixy::atom::syscall::per<SyscallId::socket>>, socket_option_row_t>);

static_assert(CtxFitsSocketOption<NetworkCtx>);
static_assert(!CtxFitsSocketOption<IoBlockCtx>, "a context without Nondet must not read the live state of a socket.");
static_assert(!CtxFitsSocketOption<IoOnlyCtx>, "a context without Block must not take the socket lock.");
static_assert(!CtxFitsSocketOption<BgOnlyCtx>, "a context without IO must not enter the kernel.");
static_assert(!CtxFitsSocketOption<int>);

}  // namespace detail::socket_option_invariants

}  // namespace crucible::cntp
