// The checked mint is gated on PredicateInvocableOn<Pred, T>.  non_null
// takes a pointer, so an int cannot be fed to it, and the gate refuses
// the pair at the call site with the concept's name rather than with a
// substitution cascade inside the contract.

#include <fixy/Refined.h>

int main() {
    fixy::Refined<fixy::non_null, int> refused = fixy::mint_refined<fixy::non_null, int>(42);
    return refused.value();
}
