// A WideStep holds at least one item and at most its bound.  The one door from
// a value checks the two bounds, so a step past the bound is no constant.

#include <fixy/Mutation.h>

int main() {
    constexpr ::fixy::WideStep<4> step = ::fixy::WideStep<4>::check_within_bound(5);
    return static_cast<int>(step.value());
}
