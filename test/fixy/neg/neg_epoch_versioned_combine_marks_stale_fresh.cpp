// The old wrapper combined two values into the left payload under the
// join of the two versions.  An old payload combined with a newer value
// came out marked with the newer version, so a stale read passed a
// freshness gate.  The combine does not exist.  select_fresher() returns
// one operand with its own version, or refuses an incomparable pair.

#include <fixy/EpochVersioned.h>

int main() {
    fixy::EpochVersioned<int> const older = fixy::EpochVersioned<int>::at_genesis(10);
    fixy::EpochVersioned<int> const newer = fixy::EpochVersioned<int>::at_genesis(20);
    return older.combine_max(newer).peek();
}
