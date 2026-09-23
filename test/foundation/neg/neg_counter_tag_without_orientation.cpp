// A counter tag must state whether its up is the weaker or the stronger
// claim.  A tag that states nothing is not a counter tag, so a new axis
// cannot skip the question that decides whether Graded may read it.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

#include <string_view>

struct replay_step {
    static constexpr std::string_view lattice_name = "ReplayStepLattice";
};

int main() {
    namespace fl = ::foundation::algebra::lattices;
    return static_cast<int>(fl::StrongCounterLattice<replay_step>::bottom().raw());
}
