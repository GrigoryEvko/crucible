// Code outside the walk of the keys of a bit set makes an Index through the
// road of the walk.  The road gives an Index with no check, and its proof
// holds only for the bits of a live BitSet<N>, so a number of any other origin
// could become an Index past its count.  The road is private, and the cursor
// of the walk is its one friend.

#include <foundation/core/Index.h>

int main() {
    auto const forged = ::foundation::core::Index<8>::of_bit_of_set_(9uz);
    return static_cast<int>(forged.raw());
}
