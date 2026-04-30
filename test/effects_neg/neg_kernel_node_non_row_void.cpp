// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-J01-AUDIT round 2: KernelNode<void> rejection witness.
//
// Same shape as neg_kernel_node_non_row_int but probing void as the
// argument.  void is the canonical "type-erased" sentinel — accepting
// it would silently let the row-discipline drop entirely.  Forcing
// the partial-specialization rule rejects this at compile time.
//
// Expected diagnostic: "invalid use of incomplete type" / "incomplete
// type".

#include <crucible/forge/KernelNode.h>

int main() {
    using BadKernelNode = ::crucible::forge::KernelNode<void>;
    static_assert(sizeof(BadKernelNode) > 0);
    return 0;
}
