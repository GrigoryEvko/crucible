// The scheduling gate refuses a context that owns neither Bg nor Init.
// A test context owns neither Bg nor Init, although its row is otherwise wide.  The mint's requires-clause is the whole gate, and the
// failure is that clause, at the call.
//
// The context is taken by reference, so nothing here has to build one.

#include <fixy/os/Sched.h>

namespace eff = foundation::effects;

namespace {

using TestCtx = eff::ExecCtx<eff::ctx_cap::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

[[maybe_unused]] void attempt(TestCtx const& ctx) {
    [[maybe_unused]] auto refused = fixy::sched::mint_priority<5>(ctx);
}

}  // namespace

int main() { return 0; }
