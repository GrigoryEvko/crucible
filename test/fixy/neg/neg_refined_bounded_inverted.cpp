// Bounded<10, 5, int> is the empty interval [10, 5].  The alias
// trampoline refuses it at instantiation, so the mistake surfaces once
// where the type is named instead of at every minted value.

#include <fixy/Refined.h>

int main() {
    fixy::Bounded<10, 5, int> refused = fixy::mint_refined_trusted<fixy::in_range<10, 5>>(7);
    return refused.value();
}
