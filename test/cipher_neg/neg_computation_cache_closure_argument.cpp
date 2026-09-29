// A computation cache key refuses an argument type that is a closure.
// The key folds the printed name of each argument type, and a closure
// prints a name that is not a function of the closure.

#include <crucible/cipher/ComputationCache.h>

namespace closure_key {
inline int apply(int value) { return value; }
inline constexpr auto marker = [](int value) { return value; };
}  // namespace closure_key

int main() {
    return crucible::cipher::computation_cache_key<&closure_key::apply, decltype(closure_key::marker)> == 0 ? 1 : 0;
}
