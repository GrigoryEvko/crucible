// A context is not evidence of a capability.  It carries one.
//
// ExecCtx is friended by every capability type so the capability member
// can be initialized.  A public default constructor on a specialization
// would build that member.  The result would be a context that satisfies
// CtxOwnsCapability for every effect in its row.  It would reach the
// capability by constructing the thing that holds it, without the
// passkey that guards Init's own constructor.
//
// No specialization has a default constructor.  Each one takes its
// capability source as a constructor argument, and the foreground source
// takes the key of the producer claim.
//
// This fixture is one of a pair.  Its sibling,
// neg_exec_ctx_with_cap_needs_the_capability.cpp, closes the builder
// route.  Fixing either alone leaves the gate open, so both are
// required — the same reason a negative fixture needs two regexes.
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
