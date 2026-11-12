// A View of a temporary FixedArray would point into an object that the full
// expression ends.

#include <foundation/core/Array.h>

inline ::foundation::core::View<int, 4> dangling_view() { return ::foundation::core::FixedArray<int, 4>{}.view(); }

int main() {}
