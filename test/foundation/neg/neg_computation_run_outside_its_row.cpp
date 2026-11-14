// The payload of an engaged row leaves through run, into a context whose
// row covers the row of the value.  The foreground context claims no atom,
// so it does not run a value that claims the background atom.

#include <foundation/effects/Computation.h>

int main() {
    namespace fe = ::foundation::effects;
    using BgCtx = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>>;
    BgCtx const ctx{fe::testing::bg()};
    auto const engaged = fe::Computation<fe::Row<>, int>::mint_computation_in_ctx<fe::Effect::Bg>(ctx, 7);
    fe::ExecCtx<> const foreground = fe::testing::foreground();
    return engaged.run(foreground);
}
