// A predicate class with a data member is a family of predicates under
// one type: at_least{3} and at_least{9} share the type, so the two
// refinements would share one row hash.  A predicate must be a stateless
// class, and a bound belongs in a template argument, as in
// fixy::bounded_below<3>.

#include <fixy/Refined.h>

namespace {
struct at_least {
    int floor;
    [[nodiscard]] constexpr bool operator()(int value) const noexcept { return value >= floor; }
};
}  // namespace

int main() {
    auto big = fixy::mint_refined<at_least{3}>(5);
    return big.value();
}
