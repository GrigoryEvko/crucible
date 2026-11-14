// The rvalue substrate view moves the substrate out, and its consume then
// moves the payload out with no gate.  So an engaged row has no view, and
// the payload of a value that claims the background atom stays in its row.

#include <foundation/effects/Computation.h>

#include <utility>

int main() {
    namespace fe = ::foundation::effects;
    using BgCtx = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>>;
    BgCtx const ctx{fe::testing::bg()};
    auto engaged = fe::Computation<fe::Row<>, int>::mint_computation_in_ctx<fe::Effect::Bg>(ctx, 7);
    return std::move(engaged).graded().consume();
}
