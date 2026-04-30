// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-J01-AUDIT round 2: KernelNode<NonRow> rejection witness.
//
// Violation: instantiating `KernelNode<int>`.  The KernelNode primary
// template is forward-declared but intentionally never defined; only
// the partial specialization on `Row<Es...>` provides a complete
// definition.  Non-Row arguments must therefore hit "incomplete type"
// — the cleanest possible diagnostic, requiring no concept and no
// static_assert in the header.
//
// Locks the partial-specialization design choice into CI: a future
// refactor that accidentally adds a primary-template body (e.g. as a
// "fallback" with arbitrary fields) would silently allow non-Row
// instantiations and produce wrong-row cache slotting downstream.
//
// Expected diagnostic: "invalid use of incomplete type" / "incomplete
// type" / "is incomplete".

#include <crucible/forge/KernelNode.h>

int main() {
    // The primary template KernelNode<typename> has no definition.
    // Asking for sizeof completes the type, which forces the compiler
    // to walk into the (missing) primary specialization body.
    using BadKernelNode = ::crucible::forge::KernelNode<int>;
    static_assert(sizeof(BadKernelNode) > 0);
    return 0;
}
