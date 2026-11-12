// A bounded predicate compares a value with its bound in its own call
// operator, and no helper template stands between the two.  This file
// tries to specialize such a helper, so that in_range<5, 9> would admit
// zero.  No helper template exists, so the specialization does not
// compile.

#include <fixy/Refined.h>

template <>
constexpr bool fixy::refined::at_least<5, int>(int) noexcept {
    return true;
}

int main() { return 0; }
