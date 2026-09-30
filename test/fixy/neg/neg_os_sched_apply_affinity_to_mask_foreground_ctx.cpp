// The runtime mask door refuses a context that owns neither Bg nor Init.
// The foreground hot path owns neither effect.  A new pin moves the
// thread between two recorded operations.  The requires-clause of the
// door is the whole gate, and the failure is that clause, at the call.
//
// The context is taken by reference, so nothing here has to build one.

#include <fixy/os/Sched.h>

namespace eff = foundation::effects;

namespace {

using ForegroundCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;

[[maybe_unused]] void attempt(ForegroundCtx const& ctx, fixy::AffinityMask mask) {
    [[maybe_unused]] auto refused = fixy::sched::apply_affinity_to_mask(ctx, mask);
}

}  // namespace

int main() { return 0; }
