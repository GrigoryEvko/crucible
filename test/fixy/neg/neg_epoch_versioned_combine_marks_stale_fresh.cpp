// EpochVersioned has no combine.  A combine that kept the left payload
// under the join of the two versions would mark a stale payload with
// the newer version, so a stale read would pass a freshness gate.
// select_fresher() returns one operand with its own version, or refuses
// an incomparable pair.

#include <fixy/EpochVersioned.h>

int main() {
    fixy::EpochVersioned<int> const older = fixy::EpochVersioned<int>::at_genesis(10);
    fixy::EpochVersioned<int> const newer = fixy::EpochVersioned<int>::at_genesis(20);
    return older.combine_max(newer).peek();
}
