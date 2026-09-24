// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-5 (#856): AllocClassFromCtx with ctx_alloc::Unbound.
//
// Violation: ctx_alloc::Unbound is the "no allocator policy
// committed" sentinel.  It has no analogue in
// algebra::lattices::AllocClassTag, so AllocClassFromCtx<Ctx, T>
// is uninhabited when Ctx::alloc_class == Unbound.  The trampoline
// fires its static_assert with a clean diagnostic.
//
// Expected diagnostic: "static assertion failed" with the message
// `AllocClassFromCtx requires Ctx::alloc_class to be bound`.

#include <crucible/effects/CtxWrapperLift.h>

namespace eff = crucible::effects;

// ExecCtx<> uses ctx_alloc::Unbound as the default sentinel.
using DefaultCtx = eff::ExecCtx<>;

using BadAlloc = eff::AllocClassFromCtx<DefaultCtx, void*>;

BadAlloc g_bad{nullptr};
