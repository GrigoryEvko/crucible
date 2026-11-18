// D002: unbounded recursion x unbounded cost.  A recursion with neither a
// depth bound nor a cost bound is a stack overflow the type system could
// have refused: the recursion atom states no maximum depth, the
// Complexity atom says the work is unbounded, and together they describe
// a frame count nothing limits.
//
// The pack trips D002 alone; no other live rule reads the CallShape or
// Complexity axis.  Replacing cost_unbounded with cost_linear<N> admits
// it, because the bounded work bounds the depth.  Replacing the recursion
// atom with recurses<N> admits it too, because the proven depth bounds the
// stack.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::dispatch::recurses_unbounded, ::fixy::atom::cost_unbounded>
        refused{};
    return 0;
}
