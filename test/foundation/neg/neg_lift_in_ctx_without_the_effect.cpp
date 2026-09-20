// The witnessed lift checks the claim against the context's row.  A
// context whose row lacks IO cannot witness an IO claim.

#include <foundation/effects/Computation.h>

int main() {
    // Built honestly, so the ONLY rejection in this fixture is the one
    // it is named for.  A context carries the capability it claims, and
    // a fixture that also failed to construct its context would reject
    // for two reasons and prove neither.
    ::foundation::effects::detail::exec_ctx_self_test::BgWitness bg{
        ::foundation::effects::testing::bg()};  // Row<Bg, Alloc>: no IO
    using Pure = ::foundation::effects::Computation<::foundation::effects::Row<>, int>;
    [[maybe_unused]] auto claimed = Pure::mint_computation_in_ctx<::foundation::effects::Effect::IO>(bg, 1);
    return 0;
}
