// The chunks of a temporary FixedArray would point into an object that the
// full expression ends.

#include <foundation/core/Array.h>

inline auto dangling_chunks() { return ::foundation::core::FixedArray<int, 4>{}.chunks<2>(); }

int main() {}
