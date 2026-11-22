// Negative compile test.  This file must fail to compile.
//
// pass writes the pass line of a test, and only a test calls it.  An init
// context owns no Test capability, so the concept TestContext refuses it at
// the call.
//
// Expected diagnostic: pass has no viable candidate, and the note names the
// concept.

#include <fixy/check/Check.h>

namespace eff = foundation::effects;

namespace {
using InitCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;
}  // namespace

int main() {
    InitCtx const ctx{eff::testing::init()};
    fixy::check::pass(ctx, "a line that no test writes\n");
    return 0;
}
