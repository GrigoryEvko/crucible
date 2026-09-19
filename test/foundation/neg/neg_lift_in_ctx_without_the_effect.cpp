// The witnessed lift checks the claim against the context's row.  A
// context whose row lacks IO cannot witness an IO claim.

#include <foundation/effects/Computation.h>

int main() {
    ::foundation::effects::detail::exec_ctx_self_test::BgWitness bg{};  // Row<Bg, Alloc>: no IO
    using Pure = ::foundation::effects::Computation<::foundation::effects::Row<>, int>;
    [[maybe_unused]] auto claimed = Pure::lift_in<::foundation::effects::Effect::IO>(bg, 1);
    return 0;
}
