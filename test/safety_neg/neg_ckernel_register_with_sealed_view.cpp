// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a CKernelTable::SealedView to register_op.  The
// typed overload only accepts MutableView — Sealed cannot substitute
// for Mutable, and no Sealed overload exists.  Structural guarantee:
// once the classifier table is sealed, no call site can reach
// register_op via the typed path.

#include <crucible/CKernel.h>

int main() {
    crucible::CKernelTable t;
    t.seal();
    auto sv = t.mint_sealed_view();

    // No register_op(SealedView, ...) overload exists.  The typed
    // overload takes MutableView; the legacy 2-arg overload mismatches
    // arity.  Diagnostic cites "no matching function for call to".
    t.register_op(sv,
                  crucible::SchemaHash{0x42},
                  crucible::CKernelId::GEMM_MM);
    return 0;
}
