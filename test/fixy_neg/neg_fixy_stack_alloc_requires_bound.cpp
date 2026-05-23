// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-246 HS14 fixture (Stack 1/2) — stack::alloc bound is mandatory.
//
// `grant::stack::alloc<MaxBytes>` carries NO default template argument:
// every explicit stack-budget grant MUST state its byte ceiling.  A bare
// `grant::stack::alloc<>` is ill-formed — "wrong number of template
// arguments".  (The ≤4 KB axis strict default is engaged via
// accept_default_strict_for_StackUse, NOT via an unbounded alloc.)
//
// Mismatch class: template-argument ARITY (missing non-defaulted NTTP).
//
// Expected diagnostic: a GCC template-argument-count error.

#include <crucible/fixy/grant/Stack.h>

namespace stk = crucible::fixy::grant::stack;

using Bad = stk::alloc<>;  // missing the mandatory MaxBytes bound

int main() {
    [[maybe_unused]] Bad bad{};
    return 0;
}
