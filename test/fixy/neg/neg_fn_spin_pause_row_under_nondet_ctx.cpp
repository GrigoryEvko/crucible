// A spin on a pause has no exit until the value arrives, so the atom
// spin_pause lifts to Row<Div>.  A context that owns Nondet and no Div can
// end a spin at a budget, but it cannot run a spin with no end, so it does
// not admit the binding.

#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/atoms/Sync.h>
#include <foundation/effects/Ctx.h>

namespace {

namespace fe = ::foundation::effects;

using NondetCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::Nondet>>;

template <class Ctx, class Bound>
    requires ::fixy::CtxAdmitsBinding<Ctx, Bound>
[[nodiscard]] int spin_on(Ctx const&, Bound const& bound) noexcept {
    return bound.value();
}

}  // namespace

int main() {
    NondetCtx const ctx{fe::testing::test()};
    const auto bound = ::fixy::mint_fn<int, ::fixy::atom::sync::spin_pause>(3);
    return spin_on(ctx, bound);
}
