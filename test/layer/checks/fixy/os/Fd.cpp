// The compile-time checks of fixy/os/Fd.h.

#include <fixy/os/Fd.h>

#include <type_traits>

namespace fixy::fs::detail::fd_surface_invariants {

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

static_assert(CtxAdmitsFs<IoBlockCtx>);
static_assert(!CtxAdmitsFs<IoOnlyCtx>);

}  // namespace fixy::fs::detail::fd_surface_invariants
