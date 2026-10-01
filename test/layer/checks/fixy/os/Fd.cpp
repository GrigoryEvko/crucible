// The compile-time checks of fixy/os/Fd.h.

#include <fixy/os/Fd.h>

#include <type_traits>

namespace fixy::fs::detail::fd_surface_invariants {

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

static_assert(CtxAdmitsFs<IoBlockCtx>);
static_assert(!CtxAdmitsFs<IoOnlyCtx>);

// The descriptor handle has one door per kind of descriptor, and the
// constructor that claims an int is private.  Checked from a scope the
// class does not befriend.
static_assert(!std::is_constructible_v<OwnedFd, int>,
              "The constructor over a descriptor must not be public: a caller could hand it any small integer and "
              "the destructor would close it.  Take a handle from mint_file or open_dirfd.");
static_assert(std::is_default_constructible_v<OwnedFd>, "The empty handle claims nothing, so it stays reachable.");
static_assert(!std::is_copy_constructible_v<OwnedFd>);
static_assert(std::is_nothrow_move_constructible_v<OwnedFd>);
static_assert(sizeof(OwnedFd) == sizeof(int), "OwnedFd is exactly the descriptor.");

}  // namespace fixy::fs::detail::fd_surface_invariants
