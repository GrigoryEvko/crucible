// A caller walks the keys of a bit set with a loop of its own.  The loop
// would hold a cursor of the set, and a cursor that a caller holds could
// outlive the set: its keys would read words that a different object reuses.
// A set has no begin(), and its frame door with_keys lends the walk to a body.

#include <foundation/core/BitSet.h>

int main() {
    ::foundation::core::BitSet<8> set{};
    set.set(::foundation::core::index<3>());
    int sum = 0;
    for (::foundation::core::Index<8> const key : set) {
        sum += static_cast<int>(key.raw());
    }
    return sum;
}
