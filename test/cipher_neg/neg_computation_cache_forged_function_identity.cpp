// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A computation cache key refuses a function reached through the static
// invoker of a closure, because the invoker prints the name of its
// closure, and that name depends on the translation unit.  Here a
// translation unit tries to put a verdict of a stable identity in front
// of the identity walk, through a variable template of
// foundation::reflect.  No such template exists.  The gate of the key
// calls the walk through a concept, and the key refuses the invoker.
//
// Expected diagnostic: the static assertion of the key refuses the
// function, and the unsatisfied clause is the identity of the function.

#include <crucible/cipher/ComputationCache.h>

namespace closure_key {
inline constexpr int (*twice)(int) = +[](int value) { return value * 2; };
}  // namespace closure_key

template <>
inline constexpr bool foundation::reflect::function_has_stable_identity_v<closure_key::twice> = true;

int main() { return crucible::cipher::computation_cache_key<closure_key::twice, int> == 0 ? 1 : 0; }
