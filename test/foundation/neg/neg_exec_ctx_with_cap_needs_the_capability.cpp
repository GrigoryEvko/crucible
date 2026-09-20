// Promoting the capability axis takes the capability, not its name.
//
// with_cap used to be `with_cap<NewCap>()` — a template argument naming
// the type, returning a context that owned one.  So
// `ExecCtx<>{}.with_cap<Init>()` climbed from a foreground context to an
// init context in a single call, supplying no evidence whatever.  That
// was the hole, reached by its second route.
//
// Ctx.h asserted the opposite in prose: "no chain of calls turns a
// foreground context into one that claims a background effect."  This
// call was that chain.  The comment now describes what the code does.
//
// with_cap takes a NewCap VALUE.  Cap's own default constructor is
// private and passkey-gated, so a caller holding one obtained it from
// mint_bg_context, mint_init_context or mint_test_context — the rule
// that a gate must consume what it authorises.
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
    constexpr auto climbed = fe::ExecCtx<>{}.with_cap<fe::Init>();
    static_cast<void>(climbed);
    return 0;
}
