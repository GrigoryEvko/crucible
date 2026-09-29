// The substrate under EpochVersioned weakens toward the older version
// only.  A weaken to a newer version would mark an old payload fresh, and
// the CRUCIBLE_PRE guard in Graded::weaken refuses it during constant
// evaluation, because the version order is the dual one.

#include <fixy/EpochVersioned.h>

namespace {

using Grade = fixy::EpochVersioned<int>::graded_type;
using Version = fixy::EpochVersioned<int>::version_t;

// The fixture is the authority for the substrate it builds.
struct Authority {
    [[nodiscard]] static constexpr ::foundation::algebra::grade_key<Authority> key() noexcept {
        return ::foundation::algebra::grade_key<Authority>{};
    }
};

constexpr Grade stale{Authority::key(), 1, Version{fixy::EpochLattice::bottom(), fixy::GenerationLattice::bottom()}};
static_assert(stale
                  .weaken(Version{fixy::EpochLattice::successor(fixy::EpochLattice::bottom()),
                                  fixy::GenerationLattice::successor(fixy::GenerationLattice::bottom())})
                  .peek()
              == 1);

}  // namespace

int main() { return 0; }
