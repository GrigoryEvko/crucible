// D002: unbounded recursion x unbounded cost.  Recursion with neither a
// depth bound nor a cost bound is a stack overflow the type system could
// have refused: the recursion atom carries a maximum depth, the
// Complexity atom says the work is unbounded, and together they describe
// a frame count nothing limits.
//
// The pack trips D002 alone; no other live rule reads the CallShape or
// Complexity axis.  Replacing cost_unbounded with cost_linear<N> admits
// it, which is the rule's content: the depth is fine once the work per
// level is bounded.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::dispatch::recurses<64>, ::fixy::atom::cost_unbounded> refused{};
    return 0;
}
