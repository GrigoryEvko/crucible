// A stamp comes only from a version source.  Its constructor from two
// counts is private, so a producer cannot vouch for its own version.

#include <fixy/EpochVersioned.h>

int main() {
    fixy::VersionStamp const stamp{fixy::EpochLattice::successor(fixy::EpochLattice::bottom()),
                                   fixy::GenerationLattice::successor(fixy::GenerationLattice::bottom())};
    return static_cast<int>(stamp.epoch().raw());
}
