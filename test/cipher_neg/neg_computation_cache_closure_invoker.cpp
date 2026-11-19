// A computation cache key refuses a function reached through the static
// invoker of a closure.  The key folds the printed name of the function,
// and the invoker prints the name of its closure, which depends on the
// translation unit.

#include <crucible/cipher/ComputationCache.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture keys a closure through the raw pointer of its static invoker")
namespace closure_key {
inline constexpr int (*twice)(int) = +[](int value) { return value * 2; };
}  // namespace closure_key
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return crucible::cipher::computation_cache_key<closure_key::twice, int> == 0 ? 1 : 0; }
