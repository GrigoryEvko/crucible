// An Index<N> proves a bound of N.  An index of a set of 128 indexes can be
// past the bits of a set of 64, so a BitSet<64> refuses it.

#include <foundation/core/BitSet.h>

int main() {
    ::foundation::core::BitSet<64> small{};
    ::foundation::core::Index<128> const wide = ::foundation::core::Index<128>::check(100).expect("100 is below 128");
    small.set(wide);
    return static_cast<int>(small.size());
}
