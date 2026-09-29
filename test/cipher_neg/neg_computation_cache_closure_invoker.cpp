// A computation cache key refuses a function reached through the static
// invoker of a closure.  The key folds the printed name of the function,
// and the invoker prints the name of its closure, which depends on the
// translation unit.

#include <crucible/cipher/ComputationCache.h>

namespace closure_key {
inline constexpr int (*twice)(int) = +[](int value) { return value * 2; };
}  // namespace closure_key

int main() { return crucible::cipher::computation_cache_key<closure_key::twice, int> == 0 ? 1 : 0; }
