// The scheduling gate refuses a context that owns neither Bg nor Init.
// The foreground hot path owns neither Bg nor Init.  The mint's requires-clause is the whole gate, and the
// failure is that clause, at the call.
//
// The context is taken by reference, so nothing here has to build one.

#include <fixy/os/Sched.h>

namespace eff = foundation::effects;

namespace {

using ForegroundCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;

[[maybe_unused]] void attempt(ForegroundCtx const& ctx) {
    [[maybe_unused]] auto refused = fixy::sched::mint_affinity<fixy::AffinityMask::single(0)>(ctx);
}

}  // namespace

int main() { return 0; }
