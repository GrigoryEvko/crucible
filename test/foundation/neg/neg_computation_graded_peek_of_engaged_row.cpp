// The substrate view reads the payload with no gate.  So an engaged row
// has no view, and a peek through graded() cannot take the payload of a
// value that claims the background atom out of its row.

#include <foundation/effects/Computation.h>

int main() {
    namespace fe = ::foundation::effects;
    using BgCtx = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>>;
    BgCtx const ctx{fe::testing::bg()};
    auto const engaged = fe::Computation<fe::Row<>, int>::mint_computation_in_ctx<fe::Effect::Bg>(ctx, 7);
    return engaged.graded().peek();
}
