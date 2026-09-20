// A context is not evidence of a capability.  It carries one.
//
// ExecCtx is friended by every capability type so the capability member
// can be initialized, and its own default constructor used to be public
// for every specialization.  Naming a specialization and default-
// constructing it therefore built the capability member and produced a
// context that satisfied CtxCanMint for every effect that source
// permits — reaching the capability by constructing the thing that
// holds it, without ever passing the passkey guarding Init's own
// constructor.  That was the hole.
//
// The default constructor is now constrained to the foreground source,
// which claims nothing and so has nothing to forge.  Every other
// specialization takes the capability as a constructor argument.
//
// This fixture is one of a pair.  Its sibling,
// neg_exec_ctx_with_cap_needs_the_capability.cpp, closes the builder
// route.  Fixing either alone leaves the gate open, so both are
// required — the same reason GUARD-5's floor demands two regexes.
//
// VIOLATION: a TU names an init context and builds it from nothing.
//
// Expected diagnostic: no matching constructor for that specialization.

#include <foundation/effects/Ctx.h>

namespace {
namespace fe = ::foundation::effects;
using ForgedRow = fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO>;
}  // namespace

int main() {
    constexpr fe::ExecCtx<fe::Init, ForgedRow> forged{};
    static_cast<void>(forged);
    return 0;
}
