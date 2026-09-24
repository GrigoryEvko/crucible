// The refinement lattice is keyed by the predicate's type.  Two function
// pointers of one signature share that type, so Refined<&is_even, int> and
// Refined<&is_odd, int> would take one row hash and one federation slot
// although they are two claims.  A predicate must be a stateless class.

#include <fixy/Refined.h>

namespace {
constexpr bool is_even(int value) noexcept { return value % 2 == 0; }
}  // namespace

int main() {
    auto even = fixy::mint_refined<&is_even>(4);
    return even.value();
}
