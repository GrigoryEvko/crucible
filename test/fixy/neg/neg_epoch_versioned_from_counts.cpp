// A version is not stated by the producer.  EpochVersioned takes a stamp
// that the version source vouches for, so two counts, even ones reached
// by honest steps, do not make a versioned value.

#include <fixy/EpochVersioned.h>

int main() {
    fixy::EpochVersioned<int> const claimed{1, fixy::EpochLattice::top(), fixy::GenerationLattice::top()};
    return claimed.peek();
}
