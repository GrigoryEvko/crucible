// copy() writes each element of the source into the target.  Two fixed
// extents that differ cannot agree on the count, so the call is refused at
// compile time.

#include <foundation/core/Region.h>

inline void copy_four_from_eight(::foundation::core::View<int, 4> target,
                                 ::foundation::core::View<int const, 8> source) {
    ::foundation::core::copy(target, source);
}

int main() {}
