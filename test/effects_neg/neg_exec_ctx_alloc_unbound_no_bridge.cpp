// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-2 (#853): ctx_alloc::Unbound has no wrapper-enum bridge.
//
// Violation: ctx_alloc::Unbound is the "no allocator policy
// committed" sentinel.  It has no analogue in the
// algebra::lattices::AllocClassTag enum, so the bridge trait is
// uninhabited for it — instantiating to_alloc_class_tag_v<Unbound>
// hard-errors with "incomplete type" rather than silently
// substituting a wrong AllocClassTag value.
//
// Expected diagnostic: incomplete type / no member named 'value' /
// invalid use of incomplete type — the missing specialisation
// surfaces as an undefined struct.

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

constexpr auto bad =
    eff::to_alloc_class_tag_v<eff::ctx_alloc::Unbound>;
