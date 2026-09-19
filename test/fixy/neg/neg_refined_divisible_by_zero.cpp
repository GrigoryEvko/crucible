// DivisibleBy<0> would compute `x % 0`.  The predicate's own
// static_assert refuses the divisor when the type is named, so the
// trusted mint below never has to run.

#include <fixy/Refined.h>

int main() {
    fixy::DivisibleByN<0, int> refused = fixy::mint_refined_trusted<fixy::divisible_by<0>>(0);
    return refused.value();
}
