// The compile-time checks of crucible/cntp/CongestionControl.h.

#include <crucible/cntp/CongestionControl.h>

namespace crucible::cntp {

static_assert(sizeof(SocketFd) == sizeof(int));
static_assert(std::is_trivially_copyable_v<KernelCcName>);
static_assert(!std::is_aggregate_v<KernelCcName>,
              "KernelCcName must not be an aggregate: from() is the only path that may set the length");

namespace detail::socket_option_invariants {

// The row that the catalog gives to the network family, stated here as
// a pin.  mint_socket reads the same row for ::socket.
static_assert(std::is_same_v<socket_option_row_t, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<socket_option_row_t, fe::lift_row_t<::fixy::atom::syscall::per<SyscallId::socket>>>);

static_assert(CtxFitsSocketOption<IoBlockCtx>);
static_assert(!CtxFitsSocketOption<IoOnlyCtx>, "a context without Block must not take the socket lock.");
static_assert(!CtxFitsSocketOption<BgOnlyCtx>, "a context without IO must not enter the kernel.");
static_assert(!CtxFitsSocketOption<int>);

// The key has no public constructor, no copy and no move, so a body that
// takes it is reachable only through a gated form.
static_assert(!std::is_default_constructible_v<SocketOptionKey>);
static_assert(!std::is_copy_constructible_v<SocketOptionKey>);
static_assert(!std::is_move_constructible_v<SocketOptionKey>);
static_assert(!std::is_aggregate_v<SocketOptionKey>);

}  // namespace detail::socket_option_invariants

}  // namespace crucible::cntp
