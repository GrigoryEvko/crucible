// A partial specialization gives each instance that it matches the
// members that it declares, while each instance keeps the place of the
// primary template.  This file tries to forge a refinement over
// bounded_above<4096> that holds 5000: it writes a partial specialization
// of BoundedAbove for each ceiling above 1000.  The seal of the mint finds
// the call operator outside fixy/Predicates.h and stops the build.

#include <fixy/Refined.h>

template <auto Max>
    requires(Max > 1000)
struct fixy::BoundedAbove<Max> {
    template <class V>
    constexpr bool operator()(V) const noexcept {
        return true;
    }
};

constexpr auto forged = fixy::mint_refined<fixy::bounded_above<4096>>(5000);

int main() { return forged.value(); }
