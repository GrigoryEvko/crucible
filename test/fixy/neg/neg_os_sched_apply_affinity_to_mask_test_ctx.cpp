// The runtime mask door refuses a context that owns neither Bg nor Init.
// A test context owns neither effect, although its row is otherwise wide.
// A test that must pin makes a background or an init context.  The
// requires-clause of the door is the whole gate, and the failure is that
// clause, at the call.
//
// The context is taken by reference, so nothing here has to build one.

#include <fixy/os/Sched.h>

namespace eff = foundation::effects;

namespace {

using TestCtx = eff::ExecCtx<eff::ctx_cap::Test,
                             eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

[[maybe_unused]] void attempt(TestCtx const& ctx, fixy::AffinityMask mask) {
    [[maybe_unused]] auto refused = fixy::sched::apply_affinity_to_mask(ctx, mask);
}

}  // namespace

int main() { return 0; }
