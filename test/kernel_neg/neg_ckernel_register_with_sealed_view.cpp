// Negative-compile test. This file must fail to compile.
//
// Violation: passing a CKernelTable::SealedView to register_op.  The
// typed overload only accepts MutableView — Sealed cannot substitute
// for Mutable, and no Sealed overload exists.  Structural guarantee:
// once the classifier table is sealed, no call site can reach
// register_op via the typed path.

#include <crucible/CKernel.h>
#include <fixy/Ctx.h>

// A write section of the table.  The fixture never links, so the function
// has no body.
crucible::WriteSection<crucible::CKernelTable>& held_section() noexcept;

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    crucible::CKernelTable t;
    t.seal(test);
    auto sv = t.mint_sealed_view();

    // No register_op(SealedView, ...) overload exists.  The typed
    // overload takes MutableView, and Sealed cannot convert to Mutable.
    t.register_op(held_section(), sv, crucible::SchemaHash{0x42}, crucible::CKernelId::GEMM_MM);
    return 0;
}
