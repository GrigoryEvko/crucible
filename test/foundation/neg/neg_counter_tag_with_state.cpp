// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A counter tag names one axis, and it adds no storage, so two counts of
// one axis differ only in their value.  The tag below states a name and
// an orientation, as a tag must, but it also carries a data member.  The
// emptiness check of CounterTag is what refuses it.
//
// Expected diagnostic: the template constraint of StrongCounterLattice is
// not satisfied, and the note names the emptiness check.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>

#include <string_view>

struct stateful_step {
    static constexpr std::string_view lattice_name = "StatefulStepLattice";
    static constexpr ::foundation::algebra::ClaimOrientation claim_orientation =
        ::foundation::algebra::ClaimOrientation::weaker_is_higher;
    int stray = 0;
};

int main() {
    namespace fl = ::foundation::algebra::lattices;
    return static_cast<int>(fl::StrongCounterLattice<stateful_step>::bottom().raw());
}
