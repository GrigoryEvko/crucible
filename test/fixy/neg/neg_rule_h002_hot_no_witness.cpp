// H002: hot x no refinement witness.
//
// A hot body buys its nanoseconds by assuming an invariant rather than
// checking it — that is what the budget pays for.  Something upstream has
// to have proved the invariant, and the Refinement grade is where the
// binding carries that proof.  pred::True, the strict pole, is the
// absence of one.
//
// The cost grade is in the pack so this fixture trips H002 alone.  With
// no Complexity grade the same pack would trip H001 too, and the regex
// below would then be reading a rule the file does not claim.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot, ::fixy::atom::cost_constant> refused{};
    return 0;
}
