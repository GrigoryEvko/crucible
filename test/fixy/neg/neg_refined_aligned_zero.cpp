// Aligned<0>: an alignment of zero.
//
// The mask of Aligned<0> is the largest address, so only the null pointer
// would pass the test.  Zero is no alignment, so the predicate refuses it.

#include <fixy/Predicates.h>

int main() {
    [[maybe_unused]] ::fixy::Aligned<0> zero_alignment{};
    return 0;
}
