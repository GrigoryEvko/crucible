// A BitSet takes an Index of the set and no number, so no index reaches the
// bits without its check.

#include <foundation/core/BitSet.h>

int main() {
    ::foundation::core::BitSet<64> const set{};
    return set.test(5) ? 1 : 0;
}
