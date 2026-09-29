// Promoting the capability axis takes the capability, not its name.
//
// A `with_cap<NewCap>()` that took only a template argument naming the
// type would return a context that owns one.  Then
// `ExecCtx<>{}.with_cap<Init>()` would climb from a foreground context to
// an init context in a single call, with no evidence.  No chain of calls
// can turn a foreground context into one that claims a background
// effect.
//
// with_cap takes a NewCap VALUE.  Cap's own default constructor is
// private and passkey-gated, so a caller holding one obtained it from
// mint_context — the rule that a gate must consume what it authorises.
//
// Sibling of neg_exec_ctx_forged_specialization.cpp, which closes the
// specialization route.  Both are required.
//
// VIOLATION: a TU promotes to an init context by naming the type.
//
// Expected diagnostic: no matching call to with_cap taking no argument.

#include <foundation/effects/Ctx.h>

namespace {
namespace fe = ::foundation::effects;
}  // namespace

int main() {
    constexpr auto climbed = fe::testing::foreground().with_cap<fe::Init>();
    static_cast<void>(climbed);
    return 0;
}
